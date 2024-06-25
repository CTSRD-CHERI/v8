// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include "src/base/atomic-utils.h"
#include "src/base/platform/platform.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace base {
namespace {

enum TestFlag : base::AtomicWord { kA, kB, kC };

}  // namespace


TEST(AtomicValue, Initial) {
  AtomicValue<TestFlag> a(kA);
  EXPECT_EQ(TestFlag::kA, a.Value());
}

TEST(AtomicValue, SetValue) {
  AtomicValue<TestFlag> a(kB);
  a.SetValue(kC);
  EXPECT_EQ(TestFlag::kC, a.Value());
}


TEST(AtomicValue, WithVoidStar) {
  AtomicValue<void*> a(nullptr);
  AtomicValue<void*> dummy(nullptr);
  EXPECT_EQ(nullptr, a.Value());
  a.SetValue(&a);
  EXPECT_EQ(&a, a.Value());
}

TEST(AsAtomic8, CompareAndSwap_Sequential) {
  uint8_t bytes[8];
  for (int i = 0; i < 8; i++) {
    bytes[i] = 0xF0 + i;
  }
  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(0xF0 + i,
              AsAtomic8::Release_CompareAndSwap(&bytes[i], i, 0xF7 + i));
  }
  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(0xF0 + i,
              AsAtomic8::Release_CompareAndSwap(&bytes[i], 0xF0 + i, 0xF7 + i));
  }
  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(0xF7 + i, bytes[i]);
  }
}

namespace {

class ByteIncrementingThread final : public Thread {
 public:
  ByteIncrementingThread()
      : Thread(Options("ByteIncrementingThread")),
        byte_addr_(nullptr),
        increments_(0) {}

  void Initialize(uint8_t* byte_addr, int increments) {
    byte_addr_ = byte_addr;
    increments_ = increments;
  }

  void Run() override {
    for (int i = 0; i < increments_; i++) {
      Increment();
    }
  }

  void Increment() {
    uint8_t byte;
    do {
      byte = AsAtomic8::Relaxed_Load(byte_addr_);
    } while (AsAtomic8::Release_CompareAndSwap(byte_addr_, byte, byte + 1) !=
             byte);
  }

 private:
  uint8_t* byte_addr_;
  int increments_;
};

}  // namespace

TEST(AsAtomic8, CompareAndSwap_Concurrent) {
  const int kIncrements = 10;
  const int kByteCount = 8;
  uint8_t bytes[kByteCount];
  const int kThreadsPerByte = 4;
  const int kThreadCount = kByteCount * kThreadsPerByte;
  ByteIncrementingThread threads[kThreadCount];

  for (int i = 0; i < kByteCount; i++) {
    AsAtomic8::Relaxed_Store(&bytes[i], i);
    for (int j = 0; j < kThreadsPerByte; j++) {
      threads[i * kThreadsPerByte + j].Initialize(&bytes[i], kIncrements);
    }
  }
  for (int i = 0; i < kThreadCount; i++) {
    CHECK(threads[i].Start());
  }

  for (int i = 0; i < kThreadCount; i++) {
    threads[i].Join();
  }

  for (int i = 0; i < kByteCount; i++) {
    EXPECT_EQ(i + kIncrements * kThreadsPerByte,
              AsAtomic8::Relaxed_Load(&bytes[i]));
  }
}

TEST(AsAtomicWord, SetBits_Sequential) {
#if defined(__CHERI_PURE_CAPABILITY__)
  size_t word = 0;
#else   // !__CHERI_PURE_CAPABILITY__
  uintptr_t word = 0;
#endif  // !__CHERI_PURE_CAPABILITY__
  // Fill the word with a repeated 0xF0 pattern.
  for (unsigned i = 0; i < sizeof(word); i++) {
    word = (word << 8) | 0xF0;
  }
  // Check the pattern.
  for (unsigned i = 0; i < sizeof(word); i++) {
    EXPECT_EQ(0xF0u, (word >> (i * 8) & 0xFFu));
  }
  // Set the i-th byte value to i.
#if defined(__CHERI_PURE_CAPABILITY__)
  size_t mask = 0xFF;
#else   // !__CHERI_PURE_CAPABILITY__
  uintptr_t mask = 0xFF;
#endif  // !__CHERI_PURE_CAPABILITY__
  for (unsigned i = 0; i < sizeof(word); i++) {
#if defined(__CHERI_PURE_CAPABILITY__)
    size_t byte = static_cast<uintptr_t>(i) << (i * 8);
#else   // !__CHERI_PURE_CAPABILITY__
    uintptr_t byte = static_cast<uintptr_t>(i) << (i * 8);
#endif  // !__CHERI_PURE_CAPABILITY__
    AsAtomicWord::SetBits(&word, byte, mask);
    mask <<= 8;
  }
  for (unsigned i = 0; i < sizeof(word); i++) {
    EXPECT_EQ(i, (word >> (i * 8) & 0xFFu));
  }
}

namespace {

class BitSettingThread final : public Thread {
 public:
  BitSettingThread()
      : Thread(Options("BitSettingThread")),
        word_addr_(nullptr),
        bit_index_(0) {}

#if defined(__CHERI_PURE_CAPABILITY__)
  void Initialize(size_t* word_addr, int bit_index) {
#else   // !__CHERI_PURE_CAPABILITY__
  void Initialize(uintptr_t* word_addr, int bit_index) {
#endif  // !__CHERI_PURE_CAPABILITY__
    word_addr_ = word_addr;
    bit_index_ = bit_index;
  }

  void Run() override {
#if defined(__CHERI_PURE_CAPABILITY__)
    size_t bit = 1;
#else   // !__CHERI_PURE_CAPABILITY__
    uintptr_t bit = 1;
#endif  // !__CHERI_PURE_CAPABILITY__
    bit = bit << bit_index_;
    AsAtomicWord::SetBits(word_addr_, bit, bit);
  }

 private:
#if defined(__CHERI_PURE_CAPABILITY__)
  size_t* word_addr_;
#else   // !__CHERI_PURE_CAPABILITY__
  uintptr_t* word_addr_;
#endif  // !__CHERI_PURE_CAPABILITY__
  int bit_index_;
};

}  // namespace.

TEST(AsAtomicWord, SetBits_Concurrent) {
  const int kBitCount = sizeof(uintptr_t) * 8;
  const int kThreadCount = kBitCount / 2;
  BitSettingThread threads[kThreadCount];

#if defined(__CHERI_PURE_CAPABILITY__)
  size_t word;
#else   // !__CHERI_PURE_CAPABILITY__
  uintptr_t word;
#endif  // !__CHERI_PURE_CAPABILITY__
  AsAtomicWord::Relaxed_Store(&word, 0);
  for (int i = 0; i < kThreadCount; i++) {
    // Thread i sets bit number i * 2.
    threads[i].Initialize(&word, i * 2);
  }
  for (int i = 0; i < kThreadCount; i++) {
    CHECK(threads[i].Start());
  }
  for (int i = 0; i < kThreadCount; i++) {
    threads[i].Join();
  }
#if defined(__CHERI_PURE_CAPABILITY__)
  size_t actual_word = AsAtomicWord::Relaxed_Load(&word);
#else   // !__CHERI_PURE_CAPABILITY__
  uintptr_t actual_word = AsAtomicWord::Relaxed_Load(&word);
#endif  // !__CHERI_PURE_CAPABILITY__
  for (int i = 0; i < kBitCount; i++) {
    // Every second bit must be set.
#if defined(__CHERI_PURE_CAPABILITY__)
    size_t expected = (i % 2 == 0);
#else   // !__CHERI_PURE_CAPABILITY__
    uintptr_t expected = (i % 2 == 0);
#endif  // !__CHERI_PURE_CAPABILITY__
    EXPECT_EQ(expected, actual_word & 1u);
    actual_word >>= 1;
  }
}

}  // namespace base
}  // namespace v8

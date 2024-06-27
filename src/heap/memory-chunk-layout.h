// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_LAYOUT_H_
#define V8_HEAP_MEMORY_CHUNK_LAYOUT_H_

#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/heap/base/active-system-pages.h"
#include "src/heap/list.h"
#include "src/heap/marking.h"
#include "src/heap/progress-bar.h"
#include "src/heap/slot-set.h"

namespace v8 {
namespace internal {

class MarkingBitmap;
class CodeObjectRegistry;
class FreeListCategory;
class Heap;
class TypedSlotsSet;
class SlotSet;

enum RememberedSetType {
  OLD_TO_NEW,
  OLD_TO_NEW_BACKGROUND,
  OLD_TO_OLD,
  OLD_TO_SHARED,
  OLD_TO_CODE,
  NUMBER_OF_REMEMBERED_SET_TYPES
};

using ActiveSystemPages = ::heap::base::ActiveSystemPages;

class V8_EXPORT_PRIVATE MemoryChunkLayout {
 public:
  static constexpr int kNumSets = NUMBER_OF_REMEMBERED_SET_TYPES;
  static constexpr int kNumTypes = ExternalBackingStoreType::kNumTypes;
#if defined(__CHERI_PURE_CAPABILITY__)
  static constexpr int kMemoryChunkAlignment = alignof(max_align_t);
#else   // !__CHERI_PURE_CAPABILITY__
  static constexpr int kMemoryChunkAlignment = sizeof(size_t);
#endif  // !__CHERI_PURE_CAPABILITY__
#define FIELD(Type, Name) \
  k##Name##Offset, k##Name##End = k##Name##Offset + sizeof(Type) - 1
  enum Header {
    // BasicMemoryChunk fields:
    FIELD(size_t, Size),
#if defined(__CHERI_PURE_CAPABILITY__)
    FIELD(size_t, Flags),
#else   // !__CHERI_PURE_CAPABILITY__
    FIELD(uintptr_t, Flags),
#endif  // !__CHERI_PURE_CAPABILITY__
    FIELD(Heap*, Heap),
    FIELD(Address, AreaStart),
    FIELD(Address, AreaEnd),
    FIELD(size_t, AllocatedBytes),
    FIELD(size_t, WastedMemory),
#if defined(__CHERI_PURE_CAPABILITY__)
    FIELD(std::atomic<ptrdiff_t>, HighWaterMark),
    FIELD(size_t, Padding0),
#else   // !__CHERI_PURE_CAPABILITY__
    FIELD(std::atomic<intptr_t>, HighWaterMark),
#endif  // !__CHERI_PURE_CAPABILITY__
    FIELD(Address, Owner),
    FIELD(VirtualMemory, Reservation),
    // MemoryChunk fields:
    FIELD(SlotSet* [kNumSets], SlotSet),
    FIELD(TypedSlotsSet* [kNumSets], TypedSlotSet),
    FIELD(ProgressBar, ProgressBar),
#if defined(__CHERI_PURE_CAPABILITY__)
    FIELD(std::atomic<size_t>, LiveByteCount),
#else   // !__CHERI_PURE_CAPABILITY__
    FIELD(std::atomic<intptr_t>, LiveByteCount),
#endif  // !__CHERI_PURE_CAPABILITY__
    FIELD(base::Mutex*, Mutex),
    FIELD(base::SharedMutex*, SharedMutex),
    FIELD(base::Mutex*, PageProtectionChangeMutex),
#if defined(__CHERI_PURE_CAPABILITY__)
    FIELD(std::atomic<uint64_t>, ConcurrentSweeping),
#else   // !__CHERI_PURE_CAPABILITY__
    FIELD(std::atomic<intptr_t>, ConcurrentSweeping),
#endif  // !__CHERI_PURE_CAPABILITY__
    FIELD(std::atomic<size_t>[kNumTypes], ExternalBackingStoreBytes),
#if defined(__CHERI_PURE_CAPABILITY__)
    FIELD(size_t, Padding1),
#endif   // __CHERI_PURE_CAPABILITY__
    FIELD(heap::ListNode<MemoryChunk>, ListNode),
    FIELD(FreeListCategory**, Categories),
    FIELD(CodeObjectRegistry*, CodeObjectRegistry),
    FIELD(PossiblyEmptyBuckets, PossiblyEmptyBuckets),
    FIELD(ActiveSystemPages*, ActiveSystemPages),
#if defined(__CHERI_PURE_CAPABILITY__)
    FIELD(size_t, AllocatedLabSize),
#else   // !__CHERI_PURE_CAPABILITY__
    FIELD(size_t, AllocatedLabSize),
#endif  // !__CHERI_PURE_CAPABILITY__
    FIELD(MarkingBitmap, MarkingBitmap),
    kEndOfMarkingBitmap,
    kMemoryChunkHeaderSize =
        kEndOfMarkingBitmap +
        ((kEndOfMarkingBitmap % kMemoryChunkAlignment) == 0
             ? 0
             : kMemoryChunkAlignment -
                   (kEndOfMarkingBitmap % kMemoryChunkAlignment)),
    kMemoryChunkHeaderStart = kSlotSetOffset,
    kBasicMemoryChunkHeaderSize = kMemoryChunkHeaderStart,
    kBasicMemoryChunkHeaderStart = 0,
  };
#undef FIELD

  static size_t CodePageGuardStartOffset();
  static size_t CodePageGuardSize();
  // Code pages have padding on the first page for code alignment, so the
  // ObjectStartOffset will not be page aligned.
  static intptr_t ObjectPageOffsetInCodePage();
  static intptr_t ObjectStartOffsetInCodePage();
  static intptr_t ObjectEndOffsetInCodePage();
  static size_t AllocatableMemoryInCodePage();
  static intptr_t ObjectStartOffsetInDataPage();
  static size_t AllocatableMemoryInDataPage();
  static intptr_t ObjectStartOffsetInReadOnlyPage();
  static size_t AllocatableMemoryInReadOnlyPage();
  static size_t ObjectStartOffsetInMemoryChunk(AllocationSpace space);
  static size_t AllocatableMemoryInMemoryChunk(AllocationSpace space);

  static int MaxRegularCodeObjectSize();

  static_assert(kMemoryChunkHeaderSize % alignof(size_t) == 0);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_CHUNK_LAYOUT_H_

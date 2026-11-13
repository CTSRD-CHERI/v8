// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// AAPCS64-cap (Morello purecap) C-call ABI tests.
//
// These tests call *real* C++ callee functions that are compiled by the same
// Morello toolchain as the rest of V8, so the callee side is ABI-correct by
// construction. The caller side is V8-generated code (RawMachineAssemblerTester
// + CallCFunction). Any CHECK failure inside a callee therefore localises a
// deviation in V8's outgoing C-call ABI implementation.
//
// The harness exercises *both* ABI edges:
//   C++ harness --(AAPCS64-cap)--> generated tester --(CallCFunction)--> C++
//   callee. The incoming edge tests V8's callee-side caller-frame-slot decoding
//   (frame.cc caller_frame_slot_size_); the outgoing edge tests V8's
//   caller-side argument layout and Arguments Capability (c9) construction.
//
// What is verified:
//   * Register-passed GP / FP / capability arguments arrive with the right
//     values (Stage C.3, C.9, C.10).
//   * Memory-passed arguments arrive with the right values and capability tags.
//     The callee reads these directly from the Arguments Memory Area via c9, so
//     a wrong outgoing byte layout (offset, stride, alignment, or a capability
//     stored with a tag-clearing X-register store) yields a wrong value, a lost
//     tag, or a bounds fault -- all of which fail a CHECK here.
//   * Memory-argument addresses are derived from c9 using the same bounded
//     Morello ABI mechanism as the native toolchain. The C++ prologue may reuse
//     c9 while loading those arguments, so the tests verify the resulting values
//     and tags rather than asserting on c9 after the prologue.
//   * double and capability results are returned correctly (v0 / c0), with the
//     capability result keeping its tag.
//
// Note on `&arg`: taking the address of a memory-passed parameter yields a
// capability the Morello compiler bounds to that single argument and places in
// a callee-local save area (laid out descending with natural alignment). It does
// *not* point into the caller's AMA and is not c9, so its offset/stride says
// nothing about V8's outgoing layout. These tests therefore assert on argument
// values/tags and on the actual memory-argument reads, never on `&arg` offsets.
//
// Variadic callees are intentionally not covered: the compiler C-call surface
// (GetSimplifiedCDescriptor) assigns registers to all parameters and has no
// notion of anonymous arguments, whereas AAPCS64-cap requires all anonymous
// arguments to be memory-passed. Only hand-written paths (e.g.
// PrintfNoPreserve) handle variadic calls, so a compiler-generated variadic
// C call is outside the supported surface.

#include "src/api/api.h"
#include "src/codegen/external-reference.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/common/code-assembler-tester.h"

#if V8_TARGET_CHERI && V8_TARGET_ARCH_ARM64

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// Distinct, addressable objects so capability arguments carry a real (tagged)
// provenance and the callee can check both the pointer value and dereference it.
int g_values[16] = {100, 101, 102, 103, 104, 105, 106, 107,
                    108, 109, 110, 111, 112, 113, 114, 115};

// Per-argument checks driven by the signature X-macros below. Scalar arguments
// are checked by value; capability arguments additionally check the hardware
// tag and that the pointed-to value is readable (a functional capability).
// These are parenthesized expressions (no trailing semicolon) so that the
// comma-separated X-macro expansion forms a valid comma-expression statement.
#define CHECK_VAL(TYPE, IDX, VALUE) \
  (ok = ok && (a##IDX == static_cast<TYPE>(VALUE)))
#define CHECK_PTR(TYPE, IDX, VALUE)                                    \
  (ok = ok && (a##IDX == VALUE) &&                                     \
   (__builtin_cheri_tag_get(a##IDX) == 1) && (*a##IDX == g_values[IDX]))

// ---------------------------------------------------------------------------
// Group 1: register-only signatures (no memory arguments, hence no AMA / c9).
// ---------------------------------------------------------------------------

#define SIG_REG_I8(V)                                                   \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3), \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6), V(int64_t, 7, 7)

int64_t RegInt64(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                 int64_t a5, int64_t a6, int64_t a7) {
  bool ok = true;
  SIG_REG_I8(CHECK_VAL);
  CHECK(ok);
  return 42;
}

#define SIG_REG_P8(V)                                                       \
  V(int*, 0, &g_values[0]), V(int*, 1, &g_values[1]), V(int*, 2, &g_values[2]), \
      V(int*, 3, &g_values[3]), V(int*, 4, &g_values[4]),                     \
      V(int*, 5, &g_values[5]), V(int*, 6, &g_values[6]),                     \
      V(int*, 7, &g_values[7])

int64_t RegPointer(int* a0, int* a1, int* a2, int* a3, int* a4, int* a5,
                   int* a6, int* a7) {
  bool ok = true;
  SIG_REG_P8(CHECK_PTR);
  CHECK(ok);
  return 42;
}

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

#define SIG_REG_D8(V)                                                     \
  V(double, 0, 0.5), V(double, 1, 1.5), V(double, 2, 2.5), V(double, 3, 3.5), \
      V(double, 4, 4.5), V(double, 5, 5.5), V(double, 6, 6.5),               \
      V(double, 7, 7.5)

int64_t RegDouble(double a0, double a1, double a2, double a3, double a4,
                  double a5, double a6, double a7) {
  bool ok = true;
  SIG_REG_D8(CHECK_VAL);
  CHECK(ok);
  return 42;
}

#define SIG_REG_ID8(V)                                                    \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3),   \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6),                 \
      V(int64_t, 7, 7), V(double, 8, 8.5), V(double, 9, 9.5),               \
      V(double, 10, 10.5), V(double, 11, 11.5), V(double, 12, 12.5),        \
      V(double, 13, 13.5), V(double, 14, 14.5), V(double, 15, 15.5)

int64_t RegInt64Double(int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                       int64_t a4, int64_t a5, int64_t a6, int64_t a7,
                       double a8, double a9, double a10, double a11,
                       double a12, double a13, double a14, double a15) {
  bool ok = true;
  SIG_REG_ID8(CHECK_VAL);
  CHECK(ok);
  return 42;
}

#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

// ---------------------------------------------------------------------------
// Group 2: scalar-only memory arguments. Overflow int64_t / double / int32_t
// are memory-passed; V8 widens sub-8-byte scalars to 8 bytes in the AMA. MALs of
// 8, 16 and 96 exercise lengths below, at, and above a 16-byte multiple.
// ---------------------------------------------------------------------------

#define SIG_MEM_I9(V)                                                   \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3), \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6),               \
      V(int64_t, 7, 7), V(int64_t, 8, 8)

int64_t MemInt64_9(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                   int64_t a5, int64_t a6, int64_t a7, int64_t a8) {
  bool ok = true;
  SIG_MEM_I9(CHECK_VAL);
  CHECK(ok);
  // a8 is the sole memory argument: MAL = 8.
  return 42;
}

#define SIG_MEM_I10(V)                                                  \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3), \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6),               \
      V(int64_t, 7, 7), V(int64_t, 8, 8), V(int64_t, 9, 9)

int64_t MemInt64_10(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                    int64_t a5, int64_t a6, int64_t a7, int64_t a8,
                    int64_t a9) {
  bool ok = true;
  SIG_MEM_I10(CHECK_VAL);
  CHECK(ok);
  // Two memory-passed 8-byte scalars: MAL = 16.
  return 42;
}

#define SIG_MEM_I20(V)                                                      \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3),     \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6), V(int64_t, 7, 7), \
      V(int64_t, 8, 8), V(int64_t, 9, 9), V(int64_t, 10, 10),                 \
      V(int64_t, 11, 11), V(int64_t, 12, 12), V(int64_t, 13, 13),             \
      V(int64_t, 14, 14), V(int64_t, 15, 15), V(int64_t, 16, 16),             \
      V(int64_t, 17, 17), V(int64_t, 18, 18), V(int64_t, 19, 19)

int64_t MemInt64_20(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                    int64_t a5, int64_t a6, int64_t a7, int64_t a8, int64_t a9,
                    int64_t a10, int64_t a11, int64_t a12, int64_t a13,
                    int64_t a14, int64_t a15, int64_t a16, int64_t a17,
                    int64_t a18, int64_t a19) {
  bool ok = true;
  SIG_MEM_I20(CHECK_VAL);
  CHECK(ok);
  // a8..a19 are memory-passed 8-byte scalars: MAL = 12 * 8 = 96.
  return 42;
}

#define SIG_MEM_I32_10(V)                                                 \
  V(int32_t, 0, 0), V(int32_t, 1, 1), V(int32_t, 2, 2), V(int32_t, 3, 3),   \
      V(int32_t, 4, 4), V(int32_t, 5, 5), V(int32_t, 6, 6),                 \
      V(int32_t, 7, 7), V(int32_t, 8, 8), V(int32_t, 9, 9)

int64_t MemInt32_10(int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4,
                    int32_t a5, int32_t a6, int32_t a7, int32_t a8,
                    int32_t a9) {
  bool ok = true;
  SIG_MEM_I32_10(CHECK_VAL);
  CHECK(ok);
  // V8 widens each int32 to 8 bytes in the AMA: MAL = 2 * 8 = 16.
  return 42;
}

int64_t MemBool_10(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                   int64_t a5, int64_t a6, int64_t a7, bool a8, bool a9) {
  CHECK_EQ(28, a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7);
  CHECK(a8);
  CHECK(!a9);
  return 42;
}

int64_t MemInt8_10(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                   int64_t a5, int64_t a6, int64_t a7, int8_t a8, int8_t a9) {
  CHECK_EQ(28, a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7);
  CHECK_EQ(-8, a8);
  CHECK_EQ(9, a9);
  return 42;
}

int64_t MemUint8_10(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                    int64_t a5, int64_t a6, int64_t a7, uint8_t a8,
                    uint8_t a9) {
  CHECK_EQ(28, a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7);
  CHECK_EQ(0xf8, a8);
  CHECK_EQ(0x09, a9);
  return 42;
}

int64_t MemInt16_10(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                    int64_t a5, int64_t a6, int64_t a7, int16_t a8,
                    int16_t a9) {
  CHECK_EQ(28, a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7);
  CHECK_EQ(-1234, a8);
  CHECK_EQ(2345, a9);
  return 42;
}

int64_t MemUint16_10(int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                     int64_t a4, int64_t a5, int64_t a6, int64_t a7,
                     uint16_t a8, uint16_t a9) {
  CHECK_EQ(28, a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7);
  CHECK_EQ(0xf123, a8);
  CHECK_EQ(0x4567, a9);
  return 42;
}

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

#define SIG_MEM_D10(V)                                                    \
  V(double, 0, 0.5), V(double, 1, 1.5), V(double, 2, 2.5), V(double, 3, 3.5), \
      V(double, 4, 4.5), V(double, 5, 5.5), V(double, 6, 6.5),               \
      V(double, 7, 7.5), V(double, 8, 8.5), V(double, 9, 9.5)

int64_t MemDouble_10(double a0, double a1, double a2, double a3, double a4,
                     double a5, double a6, double a7, double a8, double a9) {
  bool ok = true;
  SIG_MEM_D10(CHECK_VAL);
  CHECK(ok);
  // Two memory-passed 8-byte doubles: MAL = 16.
  return 42;
}

#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

// ---------------------------------------------------------------------------
// Group 3: capability memory arguments. Nine or more data-capability
// parameters; every value and tag is checked (a tag-clearing store would fail
// CHECK_PTR), along with the memory-argument values and tags.
// ---------------------------------------------------------------------------

#define SIG_MEM_P10(V)                                                      \
  V(int*, 0, &g_values[0]), V(int*, 1, &g_values[1]), V(int*, 2, &g_values[2]), \
      V(int*, 3, &g_values[3]), V(int*, 4, &g_values[4]),                     \
      V(int*, 5, &g_values[5]), V(int*, 6, &g_values[6]),                     \
      V(int*, 7, &g_values[7]), V(int*, 8, &g_values[8]),                     \
      V(int*, 9, &g_values[9])

int64_t MemPointer_10(int* a0, int* a1, int* a2, int* a3, int* a4, int* a5,
                      int* a6, int* a7, int* a8, int* a9) {
  bool ok = true;
  SIG_MEM_P10(CHECK_PTR);
  CHECK(ok);
  // Two memory-passed 16-byte capabilities: MAL = 32.
  return 42;
}

#define SIG_MEM_P12(V)                                                      \
  V(int*, 0, &g_values[0]), V(int*, 1, &g_values[1]), V(int*, 2, &g_values[2]), \
      V(int*, 3, &g_values[3]), V(int*, 4, &g_values[4]),                     \
      V(int*, 5, &g_values[5]), V(int*, 6, &g_values[6]),                     \
      V(int*, 7, &g_values[7]), V(int*, 8, &g_values[8]),                     \
      V(int*, 9, &g_values[9]), V(int*, 10, &g_values[10]),                   \
      V(int*, 11, &g_values[11])

int64_t MemPointer_12(int* a0, int* a1, int* a2, int* a3, int* a4, int* a5,
                      int* a6, int* a7, int* a8, int* a9, int* a10, int* a11) {
  bool ok = true;
  SIG_MEM_P12(CHECK_PTR);
  CHECK(ok);
  // a8..a11 memory-passed 16-byte capabilities: MAL = 4 * 16 = 64.
  return 42;
}

// ---------------------------------------------------------------------------
// Group 4: mixed memory arguments. GP scalars and capabilities overflow in
// different orders, exercising the 16-byte alignment padding before a
// capability that follows an 8-byte scalar (Stage C.15).
// ---------------------------------------------------------------------------

// 9 int64 then 9 capabilities: a0-a7 in registers, a8 (int64) memory-passed,
// then 9 capabilities. V8's NMAA: a8 @0 (8 bytes), first capability aligned to
// 16, nine 16-byte capabilities => MAL = 160.
#define SIG_MEM_IP(V)                                                       \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3),     \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6), V(int64_t, 7, 7), \
      V(int64_t, 8, 8), V(int*, 9, &g_values[0]), V(int*, 10, &g_values[1]),  \
      V(int*, 11, &g_values[2]), V(int*, 12, &g_values[3]),                   \
      V(int*, 13, &g_values[4]), V(int*, 14, &g_values[5]),                   \
      V(int*, 15, &g_values[6]), V(int*, 16, &g_values[7]),                   \
      V(int*, 17, &g_values[8])

int64_t MemInt64ThenPointer(int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                            int64_t a4, int64_t a5, int64_t a6, int64_t a7,
                            int64_t a8, int* a9, int* a10, int* a11, int* a12,
                            int* a13, int* a14, int* a15, int* a16, int* a17) {
  CHECK_EQ(0, a0);
  CHECK_EQ(7, a7);
  CHECK_EQ(8, a8);
  CHECK(a9 == &g_values[0]);
  CHECK(a17 == &g_values[8]);
  CHECK_EQ(1, __builtin_cheri_tag_get(a9));
  CHECK_EQ(1, __builtin_cheri_tag_get(a17));
  CHECK_EQ(g_values[0], *a9);
  CHECK_EQ(g_values[8], *a17);
  return 42;
}

// 9 capabilities then 9 int64: a0-a7 (capabilities) in registers, a8
// (capability) memory-passed, then 9 int64. V8's NMAA: a8 @0 (16 bytes), nine
// 8-byte scalars => MAL = 16 + 9*8 = 88.
#define SIG_MEM_PI(V)                                                       \
  V(int*, 0, &g_values[0]), V(int*, 1, &g_values[1]), V(int*, 2, &g_values[2]), \
      V(int*, 3, &g_values[3]), V(int*, 4, &g_values[4]),                     \
      V(int*, 5, &g_values[5]), V(int*, 6, &g_values[6]),                     \
      V(int*, 7, &g_values[7]), V(int*, 8, &g_values[8]), V(int64_t, 9, 9),   \
      V(int64_t, 10, 10), V(int64_t, 11, 11), V(int64_t, 12, 12),             \
      V(int64_t, 13, 13), V(int64_t, 14, 14), V(int64_t, 15, 15),             \
      V(int64_t, 16, 16), V(int64_t, 17, 17)

int64_t MemPointerThenInt64(int* a0, int* a1, int* a2, int* a3, int* a4,
                            int* a5, int* a6, int* a7, int* a8, int64_t a9,
                            int64_t a10, int64_t a11, int64_t a12, int64_t a13,
                            int64_t a14, int64_t a15, int64_t a16, int64_t a17) {
  CHECK(a0 == &g_values[0]);
  CHECK(a8 == &g_values[8]);
  CHECK_EQ(1, __builtin_cheri_tag_get(a0));
  CHECK_EQ(1, __builtin_cheri_tag_get(a8));
  CHECK_EQ(g_values[8], *a8);
  CHECK_EQ(9, a9);
  CHECK_EQ(17, a17);
  return 42;
}

// ---------------------------------------------------------------------------
// Group 5: result return. A double result is returned in v0; a capability
// result is returned in c0 and must keep its tag.
//
// (Exact-boundary AMA lengths -- group 5 of the implementation plan -- are not a
// separate group here: they are exercised by the range of MALs across groups
// 2-4 together with the memory-argument layout checks.)
// ---------------------------------------------------------------------------

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
double ReturnDouble(int64_t a0) { return a0 * 1.5; }
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

int* ReturnPointer(int64_t a0) { return &g_values[a0]; }

// Native intptr_t is a 16-byte, 16-aligned integer on Morello, unlike int64_t
// and ptraddr_t. TaggedSigned describes a tagged Smi passed to that native type;
// MachineType::IntPtr() deliberately describes an 8-byte machine integer.
int64_t CheckSmiPassedAsIntPtr(intptr_t value) {
  const intptr_t expected = static_cast<intptr_t>(Smi::FromInt(37).ptr());
  CHECK(value == expected);
  return 42;
}

int64_t CheckSmiHighBitsPassedAsIntPtr(intptr_t value, int64_t following) {
  const intptr_t expected = static_cast<intptr_t>(Smi::FromInt(-37).ptr());
  CHECK(value == expected);
  CHECK(following == INT64_C(0x0123456789abcdef));
  return 42;
}

int64_t CheckSmiHighBitsPassedAsInt64(int64_t value, int64_t following) {
  const int64_t expected = static_cast<int64_t>(Smi::FromInt(-37).ptr());
  CHECK(value == expected);
  CHECK(following == INT64_C(0x0123456789abcdef));
  return 42;
}

// JSDate::GetField uses this Address-shaped parameter for a raw tagged Smi.
int64_t CheckSmiPassedAsAddress(Address value, int64_t following) {
  CHECK(Tagged<Smi>(value).value() == -37);
  CHECK(following == INT64_C(0x0123456789abcdef));
  return 42;
}

#define SIG_MEM_SMI_10(TYPE, V)                                              \
  V(TYPE, 0, 0), V(TYPE, 1, 1), V(TYPE, 2, 2), V(TYPE, 3, 3), V(TYPE, 4, 4), \
      V(TYPE, 5, 5), V(TYPE, 6, 6), V(TYPE, 7, 7), V(TYPE, 8, -37),          \
      V(TYPE, 9, 9)
#define CHECK_SMI_VAL(TYPE, IDX, VALUE) \
  (ok = ok && (a##IDX == static_cast<TYPE>(Smi::FromInt(VALUE).ptr())))

#define DECLARE_SMI_MEM_PARAM(TYPE, IDX, VALUE) TYPE a##IDX
#define SMI_MEM_PARAMS(TYPE) SIG_MEM_SMI_10(TYPE, DECLARE_SMI_MEM_PARAM)

int64_t MemSmiIntPtr_10(SMI_MEM_PARAMS(intptr_t)) {
  bool ok = true;
  SIG_MEM_SMI_10(intptr_t, CHECK_SMI_VAL);
  CHECK(ok);
  return 42;
}

int64_t MemSmiInt64_10(SMI_MEM_PARAMS(int64_t)) {
  bool ok = true;
  SIG_MEM_SMI_10(int64_t, CHECK_SMI_VAL);
  CHECK(ok);
  return 42;
}

int64_t MemSmiThenInt64(int* a0, int* a1, int* a2, int* a3, int* a4, int* a5,
                        int* a6, int* a7, intptr_t smi, int64_t following) {
  CHECK(a0 == &g_values[0]);
  CHECK(a7 == &g_values[7]);
  CHECK(smi == static_cast<intptr_t>(Smi::FromInt(-37).ptr()));
  CHECK_EQ(INT64_C(0x0123456789abcdef), following);
  return 42;
}

int64_t MemInt64ThenSmi(int* a0, int* a1, int* a2, int* a3, int* a4, int* a5,
                        int* a6, int* a7, int64_t preceding, intptr_t smi) {
  CHECK(a0 == &g_values[0]);
  CHECK(a7 == &g_values[7]);
  CHECK_EQ(INT64_C(0x0123456789abcdef), preceding);
  CHECK(smi == static_cast<intptr_t>(Smi::FromInt(-37).ptr()));
  return 42;
}

int64_t RegisterMixedSmiInt64(intptr_t first, int64_t second, intptr_t third,
                              int64_t fourth) {
  CHECK(first == static_cast<intptr_t>(Smi::FromInt(11).ptr()));
  CHECK_EQ(INT64_C(0x0123456789abcdef), second);
  CHECK(third == static_cast<intptr_t>(Smi::FromInt(-13).ptr()));
  CHECK_EQ(INT64_C(0x1020304050607080), fourth);
  return 42;
}

int64_t TailCallMixedSpills(int* a0, int* a1, int* a2, int* a3, int* a4,
                            int* a5, int* a6, int* a7, intptr_t smi,
                            int64_t following) {
  CHECK(a0 == &g_values[0]);
  CHECK(a7 == &g_values[7]);
  CHECK(smi == static_cast<intptr_t>(Smi::FromInt(-37).ptr()));
  CHECK_EQ(INT64_C(0x0123456789abcdef), following);
  return 42;
}

int64_t TailCallScalarBeforeSmi(int* a0, int* a1, int* a2, int* a3, int* a4,
                                int* a5, int* a6, int* a7, int64_t preceding,
                                intptr_t smi) {
  CHECK(a0 == &g_values[0]);
  CHECK(a7 == &g_values[7]);
  CHECK_EQ(INT64_C(0x0123456789abcdef), preceding);
  CHECK(smi == static_cast<intptr_t>(Smi::FromInt(-37).ptr()));
  return 42;
}

int64_t TailCallScalarSpill(int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                            int64_t a4, int64_t a5, int64_t a6, int64_t a7,
                            int64_t spilled) {
  CHECK_EQ(INT64_C(0x0102030405060708), a0);
  CHECK_EQ(INT64_C(0x1112131415161718), a7);
  CHECK_EQ(INT64_C(0x0123456789abcdef), spilled);
  return 42;
}

int64_t TailCallLargeScalarArea(int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                                int64_t a4, int64_t a5, int64_t a6, int64_t a7,
                                int64_t a8, int64_t a9, int64_t a10, int64_t a11,
                                int64_t a12, int64_t a13, int64_t a14,
                                int64_t a15, int64_t a16) {
  CHECK_EQ(INT64_C(0x0102030405060708), a0);
  CHECK_EQ(INT64_C(0x1112131415161718), a7);
  CHECK_EQ(INT64_C(0x0123456789abcde0), a8);
  CHECK_EQ(INT64_C(0x0123456789abcde1), a9);
  CHECK_EQ(INT64_C(0x0123456789abcde2), a10);
  CHECK_EQ(INT64_C(0x0123456789abcde3), a11);
  CHECK_EQ(INT64_C(0x0123456789abcde4), a12);
  CHECK_EQ(INT64_C(0x0123456789abcde5), a13);
  CHECK_EQ(INT64_C(0x0123456789abcde6), a14);
  CHECK_EQ(INT64_C(0x0123456789abcde7), a15);
  CHECK_EQ(INT64_C(0x0123456789abcde8), a16);
  return 42;
}

int64_t TailCallCapabilitySpill(int* a0, int* a1, int* a2, int* a3, int* a4,
                                int* a5, int* a6, int* a7, int* spilled) {
  int* arguments[] = {a0, a1, a2, a3, a4, a5, a6, a7, spilled};
  for (int i = 0; i < 9; ++i) {
    CHECK(arguments[i] == &g_values[i]);
    CHECK_EQ(1, __builtin_cheri_tag_get(arguments[i]));
    CHECK_EQ(100 + i, *arguments[i]);
  }
  return 42;
}

void RunSmiMemoryTest(Address func_address, MachineType machine_type) {
  RawMachineAssemblerTester<int64_t> m;
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  auto smi = [&](int value) {
    if (machine_type == MachineType::Int64()) {
      return std::make_pair(
          machine_type,
          m.Int64Constant(
              static_cast<int64_t>(Smi::FromInt(value).ptr())));
    }
    return std::make_pair(
        machine_type,
        m.IntPtrConstant(
            static_cast<intptr_t>(Smi::FromInt(value).ptr())));
  };
  m.Return(m.CallCFunction(function, MachineType::Int64(), smi(0), smi(1),
                           smi(2), smi(3), smi(4), smi(5), smi(6), smi(7),
                           smi(-37), smi(9)));
  CHECK_EQ(42, m.Call());
}

// ---------------------------------------------------------------------------
// Test driver. Forwards the tester's incoming parameters to the real C++
// callee via CallCFunction and checks the int64_t result.
// ---------------------------------------------------------------------------

#define SIGNATURE_TYPES(TYPE, IDX, VALUE) MachineTypeForC<TYPE>()
#define PARAM_PAIRS(TYPE, IDX, VALUE) \
  std::make_pair(MachineTypeForC<TYPE>(), m.Parameter(IDX))
#define CALL_ARGS(TYPE, IDX, VALUE) static_cast<TYPE>(VALUE)

#define AAPCS64_CAP_TEST(NAME, SIG, CALLEE)                                  \
  TEST(NAME) {                                                               \
    RawMachineAssemblerTester<int64_t> m(SIG(SIGNATURE_TYPES));              \
    Address func_address = FUNCTION_ADDR(&CALLEE);                           \
    ApiFunction func(func_address);                                          \
    ExternalReference ref =                                                  \
        ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);    \
    Node* function = m.ExternalConstant(ref);                                \
    m.Return(m.CallCFunction(function, MachineType::Int64(),                 \
                             SIG(PARAM_PAIRS)));                             \
    CHECK_EQ(42, m.Call(SIG(CALL_ARGS)));                                    \
  }

AAPCS64_CAP_TEST(AAPCS64CapRegisterInt64, SIG_REG_I8, RegInt64)
AAPCS64_CAP_TEST(AAPCS64CapRegisterPointer, SIG_REG_P8, RegPointer)
#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
AAPCS64_CAP_TEST(AAPCS64CapRegisterDouble, SIG_REG_D8, RegDouble)
AAPCS64_CAP_TEST(AAPCS64CapRegisterInt64Double, SIG_REG_ID8, RegInt64Double)
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

AAPCS64_CAP_TEST(AAPCS64CapMemoryInt64_9, SIG_MEM_I9, MemInt64_9)
AAPCS64_CAP_TEST(AAPCS64CapMemoryInt64_10, SIG_MEM_I10, MemInt64_10)
AAPCS64_CAP_TEST(AAPCS64CapMemoryInt64_20, SIG_MEM_I20, MemInt64_20)
AAPCS64_CAP_TEST(AAPCS64CapMemoryInt32_10, SIG_MEM_I32_10, MemInt32_10)

TEST(AAPCS64CapMemoryBool_10) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&MemBool_10);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  auto i64 = [&](int64_t value) {
    return std::make_pair(MachineType::Int64(), m.Int64Constant(value));
  };
  auto boolean = [&](bool value) {
    return std::make_pair(MachineType::Bool(), m.Int32Constant(value));
  };
  m.Return(m.CallCFunction(function, MachineType::Int64(), i64(0), i64(1),
                           i64(2), i64(3), i64(4), i64(5), i64(6), i64(7),
                           boolean(true), boolean(false)));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapMemoryInt8_10) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&MemInt8_10);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  auto i64 = [&](int64_t value) {
    return std::make_pair(MachineType::Int64(), m.Int64Constant(value));
  };
  auto i8 = [&](int8_t value) {
    return std::make_pair(MachineType::Int8(), m.Int32Constant(value));
  };
  m.Return(m.CallCFunction(function, MachineType::Int64(), i64(0), i64(1),
                           i64(2), i64(3), i64(4), i64(5), i64(6), i64(7),
                           i8(-8), i8(9)));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapMemoryUint8_10) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&MemUint8_10);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  auto i64 = [&](int64_t value) {
    return std::make_pair(MachineType::Int64(), m.Int64Constant(value));
  };
  auto u8 = [&](uint8_t value) {
    return std::make_pair(MachineType::Uint8(), m.Int32Constant(value));
  };
  m.Return(m.CallCFunction(function, MachineType::Int64(), i64(0), i64(1),
                           i64(2), i64(3), i64(4), i64(5), i64(6), i64(7),
                           u8(0xf8), u8(9)));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapMemoryInt16_10) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&MemInt16_10);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  auto i64 = [&](int64_t value) {
    return std::make_pair(MachineType::Int64(), m.Int64Constant(value));
  };
  auto i16 = [&](int16_t value) {
    return std::make_pair(MachineType::Int16(), m.Int32Constant(value));
  };
  m.Return(m.CallCFunction(function, MachineType::Int64(), i64(0), i64(1),
                           i64(2), i64(3), i64(4), i64(5), i64(6), i64(7),
                           i16(-1234), i16(2345)));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapMemoryUint16_10) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&MemUint16_10);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  auto i64 = [&](int64_t value) {
    return std::make_pair(MachineType::Int64(), m.Int64Constant(value));
  };
  auto u16 = [&](uint16_t value) {
    return std::make_pair(MachineType::Uint16(), m.Int32Constant(value));
  };
  m.Return(m.CallCFunction(function, MachineType::Int64(), i64(0), i64(1),
                           i64(2), i64(3), i64(4), i64(5), i64(6), i64(7),
                           u16(0xf123), u16(0x4567)));
  CHECK_EQ(42, m.Call());
}

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
AAPCS64_CAP_TEST(AAPCS64CapMemoryDouble_10, SIG_MEM_D10, MemDouble_10)
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

AAPCS64_CAP_TEST(AAPCS64CapMemoryPointer_10, SIG_MEM_P10, MemPointer_10)
AAPCS64_CAP_TEST(AAPCS64CapMemoryPointer_12, SIG_MEM_P12, MemPointer_12)

AAPCS64_CAP_TEST(AAPCS64CapMemoryInt64ThenPointer, SIG_MEM_IP,
                 MemInt64ThenPointer)
AAPCS64_CAP_TEST(AAPCS64CapMemoryPointerThenInt64, SIG_MEM_PI,
                 MemPointerThenInt64)

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
TEST(AAPCS64CapReturnDouble) {
  RawMachineAssemblerTester<double> m(MachineType::Int64());
  Address func_address = FUNCTION_ADDR(&ReturnDouble);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  m.Return(m.CallCFunction(function, MachineType::Float64(),
                           std::make_pair(MachineType::Int64(), m.Parameter(0))));
  CHECK_EQ(3.0, m.Call(int64_t{2}));
}
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

TEST(AAPCS64CapReturnPointer) {
  RawMachineAssemblerTester<int*> m(MachineType::Int64());
  Address func_address = FUNCTION_ADDR(&ReturnPointer);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  m.Return(m.CallCFunction(function, MachineType::Pointer(),
                           std::make_pair(MachineType::Int64(), m.Parameter(0))));
  int* result = m.Call(int64_t{5});
  CHECK(result == &g_values[5]);
  CHECK_EQ(1, __builtin_cheri_tag_get(result));
  CHECK_EQ(g_values[5], *result);
}

TEST(AAPCS64CapSmiPassedAsIntPtr) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&CheckSmiPassedAsIntPtr);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);

  const intptr_t tagged_smi = static_cast<intptr_t>(Smi::FromInt(37).ptr());
  m.Return(m.CallCFunction(
      function, MachineType::Int64(),
      std::make_pair(MachineType::TaggedSigned(),
                     m.IntPtrConstant(tagged_smi))));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapSmiHighBitsPassedAsIntPtr) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&CheckSmiHighBitsPassedAsIntPtr);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);

  const intptr_t tagged_smi = static_cast<intptr_t>(Smi::FromInt(-37).ptr());
  m.Return(m.CallCFunction(
      function, MachineType::Int64(),
      std::make_pair(MachineType::TaggedSigned(),
                     m.IntPtrConstant(tagged_smi)),
      std::make_pair(MachineType::Int64(),
                     m.Int64Constant(INT64_C(0x0123456789abcdef)))));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapSmiHighBitsPassedAsInt64) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&CheckSmiHighBitsPassedAsInt64);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);

  const int64_t tagged_smi = static_cast<int64_t>(Smi::FromInt(-37).ptr());
  m.Return(m.CallCFunction(
      function, MachineType::Int64(),
      std::make_pair(MachineType::Int64(), m.Int64Constant(tagged_smi)),
      std::make_pair(MachineType::Int64(),
                     m.Int64Constant(INT64_C(0x0123456789abcdef)))));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapSmiPassedAsAddress) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&CheckSmiPassedAsAddress);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  m.Return(m.CallCFunction(
      function, MachineType::Int64(),
      std::make_pair(MachineType::AnyTagged(),
                     m.IntPtrConstant(
                         static_cast<intptr_t>(Smi::FromInt(-37).ptr()))),
      std::make_pair(MachineType::Int64(),
                     m.Int64Constant(INT64_C(0x0123456789abcdef)))));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapMemorySmiIntPtr_10) {
  RunSmiMemoryTest(FUNCTION_ADDR(&MemSmiIntPtr_10),
                   MachineType::TaggedSigned());
}

TEST(AAPCS64CapMemorySmiInt64_10) {
  RunSmiMemoryTest(FUNCTION_ADDR(&MemSmiInt64_10), MachineType::Int64());
}

TEST(AAPCS64CapMemorySmiThenInt64) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&MemSmiThenInt64);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  auto pointer = [&](int index) {
    return std::make_pair(
        MachineType::Pointer(),
        m.ExternalConstant(ExternalReference::Create(
            reinterpret_cast<Address>(&g_values[index]))));
  };
  m.Return(m.CallCFunction(
      function, MachineType::Int64(), pointer(0), pointer(1), pointer(2),
      pointer(3), pointer(4), pointer(5), pointer(6), pointer(7),
      std::make_pair(MachineType::TaggedSigned(),
                     m.IntPtrConstant(
                         static_cast<intptr_t>(Smi::FromInt(-37).ptr()))),
      std::make_pair(MachineType::Int64(),
                     m.Int64Constant(INT64_C(0x0123456789abcdef)))));
  CHECK_EQ(42, m.Call());
}

TEST(AAPCS64CapMemoryInt64ThenSmi) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&MemInt64ThenSmi);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  auto pointer = [&](int index) {
    return std::make_pair(
        MachineType::Pointer(),
        m.ExternalConstant(ExternalReference::Create(
            reinterpret_cast<Address>(&g_values[index]))));
  };
  m.Return(m.CallCFunction(
      function, MachineType::Int64(), pointer(0), pointer(1), pointer(2),
      pointer(3), pointer(4), pointer(5), pointer(6), pointer(7),
      std::make_pair(MachineType::Int64(),
                     m.Int64Constant(INT64_C(0x0123456789abcdef))),
      std::make_pair(MachineType::TaggedSigned(),
                     m.IntPtrConstant(
                         static_cast<intptr_t>(Smi::FromInt(-37).ptr())))));
  CHECK_EQ(42, m.Call());
}

Handle<Code> BuildTailCaller(Isolate* isolate, CallDescriptor* descriptor,
                             Address function_address, int smi_index = -1) {
  CodeAssemblerTester inner(isolate, descriptor, "tail-caller");
  RawMachineAssembler* raw = inner.raw_assembler_for_testing();
  ApiFunction func(function_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  const int input_count = static_cast<int>(descriptor->ParameterCount()) + 1;
  Node* inputs[18];
  inputs[0] = raw->ExternalConstant(ref);
  for (size_t i = 0; i < descriptor->ParameterCount(); ++i) {
    inputs[i + 1] = raw->Parameter(i);
  }
  if (smi_index >= 0) {
    inputs[smi_index] =
        raw->IntPtrConstant(static_cast<intptr_t>(Smi::FromInt(-37).ptr()));
  }
  raw->TailCallN(descriptor, input_count, inputs);
  return inner.GenerateCodeCloseAndEscape();
}

MachineSignature* TailCallSignature(Zone* zone, MachineType ninth,
                                    MachineType tenth) {
  MachineSignature::Builder builder(zone, 1, 10);
  builder.AddReturn(MachineType::Int64());
  for (int i = 0; i < 8; ++i) builder.AddParam(MachineType::Pointer());
  builder.AddParam(ninth);
  builder.AddParam(tenth);
  return builder.Get();
}

MachineSignature* ScalarTailCallSignature(Zone* zone, int count) {
  MachineSignature::Builder builder(zone, 1, count);
  builder.AddReturn(MachineType::Int64());
  for (int i = 0; i < count; ++i) builder.AddParam(MachineType::Int64());
  return builder.Get();
}

CSignature* TailCallNativeSignature(Zone* zone) {
  return CSignature::New(
      zone, MachineType::Int64(), MachineType::Pointer(),
      MachineType::Pointer(), MachineType::Pointer(), MachineType::Pointer(),
      MachineType::Pointer(), MachineType::Pointer(), MachineType::Pointer(),
      MachineType::Pointer(), MachineType::Pointer(), MachineType::Pointer());
}

void RunPointerTailCallTest(Address function_address, MachineType ninth_type,
                            MachineType tenth_type, intptr_t native_ninth,
                            intptr_t native_tenth, int smi_index = -1) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  CallDescriptor* descriptor = Linkage::GetSimplifiedCDescriptor(
      &zone, TailCallSignature(&zone, ninth_type, tenth_type));
  Handle<Code> tail_caller =
      BuildTailCaller(isolate, descriptor, function_address, smi_index);
  CodeRunner<int64_t> runner(isolate, tail_caller,
                             TailCallNativeSignature(&zone));
  CHECK_EQ(42, runner.Call(&g_values[0], &g_values[1], &g_values[2],
                           &g_values[3], &g_values[4], &g_values[5],
                           &g_values[6], &g_values[7], native_ninth,
                           native_tenth));
}

TEST(AAPCS64CapTailCallMixedSpills) {
  RunPointerTailCallTest(FUNCTION_ADDR(&TailCallMixedSpills),
                         MachineType::TaggedSigned(), MachineType::Int64(),
                         static_cast<intptr_t>(Smi::FromInt(-37).ptr()),
                         INT64_C(0x0123456789abcdef), 9);
}

TEST(AAPCS64CapTailCallScalarBeforeSmi) {
  RunPointerTailCallTest(FUNCTION_ADDR(&TailCallScalarBeforeSmi),
                         MachineType::Int64(), MachineType::TaggedSigned(),
                         INT64_C(0x0123456789abcdef),
                         static_cast<intptr_t>(Smi::FromInt(-37).ptr()), 10);
}

TEST(AAPCS64CapTailCallCapabilitySpill) {
  RunPointerTailCallTest(FUNCTION_ADDR(&TailCallCapabilitySpill),
                         MachineType::Pointer(), MachineType::Pointer(),
                         reinterpret_cast<intptr_t>(&g_values[8]), 0);
}

TEST(AAPCS64CapTailCallScalarSpill) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  MachineSignature* signature = ScalarTailCallSignature(&zone, 9);
  CallDescriptor* descriptor =
      Linkage::GetSimplifiedCDescriptor(&zone, signature);
  Handle<Code> tail_caller =
      BuildTailCaller(isolate, descriptor, FUNCTION_ADDR(&TailCallScalarSpill));
  CodeRunner<int64_t> runner(isolate, tail_caller, signature);
  CHECK_EQ(42, runner.Call(
      INT64_C(0x0102030405060708), INT64_C(0x0102030405060709),
      INT64_C(0x010203040506070a), INT64_C(0x010203040506070b),
      INT64_C(0x010203040506070c), INT64_C(0x010203040506070d),
      INT64_C(0x010203040506070e), INT64_C(0x1112131415161718),
      INT64_C(0x0123456789abcdef)));
}

TEST(AAPCS64CapTailCallLargeScalarArea) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  MachineSignature* signature = ScalarTailCallSignature(&zone, 17);
  CallDescriptor* descriptor =
      Linkage::GetSimplifiedCDescriptor(&zone, signature);
  Handle<Code> tail_caller = BuildTailCaller(
      isolate, descriptor, FUNCTION_ADDR(&TailCallLargeScalarArea));
  CodeRunner<int64_t> runner(isolate, tail_caller, signature);
  CHECK_EQ(42, runner.Call(
      INT64_C(0x0102030405060708), INT64_C(0x0102030405060709),
      INT64_C(0x010203040506070a), INT64_C(0x010203040506070b),
      INT64_C(0x010203040506070c), INT64_C(0x010203040506070d),
      INT64_C(0x010203040506070e), INT64_C(0x1112131415161718),
      INT64_C(0x0123456789abcde0), INT64_C(0x0123456789abcde1),
      INT64_C(0x0123456789abcde2), INT64_C(0x0123456789abcde3),
      INT64_C(0x0123456789abcde4), INT64_C(0x0123456789abcde5),
      INT64_C(0x0123456789abcde6), INT64_C(0x0123456789abcde7),
      INT64_C(0x0123456789abcde8)));
}

TEST(AAPCS64CapRegisterMixedSmiInt64) {
  RawMachineAssemblerTester<int64_t> m;
  Address func_address = FUNCTION_ADDR(&RegisterMixedSmiInt64);
  ApiFunction func(func_address);
  ExternalReference ref =
      ExternalReference::Create(&func, ExternalReference::FAST_C_CALL);
  Node* function = m.ExternalConstant(ref);
  m.Return(m.CallCFunction(
      function, MachineType::Int64(),
      std::make_pair(MachineType::TaggedSigned(),
                     m.IntPtrConstant(
                         static_cast<intptr_t>(Smi::FromInt(11).ptr()))),
      std::make_pair(MachineType::Int64(),
                     m.Int64Constant(INT64_C(0x0123456789abcdef))),
      std::make_pair(MachineType::TaggedSigned(),
                     m.IntPtrConstant(
                         static_cast<intptr_t>(Smi::FromInt(-13).ptr()))),
      std::make_pair(MachineType::Int64(),
                     m.Int64Constant(INT64_C(0x1020304050607080)))));
  CHECK_EQ(42, m.Call());
}

}  // namespace
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_CHERI && V8_TARGET_ARCH_ARM64

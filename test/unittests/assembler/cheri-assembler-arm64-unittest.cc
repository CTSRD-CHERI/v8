// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CHERI instructions emitted by the arm64 (Macro)Assembler, checked by
// execution: the disassemble-and-compare harness is compiled out under
// __CHERI_PURE_CAPABILITY__. Section refs are Arm DDI0606 A.k.

#include <stdint.h>
#include <string.h>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/codegen/arm64/macro-assembler-arm64-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"
#include "test/common/assembler-tester.h"
#include "test/unittests/assembler/test-utils-arm64.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

#if V8_TARGET_CHERI && defined(__CHERI_PURE_CAPABILITY__) && \
    !defined(USE_SIMULATOR)

#define __ masm.

#define CHERI_BUF_SIZE 8192

#define SETUP()                                                        \
  Isolate* isolate = i_isolate();                                      \
  CHECK_NOT_NULL(isolate);                                             \
  auto owned_buf = AllocateAssemblerBuffer(CHERI_BUF_SIZE);            \
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes, \
                      owned_buf->CreateView());                        \
  HandleScope handle_scope(isolate);                                   \
  Handle<Code> code;                                                   \
  RegisterDump core;

#define RESET()      \
  __ Reset();        \
  __ CodeEntry();    \
  __ Msr(NZCV, xzr); \
  __ Msr(FPCR, xzr);

#define START() \
  RESET();      \
  __ PushCalleeSavedRegisters();

#define RUN()                                               \
  {                                                         \
    auto f = GeneratedCode<void>::FromCode(isolate, *code); \
    f.Call();                                               \
  }

#define END()                                                                  \
  core.Dump(&masm);                                                            \
  __ PopCalleeSavedRegisters();                                                \
  __ Ret();                                                                    \
  {                                                                            \
    CodeDesc desc;                                                             \
    __ GetCode(masm.isolate(), &desc);                                         \
    code = Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build(); \
  }

// Reads of the dumped capability in register |c|. The dump holds real
// capabilities, so the hardware tag survives into the host process.
#define C_CAP(c) (core.creg((c).code()))
#define C_TAG(c) (V8_CHERI_TAG_GET(C_CAP(c)) ? 1u : 0u)
#define C_ADDR(c) (static_cast<uint64_t>(V8_CHERI_ADDR_GET(C_CAP(c))))
#define C_BASE(c) (static_cast<uint64_t>(V8_CHERI_BASE_GET(C_CAP(c))))
#define C_LEN(c) (static_cast<uint64_t>(V8_CHERI_LENGTH_GET(C_CAP(c))))
#define C_PERM(c) (static_cast<uint64_t>(V8_CHERI_PERMS(C_CAP(c))))
#define C_SEALED(c) (V8_CHERI_SEALED(C_CAP(c)) ? 1u : 0u)
#define C_TYPE(c) (static_cast<uint64_t>(__builtin_cheri_type_get(C_CAP(c))))
#define X64(r) (static_cast<uint64_t>(core.xreg((r).code())))

// CHERI_PERM_SEAL, from <machine/cherireg.h> on the target. A SEAL (capability)
// key must carry it, and it is not part of the userspace permission set, so no
// capability the port can derive from csp does.
static constexpr uint64_t kPermSeal = 1u << 11;

// Width of the Morello object-type field. SEAL (capability) truncates the key's
// value into it rather than range checking it.
static constexpr uint64_t kObjectTypeMask = (1u << 15) - 1u;

#define CHECK_CAP_SAME(a, b)            \
  do {                                  \
    CHECK_EQ(C_TAG(a), C_TAG(b));       \
    CHECK_EQ(C_ADDR(a), C_ADDR(b));     \
    CHECK_EQ(C_BASE(a), C_BASE(b));     \
    CHECK_EQ(C_LEN(a), C_LEN(b));       \
    CHECK_EQ(C_PERM(a), C_PERM(b));     \
    CHECK_EQ(C_SEALED(a), C_SEALED(b)); \
  } while (0)

// Compares a dumped register against a compiler-built capability: both go
// through the same hardware operations, so a mismatch is a real encoding bug.
#define CHECK_CAP_AGAINST(expected_cap, actual_reg)                       \
  do {                                                                    \
    uintptr_t expected_ = (expected_cap);                                 \
    CHECK_EQ(V8_CHERI_TAG_GET(expected_) ? 1u : 0u, C_TAG(actual_reg));   \
    CHECK_EQ(static_cast<uint64_t>(V8_CHERI_ADDR_GET(expected_)),         \
             C_ADDR(actual_reg));                                         \
    CHECK_EQ(static_cast<uint64_t>(V8_CHERI_BASE_GET(expected_)),         \
             C_BASE(actual_reg));                                         \
    CHECK_EQ(static_cast<uint64_t>(V8_CHERI_LENGTH_GET(expected_)),       \
             C_LEN(actual_reg));                                          \
    CHECK_EQ(static_cast<uint64_t>(V8_CHERI_PERMS(expected_)),            \
             C_PERM(actual_reg));                                         \
    CHECK_EQ(V8_CHERI_SEALED(expected_) ? 1u : 0u, C_SEALED(actual_reg)); \
  } while (0)

// Builds a tagged capability covering [addr, addr + len) in |cd|, deriving all
// provenance from the stack pointer capability, which has bounds covering the
// whole stack region. |xr| carries the address, |lenr| the length.
#define CHERI_MAKE_CAP(cd, xr, lenr, addr, len) \
  do {                                          \
    __ Mov((xr), static_cast<uint64_t>(addr));  \
    __ Scvalue((cd), csp, (xr));                \
    __ Mov((lenr), static_cast<uint64_t>(len)); \
    __ Scbndse((cd), (cd), (lenr));             \
    CHECK(true);                                \
  } while (0)

// |dst| = |src| with the tag cleared and the other fields intact. There is no
// CLRTAG and the X view erases the metadata, so this round-trips through
// memory. |base| must cover [off, off + 16) 16-byte aligned; |xr| is scratch.
#define CHERI_UNTAG_KEEPING_METADATA(dst, src, base, off, xr) \
  do {                                                        \
    __ Stp((src), (src), MemOperand((base), (off)));          \
    __ Mov((xr), (src).X());                                  \
    __ Str((xr), MemOperand((base), (off)));                  \
    __ Ldr((dst), MemOperand((base), (off)));                 \
  } while (0)

class CheriAssemblerArm64Test : public i::TestWithIsolate {};

// ---------------------------------------------------------------------------
// Register representation.
// ---------------------------------------------------------------------------

// A capability register is a distinct class: 128 bits, not "two 64-bit halves".
TEST_F(CheriAssemblerArm64Test, capability_register_api) {
  for (int i = 0; i < kNumberOfRegisters; i++) {
    Register c = Register::CRegFromCode(i);
    CHECK(c.IsC());
    CHECK(c.Is128Bits());
    CHECK(!c.Is64Bits());
    CHECK(!c.Is32Bits());
    CHECK_EQ(i, c.code());
    CHECK_EQ(128, c.SizeInBits());

    // The X view of a capability register is the value field only.
    Register x = c.X();
    CHECK(x.Is64Bits());
    CHECK(!x.IsC());
    CHECK_EQ(i, x.code());

    CHECK(Register::cap_from_code(i).IsC());
    CHECK_EQ(i, Register::cap_from_code(i).code());
  }

  // The zero register has a capability form; it is the null capability.
  CHECK(czr.IsC());
  CHECK(czr.Is128Bits());
  CHECK_EQ(kNumberOfRegisters - 1, czr.code());
  CHECK(!xzr.IsC());
  CHECK(xzr.Is64Bits());

  // The stack pointer has a capability form too.
  CHECK(csp.IsC());
  CHECK(csp.Is128Bits());

  // Sizes the whole port assumes.
  CHECK_EQ(16, kCRegSize);
  CHECK_EQ(16, kSystemPointerSize);
  CHECK_EQ(128, kCRegSizeInBits);
  CHECK_EQ(sizeof(uintptr_t), static_cast<size_t>(kCRegSize));

  // Assembler::AppropriateZeroRegFor makes that choice, but it is protected and
  // this fixture is not an Assembler, so the selection is covered below.
}

// ---------------------------------------------------------------------------
// CPY / MOV.
// ---------------------------------------------------------------------------

// CPY <Cd>, <Cn> (§4.4.35) copies the whole capability, tag included. This is
// the only instruction that moves a tagged pointer without disturbing it.
TEST_F(CheriAssemblerArm64Test, cpy_preserves_all_capability_fields) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ capmov(c19, c18);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c18));
  CHECK_CAP_SAME(c18, c19);
  CHECK_EQ(buf_addr, C_ADDR(c19));
  CHECK_EQ(buf_addr, C_BASE(c19));
  CHECK_EQ(sizeof(buf), C_LEN(c19));
  CHECK_EQ(buf[0], 0xAAAAAAAAAAAAAAAAUL);
}

// MOV <Cd>, <Cn> is an alias of CPY (§4.4.103); the "ORR Rd, ZR, Rm" fallback
// has no capability encoding, and the size check does not catch it.
TEST_F(CheriAssemblerArm64Test, mov_between_capability_registers_is_cpy) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(c19, c18);
  // And the same through the MacroAssembler wrapper used by the port.
  __ CapMov(c20, c18);
  END();
  RUN();

  CHECK_CAP_SAME(c18, c19);
  CHECK_CAP_SAME(c18, c20);
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(buf[0], 0xAAAAAAAAAAAAAAAAUL);
}

// ADD_c with a zero displacement stays in bounds, so the tag survives. csp
// cannot be read out of RegisterDump, so only the result is observed.
TEST_F(CheriAssemblerArm64Test, mov_from_csp_preserves_the_capability_tag) {
  SETUP();
  START();
  __ Mov(c19, csp);  // CPY c19, csp
  __ mov(c20, csp);  // ADD_c c20, csp, #0
  __ Mov(c21, c20);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(1u, C_TAG(c21));
  // The two spellings of the same move agree on the whole capability, so
  // neither truncated the value nor lost the stack's bounds and permissions.
  CHECK_CAP_SAME(c19, c20);
  CHECK_CAP_SAME(c20, c21);
}

// Writing any view narrower than 128 bits clears the tag and zeroes the
// metadata. Only a capability load, CPY or BUILD can restore it.
TEST_F(CheriAssemblerArm64Test, integer_write_clears_capability_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHECK(true);
  // Overwrite only the value half.
  __ Mov(x2, buf_addr + 16);
  __ Mov(c18.X(), x2);
  END();
  RUN();

  CHECK_EQ(0u, C_TAG(c18));
  // The value was updated ...
  CHECK_EQ(buf_addr + 16, C_ADDR(c18));
  // The metadata is *gone*: writing the X view clears the tag and zeroes the
  // upper half. A half-overwritten stack slot keeps the old bounds instead
  // (see integer_store_clears_tag_of_containing_cell).
  CHECK_EQ(0u, C_BASE(c18));
  CHECK_NE(sizeof(buf), C_LEN(c18));
  CHECK(buf[0] != 0);
}

// MacroAssembler::Mov(Register, uint64_t) forwards a capability destination to
// the X view, so an immediate address is untagged: rebuild it with SCVALUE.
TEST_F(CheriAssemblerArm64Test, mov_immediate_into_capability_is_untagged) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  __ Mov(c18, buf_addr);
  // Contrast: the same address rebuilt from the stack pointer is tagged. The
  // address must be in the *integer* register; passing the capability view as
  // the source of the value would be the mistake this test is about.
  __ Mov(x0, buf_addr);
  __ Scvalue(c19, csp, x0);
  END();
  RUN();

  CHECK_EQ(0u, C_TAG(c18));
  CHECK_EQ(buf_addr, C_ADDR(c18));
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(buf_addr, C_ADDR(c19));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Field reads and writes.
// ---------------------------------------------------------------------------

// GCTAG (§4.4.59) writes the tag as a 64-bit integer; the pseudocode's
// `1<63:0>` is widening, not bit replication, so it agrees with
// __builtin_cheri_tag_get().
TEST_F(CheriAssemblerArm64Test, gctag_reports_one_when_tagged) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(c19, buf_addr);  // Untagged: immediate into the X view.
  __ Gctag(x2, c18);
  __ Gctag(x3, c19);
  END();
  RUN();

  CHECK_EQ(1u, X64(x2));
  CHECK_EQ(0u, X64(x3));
  // The builtin agrees with the instruction, so a tag read is portable between
  // the two - unlike the sealed and length reads, which have width caveats.
  CHECK_EQ(C_TAG(c18), X64(x2));
  CHECK_EQ(C_TAG(c19), X64(x3));
  CHECK(buf[0] != 0);
}

// GC{VALUE,BASE,LEN,PERM,SEAL} (§4.4.52-61) must agree with the C builtins
// reading the same register from the dump.
TEST_F(CheriAssemblerArm64Test, gc_field_reads_match_compiler_builtins) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Gcvalue(x2, c18);
  __ Gcbase(x3, c18);
  __ Gclen(x4, c18);
  __ Gcperm(x5, c18);
  __ Gcseal(x6, c18);
  END();
  RUN();

  uintptr_t cap = C_CAP(c18);
  CHECK_EQ(static_cast<uint64_t>(V8_CHERI_ADDR_GET(cap)), X64(x2));
  CHECK_EQ(static_cast<uint64_t>(V8_CHERI_BASE_GET(cap)), X64(x3));
  CHECK_EQ(static_cast<uint64_t>(V8_CHERI_LENGTH_GET(cap)), X64(x4));
  CHECK_EQ(static_cast<uint64_t>(V8_CHERI_PERMS(cap)), X64(x5));
  // GCSEAL (§4.4.58) writes 1 for a sealed capability and 0 otherwise, like
  // GCTAG. c18 is unsealed.
  CHECK_EQ(0u, X64(x6));
  CHECK_EQ(buf_addr, X64(x2));
  CHECK_EQ(buf_addr, X64(x3));
  CHECK_EQ(sizeof(buf), X64(x4));
  CHECK(buf[0] != 0);
}

// SCVALUE <Cd>, <Cn>, <Xm> (§4.4.120) takes provenance from Cn only; the
// integer operand contributes nothing but the new value. This is the port's
// idiom for turning a computed address back into a usable pointer.
TEST_F(CheriAssemblerArm64Test, scvalue_replaces_value_and_keeps_provenance) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(x2, buf_addr + 64);
  __ Scvalue(c19, c18, x2);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(buf_addr + 64, C_ADDR(c19));
  // Bounds and permissions come from the source, not the integer operand.
  CHECK_EQ(buf_addr, C_BASE(c19));
  CHECK_EQ(sizeof(buf), C_LEN(c19));
  CHECK_EQ(C_PERM(c18), C_PERM(c19));
  CHECK_EQ(0u, C_SEALED(c19));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Bounds.
// ---------------------------------------------------------------------------

// SCBNDSE (§4.4.116) sets bounds exactly. A power-of-two length within the
// source bounds is representable, so the tag survives.
TEST_F(CheriAssemblerArm64Test, scbndse_exact_length_keeps_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(x2, 4096);
  __ Scbndse(c19, c18, x2);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(4096u, C_LEN(c19));
  CHECK_EQ(buf_addr, C_BASE(c19));
  CHECK_EQ(buf_addr, C_ADDR(c19));
  CHECK(buf[0] != 0);
}

// SCBNDSE clears the tag rather than rounding an inexact length (§4.4.116),
// so bounds set from an unchecked length fault on first use.
TEST_F(CheriAssemblerArm64Test, scbndse_inexact_length_clears_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  // A length that needs an exponent of at least one to reach 2^20, and is odd,
  // so no exponent renders it exactly.
  const uint64_t kInexact = (1 << 20) + 1;
  const uint64_t kExact = 4096;

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(x2, kExact);
  __ Scbndse(c19, c18, x2);
  __ Mov(x3, kInexact);
  __ Scbndse(c20, c18, x3);
  END();
  RUN();

  // The builtin is the oracle for both cases: it performs the same
  // representability test, so instruction and compiler must agree.
  uintptr_t exact_oracle = V8_CHERI_SET_BOUNDS_EXACT(C_CAP(c18), kExact);
  uintptr_t inexact_oracle = V8_CHERI_SET_BOUNDS_EXACT(C_CAP(c18), kInexact);
  CHECK_EQ(1u, V8_CHERI_TAG_GET(exact_oracle) ? 1u : 0u);
  CHECK_EQ(0u, V8_CHERI_TAG_GET(inexact_oracle) ? 1u : 0u);
  CHECK_CAP_AGAINST(exact_oracle, c19);
  CHECK_CAP_AGAINST(inexact_oracle, c20);
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(0u, C_TAG(c20));
  // The requested bounds are still written even when the tag is cleared, which
  // is what makes the result look plausible to everything except gctag.
  CHECK_EQ(buf_addr, C_ADDR(c20));
  CHECK(buf[0] != 0);
}

// SCBNDS (register, §4.4.115) rounds the length up and keeps the tag, so a
// bounds check against the requested length is wrong by up to one mantissa ulp.
TEST_F(CheriAssemblerArm64Test, scbnds_register_rounds_up_and_keeps_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t kLen = 5001;

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(x2, kLen);
  __ Scbnds(c19, c18, Operand(x2));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK(C_LEN(c19) >= kLen);
  CHECK_CAP_AGAINST(V8_CHERI_SET_BOUNDS(C_CAP(c18), kLen), c19);
  CHECK(buf[0] != 0);
}

// SCBNDS (immediate, §4.4.114): the imm6 field is 0..63 in both classes and
// the scaled class shifts in hardware, so a pre-shifted value asks for 16x.
TEST_F(CheriAssemblerArm64Test,
       scbnds_immediate_scaled_is_shifted_by_hardware) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ scbnds_imm_scaled(c19, c18, 4);
  __ scbnds_imm_unscaled(c20, c18, 63);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(64u, C_LEN(c19));  // 4 << 4, produced by the hardware.
  CHECK_EQ(buf_addr, C_BASE(c19));
  // The Unscaled class takes the length verbatim, so its reach is 0..63.
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(63u, C_LEN(c20));
  CHECK(buf[0] != 0);
}

// The MacroAssembler wrapper has to convert a requested length into the
// unscaled or the scaled encoding without shifting twice.
TEST_F(CheriAssemblerArm64Test, macroassembler_scbnds_small_immediate) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Scbnds(c19, c18, Operand(16));
  __ Scbnds(c20, c18, Operand(63));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(16u, C_LEN(c19));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(63u, C_LEN(c20));
  CHECK(buf[0] != 0);
}

// The predicates must agree with ScbndsImmLiteral(): unscaled 0..63, scaled
// multiples of 16 up to 1008 (§4.4.114).
TEST_F(CheriAssemblerArm64Test,
       scbnds_immediate_predicates_match_the_encoder_reach) {
  CHECK(Assembler::IsImmSBUnscaled(0));
  CHECK(Assembler::IsImmSBUnscaled(63));
  CHECK(!Assembler::IsImmSBUnscaled(64));
  CHECK(!Assembler::IsImmSBUnscaled(100));

  CHECK(Assembler::IsImmSBScaled(64));     // field 4
  CHECK(Assembler::IsImmSBScaled(1008));   // field 63, the top of the reach
  CHECK(!Assembler::IsImmSBScaled(1024));  // field 64 does not fit
  CHECK(!Assembler::IsImmSBScaled(81));    // not a multiple of 16

  for (uint64_t imm = 0; imm <= 1024; imm++) {
    CHECK_EQ(is_uint6(imm), Assembler::IsImmSBUnscaled(imm));
    CHECK_EQ(imm % 16 == 0 && is_uint6(imm >> 4),
             Assembler::IsImmSBScaled(imm));
  }
}

// ---------------------------------------------------------------------------
// BUILD and SEAL.
// ---------------------------------------------------------------------------

// BUILD <Cd>, <Cn>, <Cm> (§4.4.19) *sets* the tag when the key is tagged,
// unsealed and a superset in bounds and permissions, and never clears one. It
// is the only way to make a valid capability from untagged memory.
TEST_F(CheriAssemblerArm64Test, build_tags_subset_and_never_clears_a_tag) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));

  SETUP();
  START();
  // A tight capability inside buf, a wide one over all of buf, and a capability
  // over an unrelated object, which is not a superset of the tight one.
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, 64);
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c20, x4, x5, other_addr, sizeof(other));

  // An untagged copy of c18 that still carries its bounds and permissions.
  CHERI_UNTAG_KEEPING_METADATA(c21, c18, c19, 512, x8);
  // The wide key contains the pattern, so the tag is set.
  __ Build(c22, c21, c19);
  // The disjoint key fails the test, and the untagged data comes through
  // unchanged.
  __ Build(c23, c21, c20);
  // A *tagged* source with the same failing key also comes through unchanged:
  // the failure does not clear the tag.
  __ Build(c24, c18, c20);
  END();
  RUN();

  // Clearing the tag through memory leaves every other field, which is what
  // makes the untagged copy indistinguishable from a live pointer to anything
  // except gctag.
  CHECK_EQ(1u, C_TAG(c18));
  CHECK_EQ(0u, C_TAG(c21));
  CHECK_EQ(C_ADDR(c18), C_ADDR(c21));
  CHECK_EQ(C_BASE(c18), C_BASE(c21));
  CHECK_EQ(C_LEN(c18), C_LEN(c21));
  CHECK_EQ(C_PERM(c18), C_PERM(c21));

  // §4.4.19: the success branch is CapWithTagSet(data), so BUILD leaves every
  // field of the data operand alone and only sets the tag - which is the whole
  // point of the instruction, so the tag must not be compared here.
  CHECK_EQ(1u, C_TAG(c22));
  CHECK_EQ(C_ADDR(c21), C_ADDR(c22));
  CHECK_EQ(C_BASE(c21), C_BASE(c22));
  CHECK_EQ(C_LEN(c21), C_LEN(c22));
  CHECK_EQ(C_PERM(c21), C_PERM(c22));
  CHECK_EQ(C_SEALED(c21), C_SEALED(c22));
  CHECK_EQ(0u, C_TAG(c23));
  CHECK_CAP_SAME(c21, c23);
  // BUILD does not modify the source either; only the destination's tag
  // changed.
  CHECK_EQ(1u, C_TAG(c24));
  CHECK_CAP_SAME(c18, c24);
  CHECK(buf[0] != 0);
  CHECK(other[0] != 0);
}

// The key must be tagged and unsealed, and neither is checked when the data is
// already tagged. A sealed *data* operand is unsealed first (§4.4.19).
TEST_F(CheriAssemblerArm64Test,
       build_needs_untagged_unsealed_key_and_unseals_data) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, 64);
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c20, x4, x5, other_addr, sizeof(other));
  CHERI_UNTAG_KEEPING_METADATA(c21, c18, c19, 512, x8);

  __ Seal(c26, c19,
          Cheri::kSealFormRb);  // Sealed key: still tagged, same bounds.
  __ Build(c22, c21, c26);      // Key sealed -> no tag.
  __ Build(c23, c21, c21);      // Key untagged -> no tag.

  // Sealed data, then untagged with the sealed bit preserved in the metadata.
  __ Seal(c25, c18, Cheri::kSealFormRb);
  CHERI_UNTAG_KEEPING_METADATA(c27, c25, c19, 528, x9);
  __ Build(c28, c27, c19);  // Bounds match: unsealed and tagged.
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c26));
  CHECK_EQ(1u, C_SEALED(c26));
  // c18's bounds are inside c26's, so only the sealed-key rule applies.
  CHECK_EQ(0u, C_TAG(c22));
  CHECK_EQ(0u, C_TAG(c23));
  CHECK_EQ(C_ADDR(c18), C_ADDR(c22));

  // The sealed bit survives an untagged round trip through memory ...
  CHECK_EQ(1u, C_SEALED(c27));
  CHECK_EQ(0u, C_TAG(c27));
  // ... and BUILD both authenticates and unseals the pattern.
  CHECK_EQ(1u, C_TAG(c28));
  CHECK_EQ(0u, C_SEALED(c28));
  CHECK_CAP_SAME(c18, c28);
  CHECK(buf[0] != 0);
  CHECK(other[0] != 0);
}

// SEAL (immediate form, §4.4.122) sets the object type and the sealed bit, and
// keeps the tag only if the source was tagged and unsealed. A sealed capability
// cannot be derived from, so nothing below dereferences it.
TEST_F(CheriAssemblerArm64Test, seal_immediate_marks_capability_sealed) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Seal(c19, c18, Cheri::kSealFormRb);
  __ Gcseal(x2, c19);
  __ Mov(c20, buf_addr);  // Untagged source.
  __ Seal(c21, c20, Cheri::kSealFormLpb);
  END();
  RUN();

  CHECK_EQ(1u, C_SEALED(c19));
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(1u, X64(x2));  // GCSEAL writes 1 for a sealed capability.
  // Bounds and value survive sealing; only the sealed bit changes.
  CHECK_EQ(C_BASE(c18), C_BASE(c19));
  CHECK_EQ(C_LEN(c18), C_LEN(c19));
  CHECK_EQ(C_ADDR(c18), C_ADDR(c19));
  // Sealing an untagged capability leaves it untagged.
  CHECK_EQ(1u, C_SEALED(c21));
  CHECK_EQ(0u, C_TAG(c21));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// CHKTGD.
// ---------------------------------------------------------------------------

// CHKTGD (§4.4.30) sets PSTATE.{N,Z,C,V} to '0010' when the capability is
// tagged and '0000' when it is not, i.e. it reports the tag through the carry
// flag and nothing else.
TEST_F(CheriAssemblerArm64Test, chktgd_tagged_sets_carry_only) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Chktgd(c18);
  END();
  RUN();

  CHECK_EQ(CFlag, core.flags_nzcv());
  CHECK(buf[0] != 0);
}

TEST_F(CheriAssemblerArm64Test, chktgd_untagged_clears_all_flags) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  __ Mov(c18, buf_addr);  // Untagged.
  __ Chktgd(c18);
  END();
  RUN();

  CHECK_EQ(0u, core.flags_nzcv());
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// ALIGNU / ALIGND.
// ---------------------------------------------------------------------------

// ALIGND (§4.4.5) and ALIGNU (§4.4.6) take the immediate as an *exponent*: the
// mask is Ones(imm6), not imm6. Both only rewrite the value field, so bounds
// are unchanged and the tag survives while the source is unsealed.
TEST_F(CheriAssemblerArm64Test, alignd_alignu_immediate_is_an_exponent) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t unaligned = buf_addr + 250;
  CHECK_EQ(250u, static_cast<uint64_t>(unaligned - buf_addr));

  const uint64_t down_16 = unaligned & ~static_cast<uint64_t>(0xF);
  const uint64_t up_16 = (unaligned + 0xF) & ~static_cast<uint64_t>(0xF);
  const uint64_t down_8 = unaligned & ~static_cast<uint64_t>(0x7);
  const uint64_t up_8 = (unaligned + 0x7) & ~static_cast<uint64_t>(0x7);
  // The exponent reading must differ from a mask reading, or the test proves
  // nothing.
  CHECK_NE(down_16, down_8);
  CHECK_NE(unaligned & ~static_cast<uint64_t>(4), down_16);

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(x2, unaligned);
  __ Scvalue(c19, c18, x2);
  __ AlignD(c20, c19, Operand(4));
  __ AlignU(c21, c19, Operand(4));
  __ AlignD(c22, c19, Operand(3));
  __ AlignU(c23, c19, Operand(3));
  END();
  RUN();

  CHECK_EQ(down_16, C_ADDR(c20));
  CHECK_EQ(up_16, C_ADDR(c21));
  CHECK_EQ(down_8, C_ADDR(c22));
  CHECK_EQ(up_8, C_ADDR(c23));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(1u, C_TAG(c21));
  // Value-only operations: bounds and permissions are inherited untouched.
  CHECK_EQ(buf_addr, C_BASE(c20));
  CHECK_EQ(sizeof(buf), C_LEN(c20));
  CHECK_EQ(C_LEN(c20), C_LEN(c22));
  CHECK_EQ(C_BASE(c20), C_BASE(c22));
  // The two exponents really did give different results.
  CHECK_NE(C_ADDR(c20), C_ADDR(c22));
  CHECK_EQ(C_PERM(c19), C_PERM(c20));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Capability arithmetic.
// ---------------------------------------------------------------------------

// ADD (immediate, §4.4.2) is CapAdd of the source capability: the provenance,
// bounds and permissions are those of Cn and the result stays tagged while the
// new value is in bounds.
TEST_F(CheriAssemblerArm64Test, add_capability_immediate_keeps_tag_in_bounds) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, 64);
  __ Add(c19, c18, Operand(16));
  __ Sub(c20, c18, Operand(16));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(buf_addr + 16, C_ADDR(c19));
  CHECK_EQ(buf_addr, C_BASE(c19));
  CHECK_EQ(64u, C_LEN(c19));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(buf_addr - 16, C_ADDR(c20));
  // A negative offset from the base is out of bounds below; see the next test.
  CHECK(buf[0] != 0);
}

// CapAdd (§5.350) clears the tag only when the bounds stop being representable,
// never for in-bounds-ness, so moving outside keeps the tag.
TEST_F(CheriAssemblerArm64Test, add_capability_out_of_bounds_keeps_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, 64);
  // Above the top of a 64-byte region, but well inside the mantissa window.
  __ Add(c19, c18, Operand(4000));
  __ Add(c20, c18, Operand(64));  // Exactly the first address past the top.
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(buf_addr + 4000, C_ADDR(c19));
  // Bounds and permissions are untouched: the capability is "valid", merely
  // outside itself.
  CHECK_EQ(buf_addr, C_BASE(c19));
  CHECK_EQ(64u, C_LEN(c19));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(buf_addr + 64, C_ADDR(c20));
  CHECK(V8_CHERI_INBOUNDS(C_CAP(c18), buf_addr + 32));
  CHECK(!V8_CHERI_INBOUNDS(C_CAP(c18), buf_addr + 4000));
  // The source is untouched.
  CHECK_EQ(1u, C_TAG(c18));
  CHECK(buf[0] != 0);
}

// With a 16-bit mantissa (§5.297-5.301) a displacement of 2^16 or more makes
// the bounds inexpressible, which clears the tag and re-bases the bounds onto
// the value - so the untagged result describes a *different* region.
TEST_F(CheriAssemblerArm64Test, add_beyond_the_bounds_window_clears_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, 64);
  __ Mov(x2, static_cast<uint64_t>(1) << 15);  // Just inside the window.
  __ Add(c19, c18, Operand(x2));
  __ Mov(x3, static_cast<uint64_t>(1) << 17);  // Outside it.
  __ Add(c20, c18, Operand(x3));
  END();
  RUN();

  // Representable: value moves, every other field is untouched - including the
  // bounds, so the new value is outside the region. ADD_c tests
  // representability, never bounds.
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(buf_addr + (static_cast<uint64_t>(1) << 15), C_ADDR(c19));
  CHECK_EQ(buf_addr, C_BASE(c19));
  CHECK_EQ(64u, C_LEN(c19));
  CHECK(!V8_CHERI_INBOUNDS(C_CAP(c19),
                           buf_addr + (static_cast<uint64_t>(1) << 15)));

  // Inexpressible: the tag goes, and the bounds are re-expressed from the new
  // value rather than kept.
  CHECK_EQ(0u, C_TAG(c20));
  CHECK_EQ(buf_addr + (static_cast<uint64_t>(1) << 17), C_ADDR(c20));
  CHECK_EQ(buf_addr + (static_cast<uint64_t>(1) << 17), C_BASE(c20));
  CHECK_EQ(64u, C_LEN(c20));
  CHECK(!V8_CHERI_INBOUNDS(C_CAP(c20), buf_addr));
  CHECK_EQ(1u, C_TAG(c18));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Comparison.
// ---------------------------------------------------------------------------

// Note on the spelling below: SUBS_c has no shift or extend field, so
// MacroAssembler::Subsc accepts only a plain register operand. Operand(cr)
// counts as plain because the single-register constructor spells it LSL #0.

// SUBS <Xd>, <Cn>, <Cm> (§4.4.156): when the tags differ the result is the
// difference of the *tags* -- tagged-untagged is 1, untagged-tagged is 3.
TEST_F(CheriAssemblerArm64Test, subsc_with_equal_tags_subtracts_values) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(x2, buf_addr + 64);
  __ Scvalue(c19, c18, x2);
  __ Subsc(x3, c18, Operand(c19));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(static_cast<uint64_t>(0 - 64), X64(x3));
  CHECK_NE(0u, core.flags_nzcv() & NFlag);
  CHECK_EQ(0u, core.flags_nzcv() & ZFlag);
  CHECK(buf[0] != 0);
}

TEST_F(CheriAssemblerArm64Test, subsc_with_differing_tags_subtracts_tags) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(c19, buf_addr);  // Same address, untagged.
  __ Subsc(x2, c18, Operand(c19));
  END();
  RUN();

  CHECK_EQ(buf_addr, C_ADDR(c19));
  CHECK_EQ(0u, C_TAG(c19));
  // Tagged first: '01' - '00' computed in two bits with a carry-in.
  CHECK_EQ(1u, X64(x2));
  CHECK_EQ(0u, core.flags_nzcv() & ZFlag);
  CHECK(buf[0] != 0);
}

TEST_F(CheriAssemblerArm64Test, subsc_reversed_tags_gives_three) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(c19, buf_addr);  // Same address, untagged.
  __ Subsc(x2, c19, Operand(c18));
  END();
  RUN();

  // Untagged first: '00' - '01' in two bits is '11'.
  CHECK_EQ(3u, X64(x2));
  CHECK_EQ(0u, core.flags_nzcv() & ZFlag);
  CHECK(buf[0] != 0);
}

// The port's Cmp() downgrades both operands to their X views before SUBS, so it
// compares addresses only and reports a tagged pointer as equal to an untagged
// one at the same address. Cmpc() is the tag-sensitive form.
TEST_F(CheriAssemblerArm64Test, cmp_on_capability_registers_is_address_only) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(c19, buf_addr);  // Untagged, same address.
  __ Cmp(c18, Operand(c19));
  END();
  RUN();

  CHECK_EQ(0u, C_TAG(c19));
  CHECK_EQ(1u, C_TAG(c18));
  CHECK_NE(0u,
           core.flags_nzcv() & ZFlag);  // "Equal", although not the same cap.
  CHECK(buf[0] != 0);
}

TEST_F(CheriAssemblerArm64Test, cmpc_is_tag_sensitive) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(c19, buf_addr);  // Untagged, same address.
  __ Cmpc(c18, Operand(c19));
  END();
  RUN();

  CHECK_EQ(0u, core.flags_nzcv() & ZFlag);  // Correctly "not equal".
  CHECK(buf[0] != 0);
}

// The non-LSE capability CAS spelled its guard as Cmp(cn, Operand(cm, UXTX)),
// which rejects a 128-bit register outright (assembler-arm64-inl.h:349): it
// only existed with DCHECKs off, as an X-view address compare.
TEST_F(CheriAssemblerArm64Test,
       cmp_with_extended_x_operand_compares_addresses_only) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Mov(c19, buf_addr);  // Untagged, same address.
  __ Mov(x20, 0);
  __ Cmp(x20, 1);  // Leaves Z clear, so the next compare must write it.
  __ Cmp(c18, Operand(c19.X(), UXTX));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c18));
  CHECK_EQ(0u, C_TAG(c19));
  CHECK_NE(0u, core.flags_nzcv() &
                   ZFlag);  // "Equal", although not the same capability.
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Memory.
// ---------------------------------------------------------------------------

// A capability load takes the tag from the memory cell; a capability store
// writes it. STP/LDP of two capabilities (§4.4) are the only pair forms that
// carry tags at all.
TEST_F(CheriAssemblerArm64Test, stp_ldp_capability_pair_round_trip_tags) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, 64);
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 128, 32);
  __ Stp(c18, c19, MemOperand(c18, 0));
  __ Ldp(c20, c21, MemOperand(c18, 0));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_CAP_SAME(c18, c20);
  CHECK_CAP_SAME(c19, c21);
  CHECK_EQ(buf_addr, C_BASE(c20));
  CHECK_EQ(64u, C_LEN(c20));
  CHECK_EQ(buf_addr + 128, C_BASE(c21));
  CHECK_EQ(32u, C_LEN(c21));
  CHECK(buf[0] != 0);
}

// An integer store to any byte of a 16-byte cell clears the cell's tag but
// keeps the untouched metadata: every field check passes except gctag.
TEST_F(CheriAssemblerArm64Test, integer_store_clears_tag_of_containing_cell) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 128, 32);
  __ Stp(c19, c19, MemOperand(c18, 64));
  __ Ldr(c20, MemOperand(c18, 64));
  __ Mov(x4, buf_addr + 0x1234);
  __ Str(x4, MemOperand(c18, 64));  // Only the low half, and not a cap store.
  __ Ldr(c21, MemOperand(c18, 64));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(buf_addr + 128, C_ADDR(c20));
  CHECK_EQ(0u, C_TAG(c21));
  CHECK_EQ(buf_addr + 0x1234, C_ADDR(c21));
  // The stale upper half still carries c19's bounds, which is what makes the
  // untagged value look legitimate.
  CHECK_EQ(buf_addr + 128, C_BASE(c21));
  CHECK_EQ(32u, C_LEN(c21));
  CHECK(buf[0] != 0);
}

// The integer pair form yields the value field then the *metadata*, not the
// neighbouring capability, so Ldp of two Xs cannot restore two tagged values.
TEST_F(CheriAssemblerArm64Test,
       ldp_of_capability_cell_yields_value_and_metadata) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 128, 32);
  CHERI_MAKE_CAP(c20, x4, x5, buf_addr + 256, 32);
  __ Stp(c19, c20, MemOperand(c18, 64));
  __ Ldp(x6, x7, MemOperand(c18, 64));
  __ Ldr(c21, MemOperand(c18, 64));
  __ Ldr(c22, MemOperand(c18, 80));
  END();
  RUN();

  // x6 is c19's value ...
  CHECK_EQ(buf_addr + 128, X64(x6));
  // ... and x7 is its metadata, which is neither address.
  CHECK_NE(buf_addr + 128, X64(x7));
  CHECK_NE(buf_addr + 256, X64(x7));
  // The capability loads see both capabilities, tagged.
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_EQ(1u, C_TAG(c22));
  CHECK_EQ(buf_addr + 128, C_ADDR(c21));
  CHECK_EQ(buf_addr + 256, C_ADDR(c22));
  CHECK(buf[0] != 0);
}

// LDR/STR (register offset, capability base, §4.4.80 and §4.4.137) take a
// general-purpose index, so the tag comes from the base; the UXTW fast path
// must not intercept into the integer encoding, which cannot move a tag.
TEST_F(CheriAssemblerArm64Test, capability_uxtw_index_keeps_the_base_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 128, 32);
  __ Mov(x4, 64);  // A byte displacement, not a cell count.
  __ Mov(x5, 4);   // The same cell, as a count of 16-byte cells.
  __ Str(c19, MemOperand(c18, x4, UXTW, 0));
  __ Ldr(c20, MemOperand(c18, x4, UXTW, 0));
  __ Ldr(c21, MemOperand(c18, 64));
  __ Ldr(c22, MemOperand(c18, x5, LSL, 4));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c20));
  CHECK_CAP_SAME(c19, c20);
  // Both lowerings address the same cell.
  CHECK_CAP_SAME(c20, c21);
  // The scaled index reaches it too.
  CHECK_CAP_SAME(c20, c22);
  // Written through memory as a capability, so the tag survived the store.
  const uintptr_t* cells = reinterpret_cast<const uintptr_t*>(buf);
  CHECK(V8_CHERI_TAG_GET(cells[4]));
  CHECK_EQ(buf_addr + 128, static_cast<uint64_t>(V8_CHERI_ADDR_GET(cells[4])));
}

// ---------------------------------------------------------------------------
// Conditional selection.
// ---------------------------------------------------------------------------

// CSEL <Cd>, <Cn>, <Cm>, <cond> (§4.4.39) copies the selected capability in
// full, tag included - unlike the integer CSEL, which cannot move a tag.
TEST_F(CheriAssemblerArm64Test, cselc_selects_whole_capability) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, 64);
  CHERI_MAKE_CAP(c19, x2, x3, other_addr, sizeof(other));
  __ Mov(x4, 0);
  __ Mov(x5, 0);
  __ Cmp(x4, x5);  // Z set: equal.
  __ cselc(c20, c18, c19, eq);
  __ cselc(c21, c18, c19, ne);
  END();
  RUN();

  CHECK_CAP_SAME(c18, c20);
  CHECK_CAP_SAME(c19, c21);
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(1u, C_TAG(c21));
  CHECK(buf[0] != 0);
  CHECK(other[0] != 0);
}

// CzeroC() is cselc against the capability zero register. The null capability
// keeps no tag, so this is a portable way to invalidate a pointer without
// clobbering the flags.
TEST_F(CheriAssemblerArm64Test, czero_c_clears_tag_and_value) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 64, sizeof(buf) - 64);
  __ Mov(x4, 0);
  __ Mov(x5, 0);
  __ Cmp(x4, x5);
  __ CzeroC(c18, eq);  // Condition holds: zeroed.
  __ CzeroC(c19, ne);  // Condition fails: left alone.
  END();
  RUN();

  CHECK_EQ(0u, C_TAG(c18));
  CHECK_EQ(0u, C_ADDR(c18));
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(buf_addr + 64, C_ADDR(c19));
  CHECK(buf[0] != 0);
}

// The same selection through MacroAssembler::Csel, which is the spelling used
// by Builtins_SpreadCall to pick between a loaded element and the undefined
// root. The immediate-0 case is the one that has a zero-register shortcut.
TEST_F(CheriAssemblerArm64Test, csel_macro_selects_whole_capabilities) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 64, sizeof(buf) - 64);
  __ Mov(x4, 5);
  __ Mov(x5, 5);
  __ Cmp(x4, x5);  // eq holds, ne fails.
  __ Csel(c20, c18, Operand(c19), eq);
  __ Csel(c21, c18, Operand(c19), ne);
  __ Csel(c22, c18, Operand(0), ne);
  END();
  RUN();

  CHECK_CAP_SAME(c18, c20);
  CHECK_CAP_SAME(c19, c21);
  CHECK_EQ(0u, C_TAG(c22));
  CHECK_EQ(0u, C_ADDR(c22));
  CHECK_EQ(1u, C_TAG(c18));
  CHECK_EQ(buf_addr, C_ADDR(c18));
}

// ---------------------------------------------------------------------------
// C64 / sentry handling.
// ---------------------------------------------------------------------------

// PrepareC64Jump() sets bit 0 without disturbing the tag: ORR on a C register
// has no capability encoding, so it must go through an X register and SCVALUE.
TEST_F(CheriAssemblerArm64Test, prepare_c64_jump_sets_bit_zero_and_keeps_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ PrepareC64Jump(c18);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c18));
  CHECK_EQ(1u, C_ADDR(c18) & 1);
  CHECK_EQ(buf_addr + 1, C_ADDR(c18));
  CHECK_EQ(buf_addr, C_BASE(c18));
  CHECK_EQ(sizeof(buf), C_LEN(c18));
  CHECK_EQ(0u, C_SEALED(c18));
  CHECK(buf[0] != 0);
}

TEST_F(CheriAssemblerArm64Test, prepare_c64_jump_is_idempotent) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ PrepareC64Jump(c18);
  __ PrepareC64Jump(c18);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c18));
  CHECK_EQ(buf_addr + 1, C_ADDR(c18));
  CHECK(buf[0] != 0);
}

// CheriSentryAdd() adds an offset and forces the C64 bit. For an unsealed
// source the result is not a hardware sentry, so the branch relies on C64.
TEST_F(CheriAssemblerArm64Test, cheri_sentry_add_forces_c64_bit) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ CheriSentryAdd(c19, c18, Operand(16));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(buf_addr + 16, C_ADDR(c18));
  CHECK_EQ(C_ADDR(c18) + 1, C_ADDR(c19));
  CHECK_EQ(1u, C_ADDR(c19) & 1);
  CHECK_EQ(buf_addr, C_BASE(c19));
  CHECK_EQ(sizeof(buf), C_LEN(c19));
  CHECK_EQ(0u, C_SEALED(c19));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Capability atomics: encodings.
// ---------------------------------------------------------------------------
//
// Ordering is not observable single-threaded and there is no decoder under
// purecap, so these are checked at the encoder.
//
// Golden words come from the §4.4 diagrams, not constants-arm64.h: a copied
// wrong constant would agree with the bug it catches. c5 -> Cs/Rs (16-20),
// c12 -> Cn (5-9), c21 -> Ct (0-4).

#define SETUP_ENCODER()                                               \
  Isolate* isolate = i_isolate();                                     \
  CHECK_NOT_NULL(isolate);                                            \
  auto enc_buf = AllocateAssemblerBuffer(CHERI_BUF_SIZE);             \
  MacroAssembler enc(isolate, v8::internal::CodeObjectRequired::kYes, \
                     enc_buf->CreateView());                          \
  HandleScope enc_handle_scope(isolate);                              \
  CpuFeatureScope enc_lse_scope(&enc, LSE, CpuFeatureScope::kDontCheckSupported)

// The capability LSE instructions (SWP, CAS) DCHECK that the feature is
// enabled, while LDAR/LDAXR/STLR/STLXR and the exclusive forms do not.
#define SETUP_LSE()                                       \
  const bool can_run_lse = CpuFeatures::IsSupported(LSE); \
  USE(can_run_lse);                                       \
  CpuFeatureScope masm_lse_scope(&masm, LSE,              \
                                 CpuFeatureScope::kDontCheckSupported)

// Emits exactly one instruction into a reset buffer and reads its 32-bit
// encoding back. The instruction-size check is what makes the read meaningful:
// it proves nothing else was emitted before the word under test.
#define ENCODE(var, STMT)                                         \
  do {                                                            \
    enc.Reset();                                                  \
    CHECK_EQ(0, enc.pc_offset());                                 \
    STMT;                                                         \
    CHECK_EQ(kInstrSize, enc.pc_offset());                        \
    var = *reinterpret_cast<const uint32_t*>(enc.buffer_start()); \
  } while (0)

// A buffer and assembler large enough that a label bound at the top of the code
// is out of reach of both TB[N]Z (+-32 KiB) and CB[N]Z (+-1 MiB), so the branch
// macros take their extra-instructions path.
#define CHERI_FAR_BUF_SIZE (2 * 1024 * 1024)
#define CHERI_FAR_WALK (300000)

#define SETUP_FAR_ENCODER()                                               \
  Isolate* far_isolate = i_isolate();                                     \
  CHECK_NOT_NULL(far_isolate);                                            \
  auto far_buf = AllocateAssemblerBuffer(CHERI_FAR_BUF_SIZE);             \
  MacroAssembler far(far_isolate, v8::internal::CodeObjectRequired::kYes, \
                     far_buf->CreateView());                              \
  HandleScope far_handle_scope(far_isolate)

// Assembler::mov() encodes any SP-operand move as "add immediate #0" (§4.4.2),
// i.e. ADD_c on CHERI, with CSP legal in both operand slots. The wasm tail-call
// setup depends on it: ADD_c needs no scratch register, unlike Mov to SP.
TEST_F(CheriAssemblerArm64Test, mov_with_csp_lowers_to_add_immediate) {
  SETUP_ENCODER();
  uint32_t to_sp = 0;
  uint32_t from_sp = 0;
  uint32_t add_to_sp = 0;
  uint32_t add_from_sp = 0;

  ENCODE(to_sp, enc.mov(csp, c16));
  ENCODE(from_sp, enc.mov(c16, csp));
  ENCODE(add_to_sp, enc.add(csp, c16, 0));
  ENCODE(add_from_sp, enc.add(c16, csp, 0));

  // §4.4.2: 0x02000000 | (imm12 << 10) | (Cn << 5) | Cd. Taken from the
  // architecture diagram, not from constants-arm64.h.
  CHECK_EQ(0x0200021Fu, to_sp);
  CHECK_EQ(0x020003F0u, from_sp);
  // csp contributes 0b1111 whichever side it is on, and the two directions are
  // distinct instructions rather than both collapsing to the same field.
  CHECK_NE(to_sp, from_sp);
  // The move is exactly the zero-displacement add, in both directions.
  CHECK_EQ(to_sp, add_to_sp);
  CHECK_EQ(from_sp, add_from_sp);
}

// The UXTW register-offset fast path must leave a capability access to
// Assembler::LoadStore: the integer encoding cannot carry a tag, and folding
// the index into the base would emit an ADD_c first.
TEST_F(CheriAssemblerArm64Test,
       capability_uxtw_index_is_one_capability_instruction) {
  SETUP_ENCODER();
  uint32_t cap_load = 0;
  uint32_t cap_store = 0;
  uint32_t integer_load = 0;

  ENCODE(cap_load, enc.Ldr(c20, MemOperand(c18, x4, UXTW, 0)));
  ENCODE(cap_store, enc.Str(c20, MemOperand(c18, x4, UXTW, 0)));
  ENCODE(integer_load, enc.Ldr(x20, MemOperand(x18, x4, UXTW, 0)));

  // Capability load and store are distinct, and neither is the integer word
  // with the register numbers reinterpreted.
  CHECK_NE(cap_load, cap_store);
  CHECK_NE(cap_load, integer_load);
}

// SWP (§4.4.157) and SWPA (§4.4.158) differ only in acquire bit 23, so
// swapping them is silent. Guards the Swp_C/Swpa_C lowering.
TEST_F(CheriAssemblerArm64Test,
       capability_swap_encodings_match_the_architecture) {
  SETUP_ENCODER();
  uint32_t relaxed = 0;
  uint32_t acquire = 0;
  uint32_t macro_relaxed = 0;
  uint32_t macro_acquire = 0;

  ENCODE(relaxed, enc.swp_c(c5, c21, MemOperand(c12)));
  ENCODE(acquire, enc.swpa_c(c5, c21, MemOperand(c12)));
  ENCODE(macro_relaxed, enc.Swp_C(c5, c21, MemOperand(c12)));
  ENCODE(macro_acquire, enc.Swpa_C(c5, c21, MemOperand(c12)));

  // Derived from §4.4.157: 0xA2208000 | (c5 << 16) | (c12 << 5) | c21.
  CHECK_EQ(0xA2258195u, relaxed);
  // ... and §4.4.158, which sets only the acquire bit.
  CHECK_EQ(0xA2A58195u, acquire);
  CHECK_NE(relaxed, acquire);
  CHECK_EQ(1u << 23, relaxed ^ acquire);

  // The MacroAssembler wrappers must reach the same encodings, in the same
  // order: Swp_C is the relaxed swap and Swpa_C the acquire one.
  CHECK_EQ(relaxed, macro_relaxed);
  CHECK_EQ(acquire, macro_acquire);
}

// CAS (cap, §4.4.21-24) uses two non-adjacent order bits: acquire is 22 and
// release is 15, so the four wrappers must produce four distinct words.
TEST_F(CheriAssemblerArm64Test,
       capability_compare_and_swap_encodings_match_the_architecture) {
  SETUP_ENCODER();
  uint32_t cas = 0;
  uint32_t casa = 0;
  uint32_t casl = 0;
  uint32_t casal = 0;

  ENCODE(cas, enc.cas_c(c5, c21, MemOperand(c12)));
  ENCODE(casa, enc.casa_c(c5, c21, MemOperand(c12)));
  ENCODE(casl, enc.casl_c(c5, c21, MemOperand(c12)));
  ENCODE(casal, enc.casal_c(c5, c21, MemOperand(c12)));

  CHECK_EQ(0xA2A57D95u, cas);
  CHECK_EQ(0xA2E57D95u, casa);
  CHECK_EQ(0xA2A5FD95u, casl);
  CHECK_EQ(0xA2E5FD95u, casal);

  // Only the two order bits move, and they are independent.
  CHECK_EQ(1u << 22, cas ^ casa);
  CHECK_EQ(1u << 15, cas ^ casl);
  CHECK_EQ((1u << 22) | (1u << 15), cas ^ casal);
  CHECK_NE(casa, casl);
}

// LDAR/STLR (cap, §4.4.64 and §4.4.125): the unused Cs and Ct2 fields must both
// be all-ones. Ldar_C with csp also pins that SP encodes as 31 in Cn.
TEST_F(CheriAssemblerArm64Test,
       capability_acquire_release_encodings_match_the_architecture) {
  SETUP_ENCODER();
  uint32_t ldar = 0;
  uint32_t stlr = 0;
  uint32_t ldar_from_sp = 0;

  ENCODE(ldar, enc.ldar_c(c21, c12));
  ENCODE(stlr, enc.stlr_c(c21, c12));
  ENCODE(ldar_from_sp, enc.Ldar_C(c21, csp));

  // §4.4.64: 0x425FFC00 | (c12 << 5) | c21, with Cs = Ct2 = 0b11111.
  CHECK_EQ(0x425FFD95u, ldar);
  // §4.4.125 differs from LDAR only in the direction bit (22).
  CHECK_EQ(0x421FFD95u, stlr);
  CHECK_EQ(1u << 22, ldar ^ stlr);

  // csp contributes 0b11111 to the Cn field.
  CHECK_EQ(0x425FFFF5u, ldar_from_sp);
}

// LDAXR (cap, §4.4.68) and STLXR (cap, §4.4.129). STLXR keeps its status
// operand a *32-bit integer* Rs while the data operand is a full capability.
TEST_F(CheriAssemblerArm64Test,
       capability_exclusive_encodings_match_the_architecture) {
  SETUP_ENCODER();
  uint32_t ldaxr = 0;
  uint32_t stlxr = 0;

  ENCODE(ldaxr, enc.ldaxr_c(c21, c12));
  ENCODE(stlxr, enc.stlxr_c(w5, c21, c12));

  CHECK_EQ(0x225FFD95u, ldaxr);
  // §4.4.129: 0x2200FC00 | (w5 << 16) | (c12 << 5) | c21.
  CHECK_EQ(0x2205FD95u, stlxr);
  // Both sit in the same exclusive group, name the same base and the same data
  // register, and differ only in the Rs field and the direction bit.
  CHECK_EQ(ldaxr & 0xFF800000u,
           stlxr & 0xFF800000u);             // Same instruction group.
  CHECK_EQ(ldaxr & 0x1E0u, stlxr & 0x1E0u);  // Cn: bits 5-9.
  CHECK_EQ(ldaxr & 0x1Fu, stlxr & 0x1Fu);    // Ct: bits 0-4.
  CHECK_EQ(31u, (ldaxr >> 16) & 0x1Fu);      // LDAXR leaves Rs all-ones ...
  CHECK_EQ(5u, (stlxr >> 16) & 0x1Fu);       // ... and STLXR puts w5 in it.
  CHECK_EQ(1u << 22, (ldaxr ^ stlxr) & (1u << 22));
}

// ---------------------------------------------------------------------------
// Capability atomics: execution.
// ---------------------------------------------------------------------------

// The access operand must be a capability whose value is 16-byte aligned, or
// the tagged store raises an alignment fault rather than writing the tag.
TEST_F(CheriAssemblerArm64Test, stlr_c_ldar_c_round_trip_preserves_the_tag) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uintptr_t cell[2];
  memset(buf, 0xAA, sizeof(buf));
  memset(cell, 0, sizeof(cell));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t cell_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cell));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, cell_addr, sizeof(cell));
  __ Stlr_C(c18, c19);
  __ Ldar_C(c20, c19);
  END();
  RUN();

  // The cell holds a tagged capability, not an untagged bit pattern.
  CHECK(V8_CHERI_TAG_GET(cell[0]));
  CHECK_EQ(buf_addr, static_cast<uint64_t>(V8_CHERI_ADDR_GET(cell[0])));

  // And loading it back gives the whole capability, bounds included.
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_CAP_SAME(c18, c20);
  CHECK(buf[0] != 0);
}

// LDAXR/STLXR (cap) is the sequence the port uses to build atomics on parts
// without LSE. STLXR's status is 0 on success, and the capability it writes
// keeps its tag.
TEST_F(CheriAssemblerArm64Test,
       ldaxr_c_stlxr_c_exclusive_pair_moves_a_tagged_capability) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  alignas(16) uintptr_t cell[2];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  memset(cell, 0, sizeof(cell));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));
  const uint64_t cell_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cell));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c22, x4, x5, other_addr, sizeof(other));
  CHERI_MAKE_CAP(c19, x2, x3, cell_addr, sizeof(cell));
  __ Stlr_C(c18, c19);
  __ Ldaxr_C(c20, c19);
  __ Stlxr_C(w1, c22, c19);
  __ Ldar_C(c21, c19);
  END();
  RUN();

  CHECK_EQ(0u, X64(w1));  // Exclusive store succeeded.
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_CAP_SAME(c18, c20);  // The exclusive load saw the stored capability.
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_CAP_SAME(c22, c21);  // ... and the exclusive store replaced it.
  CHECK_EQ(other_addr, C_ADDR(c21));
  CHECK(buf[0] != 0);
}

// SWP (cap, §4.4.157) writes C[s] to memory and returns the previous value in
// C[t] - so the first operand is the new value and the second is the output.
// kArm64CapabilityAtomicExchange relies on exactly that ordering.
TEST_F(CheriAssemblerArm64Test,
       swp_c_stores_the_first_operand_and_returns_the_other) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  alignas(16) uintptr_t cell[2];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));
  const uint64_t cell_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cell));

  // Seed the cell from the host side so the swap returns a known capability.
  // Only address and tag are checked; the compiler's bounds for |other| need
  // not match the assembler's.
  cell[0] = reinterpret_cast<uintptr_t>(other);

  SETUP();
  SETUP_LSE();
  START();
  CHECK(
      can_run_lse);  // Morello implements LSE; a part without it is a port bug.
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, cell_addr, sizeof(cell));
  __ Swp_C(c18, c20, MemOperand(c19));
  END();
  RUN();

  // The cell now holds the register that was passed first.
  CHECK(V8_CHERI_TAG_GET(cell[0]));
  CHECK_EQ(buf_addr, static_cast<uint64_t>(V8_CHERI_ADDR_GET(cell[0])));
  // The second operand received what was in the cell.
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(other_addr, C_ADDR(c20));
  CHECK(buf[0] != 0);
  CHECK(other[0] != 0);
}

// CAS (cap, §4.4.21) compares against C[s] and always writes the previous
// contents back to C[s], so the output can double as the comparison operand.
TEST_F(CheriAssemblerArm64Test,
       casal_c_compares_the_first_operand_and_leaves_the_old_value_in_it) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  alignas(16) uint64_t third[8];
  alignas(16) uintptr_t cells[8];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  memset(third, 0xCC, sizeof(third));
  memset(cells, 0, sizeof(cells));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));
  const uint64_t third_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(third));
  const uint64_t cell0_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&cells[0]));
  const uint64_t cell2_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&cells[2]));

  SETUP();
  SETUP_LSE();
  START();
  CHECK(
      can_run_lse);  // Morello implements LSE; a part without it is a port bug.
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));  // match-site contents
  CHERI_MAKE_CAP(c19, x2, x3, cell0_addr, 32);         // match site
  CHERI_MAKE_CAP(c20, x4, x5, cell2_addr, 32);         // mismatch site
  CHERI_MAKE_CAP(c21, x6, x7, other_addr, sizeof(other));  // new value
  CHERI_MAKE_CAP(c22, x8, x9, third_addr, sizeof(third));  // wrong expectation
  // Seed both sites with capability-store, so the compared values are built by
  // the same primitives on both sides rather than by the host compiler, whose
  // metadata for the same object need not be bit-identical to a derived one.
  __ Stlr_C(c18, c19);
  __ Stlr_C(c21, c20);
  // Match: c23 is a CPY of what the cell holds, so the comparison is exact.
  __ Mov(c23, c18);
  __ Casal_C(c23, c21, MemOperand(c19));
  // Mismatch: the cell holds |other|, the key is a capability to |third|.
  __ Casal_C(c22, c21, MemOperand(c20));
  END();
  RUN();

  // Matching case: memory took the new value, and the compare register still
  // reports the previous contents.
  CHECK(V8_CHERI_TAG_GET(cells[0]));
  CHECK_EQ(other_addr, static_cast<uint64_t>(V8_CHERI_ADDR_GET(cells[0])));
  CHECK_EQ(1u, C_TAG(c23));
  CHECK_EQ(buf_addr, C_ADDR(c23));

  // Non-matching case: memory is untouched and the compare register was
  // overwritten with what was actually there.
  CHECK(V8_CHERI_TAG_GET(cells[2]));
  CHECK_EQ(other_addr, static_cast<uint64_t>(V8_CHERI_ADDR_GET(cells[2])));
  CHECK_EQ(1u, C_TAG(c22));
  CHECK_EQ(other_addr, C_ADDR(c22));
  CHECK(buf[0] != 0);
  CHECK(other[0] != 0);
  CHECK(third[0] != 0);
}

// ---------------------------------------------------------------------------
// SEAL (capability key).
// ---------------------------------------------------------------------------

// SEAL (capability, §4.4.121) takes the object type from the key's *value* and
// clears the tag if the key lacks CHERI_PERM_SEAL -- which every port-derivable
// key does. Rejection still writes type, bounds and permissions.
TEST_F(CheriAssemblerArm64Test,
       seal_with_a_key_writes_value_and_clears_tag_without_seal_permission) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));  // data
  // A key meeting every §4.4.121 precondition except the permission. Its value
  // must stay inside the region it came from, or SCVALUE invalidates it first.
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 2 * kCRegSize, 4 * kCRegSize);
  __ Gcseal(x4, c18);
  __ Seal(c20, c18, c19);
  __ Gcseal(x5, c20);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(0u, C_SEALED(c19));
  CHECK(C_ADDR(c19) >= C_BASE(c19));
  CHECK(C_ADDR(c19) < C_BASE(c19) + C_LEN(c19));
  CHECK_EQ(0u, C_PERM(c19) & kPermSeal);  // the one thing missing

  CHECK_EQ(0u, X64(x4));  // the data started unsealed ...
  CHECK_EQ(1u, X64(x5));  // ... and the result reports an object type ...
  CHECK_EQ(1u, C_SEALED(c20));
  CHECK_EQ(0u, C_TAG(c20));  // ... but is not valid.
  // The object type is the low bits of the key's value: the field is truncated,
  // never range checked.
  CHECK_EQ(C_ADDR(c19) & kObjectTypeMask, C_TYPE(c20));
  // Everything else comes from the data operand, untouched.
  CHECK_EQ(C_ADDR(c18), C_ADDR(c20));
  CHECK_EQ(C_BASE(c18), C_BASE(c20));
  CHECK_EQ(C_LEN(c18), C_LEN(c20));
  CHECK_EQ(C_PERM(c18), C_PERM(c20));
  CHECK(buf[0] != 0);
}

// Each §4.4.121 precondition violated alone: untagged key, sealed key, sealed
// data. All three reject, and all three still write the key's value as type.
TEST_F(CheriAssemblerArm64Test,
       seal_with_a_key_rejects_an_untagged_or_sealed_key_and_sealed_data) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));  // data
  // A key that is usable in every respect except the seal permission.
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 2 * kCRegSize, 4 * kCRegSize);
  // An untagged key whose value is a small, known object type: SCVALUE to an
  // address outside the bounds clears the tag but keeps the value, which is the
  // only way to get a key carrying an arbitrary integer.
  CHERI_MAKE_CAP(c20, x4, x5, 16, 4096);
  __ Seal(c21, c19, Cheri::kSealFormRb);  // a sealed key
  __ Seal(c22, c18, Cheri::kSealFormRb);  // a sealed data operand
  __ Seal(c23, c18, c20);                 // untagged key
  __ Seal(c24, c18, c21);                 // sealed key
  __ Seal(c25, c22, c19);                 // sealed data
  END();
  RUN();

  // The keys and the data are what the cases below claim they are; without
  // these they could all be exercising the same branch.
  CHECK_EQ(0u, C_TAG(c20));  // out-of-bounds SCVALUE invalidates ...
  CHECK_EQ(16u, C_ADDR(c20));
  CHECK_EQ(1u, C_TAG(c21));  // ... while the immediate form does seal.
  CHECK_EQ(1u, C_SEALED(c21));
  CHECK_EQ(1u, C_TAG(c22));
  CHECK_EQ(1u, C_SEALED(c22));
  CHECK_EQ(1u, C_TYPE(c22));  // the rb form writes object type 1

  // Untagged key: rejected, and the object type is the key's value exactly.
  CHECK_EQ(0u, C_TAG(c23));
  CHECK_EQ(16u, C_TYPE(c23));
  // Sealed key: rejected, and the key's value is still taken as the type.
  CHECK_EQ(0u, C_TAG(c24));
  CHECK_EQ(C_ADDR(c21) & kObjectTypeMask, C_TYPE(c24));
  // Sealed data: rejected, and its own object type is overwritten.
  CHECK_EQ(0u, C_TAG(c25));
  CHECK_EQ(C_ADDR(c19) & kObjectTypeMask, C_TYPE(c25));

  // Bounds, permissions and value always come from the data operand.
  CHECK_EQ(C_ADDR(c18), C_ADDR(c23));
  CHECK_EQ(C_BASE(c18), C_BASE(c23));
  CHECK_EQ(C_LEN(c18), C_LEN(c23));
  CHECK_EQ(C_PERM(c18), C_PERM(c23));
  CHECK_EQ(C_ADDR(c22), C_ADDR(c25));
  CHECK(buf[0] != 0);
}

// The port seals with __builtin_cheri_seal_entry (V8_CHERI_TO_SENTRY,
// src/base/macros.h:33) while the code generator uses SEAL (immediate,
// §4.4.122) rb. Both are branch targets, so they must agree.
TEST_F(CheriAssemblerArm64Test,
       seal_immediate_rb_agrees_with_the_compilers_sentry_builder) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Seal(c19, c18, Cheri::kSealFormRb);
  __ Gcseal(x2, c19);
  END();
  RUN();

  uintptr_t expected = V8_CHERI_TO_SENTRY(C_CAP(c18));
  CHECK_CAP_AGAINST(expected, c19);
  CHECK_EQ(1u, X64(x2));
  CHECK_EQ(1u, C_TYPE(c19));
  CHECK(buf[0] != 0);
}

// A sealed capability is not derivable: §4.4 is explicit that derivation is
// monotonic and the seal bit makes a capability non-derivable, so an ADD_c from
// a sealed source yields an untagged result rather than a fault.
TEST_F(CheriAssemblerArm64Test,
       deriving_from_a_sealed_capability_clears_the_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Seal(c19, c18, Cheri::kSealFormRb);
  __ Add(c20, c19, 16);
  __ Gcseal(x2, c20);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(0u, C_TAG(c20));  // Derived, so no longer valid.
  CHECK_EQ(buf_addr + 16, C_ADDR(c20));
  CHECK_EQ(1u, X64(x2));  // Still carries the object type.
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Stack and frame macros used by the real port.
// ---------------------------------------------------------------------------

// SlotAddress() has two spellings that reach the scaling through different
// constants; this keeps them agreeing.
TEST_F(CheriAssemblerArm64Test, slot_address_scales_by_the_unit_size) {
  SETUP();
  START();
  __ Mov(c28, csp);                   // pristine baseline
  __ SlotAddress(c18, 3);             // 3 capability slots
  __ SlotAddress(c19, 3, kXRegSize);  // 3 doubleword slots
  __ Mov(x0, 3);
  __ SlotAddress(c20, x0);             // register form, capability slots
  __ SlotAddress(c21, x0, kXRegSize);  // register form, doubleword slots
  __ Gcvalue(x1, c28);
  __ Gcbase(x2, c28);
  __ Gclen(x3, c28);
  __ Gctag(x4, c18);
  END();
  RUN();

  const uint64_t sp = X64(x1);
  CHECK_EQ(1u, X64(x4));
  CHECK_EQ(sp + 3 * kCRegSize, C_ADDR(c18));
  CHECK_EQ(sp + 3 * kXRegSize, C_ADDR(c19));
  CHECK_EQ(sp + 3 * kCRegSize, C_ADDR(c20));
  CHECK_EQ(sp + 3 * kXRegSize, C_ADDR(c21));

  // The address moved; the region did not. SlotAddress is a derivation, so the
  // bounds must still be the stack's, which is what lets the result be used for
  // a tagged store later.
  CHECK_EQ(1u, C_TAG(c18));
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_EQ(X64(x2), C_BASE(c18));
  CHECK_EQ(X64(x3), C_LEN(c18));
}

// Claim()/Drop() move csp in *slots*, not bytes. csp cannot be read back
// through RegisterDump, so the values are copied into ordinary registers.
TEST_F(CheriAssemblerArm64Test, claim_and_drop_move_csp_by_slots_not_bytes) {
  SETUP();
  START();
  __ Mov(c28, csp);  // baseline
  __ Claim(3);       // 3 capability slots
  __ Mov(c25, csp);
  __ Claim(2, kXRegSize);  // 2 doubleword slots
  __ Mov(c26, csp);
  __ Drop(2, kXRegSize);
  __ Mov(c27, csp);
  __ Drop(3);
  // All reads happen after every mutation, so no scratch register taken
  // internally by the SP arithmetic can overwrite them.
  __ Gctag(x0, c25);
  __ Gcvalue(x1, c25);
  __ Gcvalue(x2, c26);
  __ Gcvalue(x3, c27);
  __ Gcvalue(x4, csp);
  __ Gcvalue(x5, c28);
  END();
  RUN();

  const uint64_t base_sp = X64(x5);
  CHECK_EQ(1u, X64(x0));  // csp stays tagged throughout
  CHECK_EQ(base_sp - 3 * kCRegSize, X64(x1));
  CHECK_EQ(X64(x1) - 2 * kXRegSize, X64(x2));
  CHECK_EQ(X64(x1), X64(x3));  // Drop undoes Claim exactly
  CHECK_EQ(base_sp, X64(x4));
  // Both steps kept the 16-byte alignment the ABI requires.
  CHECK_EQ(0u, X64(x1) % 16);
  CHECK_EQ(0u, X64(x2) % 16);
}

// PushCRegList()/PopCRegList() spill whole capabilities: a tagged register must
// come back tagged, an untagged one must not become valid.
TEST_F(CheriAssemblerArm64Test,
       push_pop_c_reg_list_round_trips_tag_and_no_tag) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_UNTAG_KEEPING_METADATA(c19, c18, c18, 512, x2);
  __ Mov(c20, c18);  // pristine copies, for comparison after the pop
  __ Mov(c21, c19);
  __ PushCRegList({c18, c19});
  // Clobber both through the integer view, as a real callee would. There is no
  // unconditional Csel (which is why CzeroC DCHECKs on al/nv).
  __ Mov(c18.X(), xzr);
  __ Mov(c19.X(), xzr);
  __ PopCRegList({c18, c19});
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c18));
  CHECK_CAP_SAME(c18, c20);
  // The untagged value survives as an untagged value, metadata and all.
  CHECK_EQ(0u, C_TAG(c19));
  CHECK_CAP_SAME(c19, c21);
  CHECK(buf[0] != 0);
}

// Compares two capability *cells* in memory, field by field. Used by the copy
// tests below, where the destination is a host-visible array rather than a
// register, so the dump cannot reach it.
#define CHECK_CAP_CELL_EQ(expected, actual)                        \
  do {                                                             \
    CHECK_EQ(V8_CHERI_TAG_GET(expected) ? 1u : 0u,                 \
             V8_CHERI_TAG_GET(actual) ? 1u : 0u);                  \
    CHECK_EQ(static_cast<uint64_t>(V8_CHERI_ADDR_GET(expected)),   \
             static_cast<uint64_t>(V8_CHERI_ADDR_GET(actual)));    \
    CHECK_EQ(static_cast<uint64_t>(V8_CHERI_BASE_GET(expected)),   \
             static_cast<uint64_t>(V8_CHERI_BASE_GET(actual)));    \
    CHECK_EQ(static_cast<uint64_t>(V8_CHERI_LENGTH_GET(expected)), \
             static_cast<uint64_t>(V8_CHERI_LENGTH_GET(actual)));  \
    CHECK_EQ(static_cast<uint64_t>(V8_CHERI_PERMS(expected)),      \
             static_cast<uint64_t>(V8_CHERI_PERMS(actual)));       \
    CHECK_EQ(V8_CHERI_SEALED(expected) ? 1u : 0u,                  \
             V8_CHERI_SEALED(actual) ? 1u : 0u);                   \
  } while (0)

// CopyCapabilities() (builtins-arm64.cc:134, :1576, :2549) uses post-index
// accesses, so the base names the first slot copied in the direction of
// travel: ascending runs up from it, descending ends at it, reversing takes the
// source from its top and fills the destination from the base.
TEST_F(CheriAssemblerArm64Test,
       copy_capabilities_ascending_starts_at_the_base) {
  alignas(16) uintptr_t src[16];
  alignas(16) uintptr_t dst[16];
  memset(src, 0, sizeof(src));
  memset(dst, 0, sizeof(dst));

  // Three slots (1, 2, 3): an odd count, so the first slot goes through the
  // single Ldr/Str prologue and the remaining two through one LDP/STP pair.
  for (int i = 1; i <= 3; i++) {
    src[i] = reinterpret_cast<uintptr_t>(&src[i]);
  }
  // Sentinels in the slot below the region and in the first slot above it;
  // neither may be written, and both would be if the anchor convention were off
  // by one slot.
  const uintptr_t sentinel = reinterpret_cast<uintptr_t>(&dst[12]);
  dst[0] = sentinel;
  dst[4] = sentinel;
  // Distinct values immediately outside the source region, so that a base that
  // is off by one slot shows up as a wrong cell rather than as a zero.
  src[0] = reinterpret_cast<uintptr_t>(&src[0]);
  src[4] = reinterpret_cast<uintptr_t>(&src[4]);

  const uint64_t src_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(src));
  const uint64_t dst_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(dst));

  SETUP();
  START();
  CHERI_MAKE_CAP(c20, x0, x1, dst_addr, sizeof(dst));
  CHERI_MAKE_CAP(c21, x2, x3, src_addr, sizeof(src));
  // Both anchors are the region's first slot, which is slot 1 here so that the
  // slot below it can hold a sentinel.
  __ Mov(x4, dst_addr + kCRegSize);
  __ Scvalue(c20, c20, x4);
  __ Mov(x5, src_addr + kCRegSize);
  __ Scvalue(c21, c21, x5);
  __ Mov(x6, 3);
  __ CopyCapabilities(c20, c21, x6, MacroAssembler::kCapDstLessThanSrc);
  END();
  RUN();

  for (int i = 1; i <= 3; i++) {
    CHECK_CAP_CELL_EQ(src[i], dst[i]);
    CHECK(V8_CHERI_TAG_GET(dst[i]));
  }
  CHECK_CAP_CELL_EQ(sentinel, dst[0]);
  CHECK_CAP_CELL_EQ(sentinel, dst[4]);
  for (int i = 5; i < 16; i++) {
    CHECK(!V8_CHERI_TAG_GET(dst[i]));  // Nothing beyond the region was tagged.
  }
}

TEST_F(CheriAssemblerArm64Test, copy_capabilities_descending_ends_at_the_base) {
  alignas(16) uintptr_t src[16];
  alignas(16) uintptr_t dst[16];
  memset(src, 0, sizeof(src));
  memset(dst, 0, sizeof(dst));

  // Four slots (5, 6, 7, 8) below an anchor at slot 8: an even count, so every
  // transfer goes through a pair and the prologue is skipped. The pair order is
  // observable through the per-slot checks, because each cell names itself.
  for (int i = 5; i <= 8; i++) {
    src[i] = reinterpret_cast<uintptr_t>(&src[i]);
  }
  const uintptr_t sentinel = reinterpret_cast<uintptr_t>(&dst[12]);
  dst[4] = sentinel;  // the slot below the region
  dst[9] = sentinel;  // the slot above the anchor
  // Values just outside the source, distinct from the sentinels, so an
  // off-by-one copy shows up rather than silently reproducing a sentinel.
  src[4] = reinterpret_cast<uintptr_t>(&src[4]);
  src[9] = reinterpret_cast<uintptr_t>(&src[9]);

  const uint64_t src_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(src));
  const uint64_t dst_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(dst));

  SETUP();
  START();
  // The anchor is the region's *top* slot for the descending mode. SCVALUE
  // moves the value there while the bounds keep covering the whole array, so an
  // off-by-one here lands inside the region rather than faulting.
  CHERI_MAKE_CAP(c20, x0, x1, dst_addr, sizeof(dst));
  __ Mov(x2, dst_addr + 8 * kCRegSize);
  __ Scvalue(c20, c20, x2);
  CHERI_MAKE_CAP(c21, x3, x4, src_addr, sizeof(src));
  __ Mov(x5, src_addr + 8 * kCRegSize);
  __ Scvalue(c21, c21, x5);
  __ Mov(x6, 4);
  __ CopyCapabilities(c20, c21, x6, MacroAssembler::kCapSrcLessThanDst);
  END();
  RUN();

  for (int i = 5; i <= 8; i++) {
    CHECK_CAP_CELL_EQ(src[i], dst[i]);
    CHECK(V8_CHERI_TAG_GET(dst[i]));
  }
  CHECK_CAP_CELL_EQ(sentinel, dst[4]);
  CHECK_CAP_CELL_EQ(sentinel, dst[9]);  // Not overwritten from src[9].
  for (int i = 10; i < 16; i++) {
    CHECK(!V8_CHERI_TAG_GET(dst[i]));
  }
  for (int i = 0; i < 4; i++) {
    CHECK(!V8_CHERI_TAG_GET(dst[i]));
  }
}

// The reversing mode (builtins-arm64.cc:1576) is the one whose base convention
// differs between the two pointers.
TEST_F(CheriAssemblerArm64Test,
       copy_capabilities_reversed_writes_source_back_to_front) {
  alignas(16) uintptr_t src[8];
  alignas(16) uintptr_t dst[8];
  memset(src, 0, sizeof(src));
  memset(dst, 0, sizeof(dst));

  for (int i = 1; i <= 3; i++) {
    src[i] = reinterpret_cast<uintptr_t>(&src[i]);
  }
  const uintptr_t sentinel = reinterpret_cast<uintptr_t>(&dst[6]);
  dst[0] = sentinel;
  dst[4] = sentinel;

  const uint64_t src_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(src));
  const uint64_t dst_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(dst));

  SETUP();
  START();
  // Both arguments name the *first* slot of their region; CopyCapabilities
  // advances the source itself before the loop starts.
  CHERI_MAKE_CAP(c20, x0, x1, dst_addr, sizeof(dst));
  __ Mov(x2, dst_addr + kCRegSize);
  __ Scvalue(c20, c20, x2);
  CHERI_MAKE_CAP(c21, x3, x4, src_addr, sizeof(src));
  __ Mov(x5, src_addr + kCRegSize);
  __ Scvalue(c21, c21, x5);
  __ Mov(x6, 3);
  __ CopyCapabilities(c20, c21, x6,
                      MacroAssembler::kCapDstLessThanSrcAndReverse);
  END();
  RUN();

  // Destination slot 1 took the source's top slot, and so on down; the middle
  // slot is the only one that could not tell the two orders apart.
  CHECK_CAP_CELL_EQ(src[3], dst[1]);
  CHECK_CAP_CELL_EQ(src[2], dst[2]);
  CHECK_CAP_CELL_EQ(src[1], dst[3]);
  CHECK_CAP_CELL_EQ(sentinel, dst[0]);
  CHECK_CAP_CELL_EQ(sentinel, dst[4]);
  for (int i = 5; i < 8; i++) {
    CHECK(!V8_CHERI_TAG_GET(dst[i]));
  }
}

// ---------------------------------------------------------------------------
// Harness invariants.
// ---------------------------------------------------------------------------

// The dump must preserve tags, or every test above reads a reconstruction.
// csp is the exception: SPRegAliasesMatch() compares sp_ against a wsp_ written
// by a 32-bit store into a 64-bit field, so it is read from the hardware.
TEST_F(CheriAssemblerArm64Test,
       dump_preserves_tags_and_stack_pointer_is_tagged) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Gctag(x2, csp);
  __ Gcvalue(x3, csp);
  __ Gcbase(x4, csp);
  __ Gclen(x5, csp);
  END();
  RUN();

  // The dump carries the tag, not just the address.
  CHECK_EQ(1u, C_TAG(c18));
  CHECK_EQ(1u, X64(x2));
  // The stack pointer capability covers the pointer the frame actually uses.
  uint64_t sp = X64(x3);
  CHECK(sp >= X64(x4));
  CHECK(sp < X64(x4) + X64(x5));
  CHECK_NE(0u, X64(x5));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Edge cases around instruction selection: which encoding a wrapper picks,
// and the field layout that makes the choice observable.
// ---------------------------------------------------------------------------

// Spec 4.4.114: both SCBNDS (immediate) classes carry the length in the same
// 6-bit field at 20:15 and differ only in bit 14, the S flag. The scaled class
// stores length >> 4, so field 63 means 1008 bytes, not 63.
TEST_F(CheriAssemblerArm64Test, scbnds_immediates_share_one_length_field) {
  SETUP_ENCODER();
  uint32_t unscaled = 0;
  uint32_t scaled = 0;

  ENCODE(unscaled, enc.scbnds_imm_unscaled(c19, c18, 63));
  ENCODE(scaled, enc.scbnds_imm_scaled(c19, c18, 63));

  CHECK_EQ(63u, (unscaled >> 15) & 0x3F);
  CHECK_EQ(63u, (scaled >> 15) & 0x3F);
  CHECK_EQ(static_cast<uint32_t>(1 << 14), unscaled ^ scaled);
  CHECK_EQ(19u, unscaled & 0x1F);
  CHECK_EQ(18u, (unscaled >> 5) & 0x1F);

  // The field is 6 bits wide in both classes, so 63 is the last encodable
  // value and the unscaled reach is therefore only 0..63 bytes.
  uint32_t zero = 0;
  ENCODE(zero, enc.scbnds_imm_unscaled(c19, c18, 0));
  CHECK_EQ(0u, (zero >> 15) & 0x3F);
}

// The wrapper converts a requested length into a class plus a field value.
// Getting the shift direction wrong sends field `length << 4` to an encoder
// that wants `length >> 4`, which aborts for every scaled length.
TEST_F(CheriAssemblerArm64Test,
       macroassembler_scbnds_routes_immediates_to_the_right_class) {
  SETUP_ENCODER();
  uint32_t raw = 0;
  uint32_t routed = 0;

  ENCODE(raw, enc.scbnds_imm_unscaled(c19, c18, 63));
  ENCODE(routed, enc.Scbnds(c19, c18, Operand(63)));
  CHECK_EQ(raw, routed);

  // 64 is the first length that needs the scaled class: field 4, not 64 << 4.
  ENCODE(raw, enc.scbnds_imm_scaled(c19, c18, 4));
  ENCODE(routed, enc.Scbnds(c19, c18, Operand(64)));
  CHECK_EQ(raw, routed);
  CHECK_EQ(4u, (routed >> 15) & 0x3F);

  // 1008 is the last one: field 63.
  ENCODE(raw, enc.scbnds_imm_scaled(c19, c18, 63));
  ENCODE(routed, enc.Scbnds(c19, c18, Operand(1008)));
  CHECK_EQ(raw, routed);
}

// Above the scaled reach the wrapper must materialise the length in a scratch
// register and use the register class. Only the last word is compared because
// the scratch register is the scope's choice, not the caller's.
TEST_F(CheriAssemblerArm64Test,
       macroassembler_scbnds_out_of_range_immediate_uses_the_register_form) {
  SETUP_ENCODER();
  enc.Reset();
  enc.Scbnds(c19, c18, Operand(1024));
  const int count = enc.pc_offset() / kInstrSize;
  CHECK(count >= 2);
  const uint32_t last =
      reinterpret_cast<const uint32_t*>(enc.buffer_start())[count - 1];

  // SCBNDS (register): everything outside Rm, Cn and Cd is fixed.
  const uint32_t fixed_mask = ~(0x1Fu << 16 | 0x1Fu << 5 | 0x1Fu);
  CHECK_EQ(SCBNDS_REG & fixed_mask, last & fixed_mask);
  CHECK_EQ(19u, last & 0x1F);
  CHECK_EQ(18u, (last >> 5) & 0x1F);
  // Not the scaled immediate class, which shares most of its fixed bits.
  CHECK_EQ(0u, last & (1 << 14));
}

// The routing above has to produce the lengths that were asked for. All three
// stay inside the immediate window, so no scratch register is involved.
TEST_F(CheriAssemblerArm64Test, scbnds_reaches_lengths_at_both_boundaries) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Scbnds(c19, c18, Operand(63));    // Last unscaled.
  __ Scbnds(c20, c18, Operand(64));    // First scaled.
  __ Scbnds(c21, c18, Operand(1008));  // Last scaled.
  END();
  RUN();

  CHECK_EQ(63u, C_LEN(c19));
  CHECK_EQ(64u, C_LEN(c20));
  CHECK_EQ(1008u, C_LEN(c21));
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_EQ(buf_addr, C_BASE(c21));
  CHECK(buf[0] != 0);
}

// The lowercase acquire/release/exclusive wrappers dispatch on the transfer
// register; all four once hardcoded the *_x opcode and Rt(rt.X()).
TEST_F(CheriAssemblerArm64Test,
       lowercase_atomic_wrappers_emit_the_capability_word) {
  SETUP_ENCODER();
  uint32_t plain = 0;
  uint32_t explicit_cap = 0;
  uint32_t integer = 0;

  ENCODE(plain, enc.ldar(c21, c12));
  ENCODE(explicit_cap, enc.ldar_c(c21, c12));
  CHECK_EQ(explicit_cap, plain);
  ENCODE(integer, enc.ldar(x21, c12));
  CHECK_NE(plain, integer);
  // The transfer register and base keep their fields in both classes.
  CHECK_EQ(21u, integer & 0x1F);
  CHECK_EQ(12u, (integer >> 5) & 0x1F);

  ENCODE(plain, enc.stlr(c21, c12));
  ENCODE(explicit_cap, enc.stlr_c(c21, c12));
  CHECK_EQ(explicit_cap, plain);

  ENCODE(plain, enc.ldaxr(c21, c12));
  ENCODE(explicit_cap, enc.ldaxr_c(c21, c12));
  CHECK_EQ(explicit_cap, plain);

  ENCODE(plain, enc.stlxr(w5, c21, c12));
  ENCODE(explicit_cap, enc.stlxr_c(w5, c21, c12));
  CHECK_EQ(explicit_cap, plain);
}

// Same dispatch end to end. The integer transfer register is the control: it
// must still take the 64-bit path.
TEST_F(CheriAssemblerArm64Test, lowercase_stlr_ldar_round_trip_keeps_the_tag) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uintptr_t cell[2];
  memset(buf, 0xAA, sizeof(buf));
  memset(cell, 0, sizeof(cell));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t cell_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cell));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, cell_addr, sizeof(cell));
  __ stlr(c18, c19);
  __ ldar(c20, c19);
  __ ldar(x6, c19);  // Same base, integer transfer register.
  END();
  RUN();

  CHECK(V8_CHERI_TAG_GET(cell[0]));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_CAP_SAME(c18, c20);
  // The integer load sees the value half and no tag.
  CHECK_EQ(buf_addr, X64(x6));
  CHECK_EQ(0u, C_TAG(c6));
  CHECK(buf[0] != 0);
}

// The exclusive pair, likewise: LDAXR must return a tagged capability and
// STLXR must report success in its status register while writing a tagged
// capability. This is the non-LSE path for kArm64CapabilityAtomicLoad/Store.
TEST_F(CheriAssemblerArm64Test,
       lowercase_ldaxr_stlxr_round_trip_keeps_the_tag) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  alignas(16) uintptr_t cell[2];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  memset(cell, 0, sizeof(cell));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));
  const uint64_t cell_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cell));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c22, x4, x5, other_addr, sizeof(other));
  CHERI_MAKE_CAP(c19, x2, x3, cell_addr, sizeof(cell));
  __ stlr(c18, c19);
  __ ldaxr(c20, c19);
  __ stlxr(w1, c22, c19);
  __ ldar(c21, c19);
  END();
  RUN();

  CHECK_EQ(0u, X64(w1));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_CAP_SAME(c18, c20);
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_CAP_SAME(c22, c21);
  CHECK(buf[0] != 0);
}

// The non-LSE CAS lowers as LDAXR, SUBS_c, B.NE: an X-view address compare
// would accept an untagged look-alike, so the branch must see the tag.
TEST_F(CheriAssemblerArm64Test, cas_emulation_branches_on_tags_not_addresses) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uintptr_t cell[2];
  memset(buf, 0xAA, sizeof(buf));
  memset(cell, 0, sizeof(cell));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t cell_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cell));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, cell_addr, sizeof(cell));
  // Leave an untagged copy of c18 in the cell: same address, tag cleared.
  __ Stp(c18, c18, MemOperand(c19, 0));
  __ Mov(x4, c18.X());
  __ Str(x4, MemOperand(c19, 0));

  __ ldaxr(c20, c19);
  __ Mov(x5, 0);
  __ Mov(x6, 1);
  Label mismatch;
  __ Cmpc(c20, Operand(c18));
  __ B(ne, &mismatch);
  __ Mov(x5, 1);  // Reachable only if the compare called the two equal.
  __ Bind(&mismatch);
  END();
  RUN();

  CHECK_EQ(0u, C_TAG(c20));
  CHECK_EQ(buf_addr, C_ADDR(c20));
  CHECK_EQ(0u, X64(x5));  // The tag mismatch was seen.
  CHECK_EQ(1u, X64(x6));
  CHECK_EQ(0u, core.flags_nzcv() & ZFlag);
  CHECK(buf[0] != 0);
}

// Stpc/Ldpc are the capability spellings of the stack pair accesses. They were
// generated from an inverted macro list (Ldpc mapped to the store opcode) and
// then never instantiated, so any use was a link error.
TEST_F(CheriAssemblerArm64Test,
       ldpc_stpc_encode_like_the_capability_pair_instructions) {
  SETUP_ENCODER();
  uint32_t stpc = 0;
  uint32_t stp = 0;
  uint32_t ldpc = 0;
  uint32_t ldp = 0;

  ENCODE(stp, enc.Stp(c18, c22, MemOperand(c19, 0)));
  ENCODE(stpc, enc.Stpc(c18, c22, MemOperand(c19, 0)));
  CHECK_EQ(stp, stpc);

  ENCODE(ldp, enc.Ldp(c18, c22, MemOperand(c19, 0)));
  ENCODE(ldpc, enc.Ldpc(c18, c22, MemOperand(c19, 0)));
  CHECK_EQ(ldp, ldpc);

  // The two differ only in the L bit, so a swap of the two macros is visible.
  CHECK_EQ(static_cast<uint32_t>(LoadStorePairLBit), stpc ^ ldpc);
}

TEST_F(CheriAssemblerArm64Test, ldpc_stpc_round_trip_preserves_both_tags) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  alignas(16) uintptr_t cell[8];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  memset(cell, 0, sizeof(cell));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));
  const uint64_t cell_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cell));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c22, x4, x5, other_addr, sizeof(other));
  CHERI_MAKE_CAP(c19, x2, x3, cell_addr, sizeof(cell));
  __ Stpc(c18, c22, MemOperand(c19, 0));
  __ Ldpc(c20, c21, MemOperand(c19, 0));
  END();
  RUN();

  // uintptr_t is a full 128-bit cell here, so the pair occupies cell[0] and
  // cell[1]; cell[2] is past the store.
  CHECK(V8_CHERI_TAG_GET(cell[0]));
  CHECK(V8_CHERI_TAG_GET(cell[1]));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_CAP_SAME(c18, c20);
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_CAP_SAME(c22, c21);
  CHECK(buf[0] != 0);
}

// Spec 4.4.5 and 4.4.6: ALIGND/ALIGNU take <Cd|CSP>, <Cn|CSP>, so the stack
// capability is a legal operand in both positions. The encoders rejected it
// while every other capability transfer instruction accepted CSP.
TEST_F(CheriAssemblerArm64Test,
       alignd_alignu_encode_with_a_capability_stack_operand) {
  SETUP_ENCODER();
  uint32_t aligned = 0;
  uint32_t alignu = 0;
  uint32_t plain = 0;

  ENCODE(aligned, enc.alignd(csp, c12, Operand(3)));
  ENCODE(plain, enc.alignd(c19, c12, Operand(3)));
  ENCODE(alignu, enc.alignu(csp, c12, Operand(3)));

  CHECK_EQ(31u, aligned & 0x1F);
  CHECK_EQ(12u, (aligned >> 5) & 0x1F);
  CHECK_EQ(3u, (aligned >> 15) & 0x3F);
  // Only the destination register class differs from the ordinary form.
  CHECK_EQ(static_cast<uint32_t>(31u ^ 19u), aligned ^ plain);
  // ALIGNU and ALIGND differ only in bit 14, the U flag.
  CHECK_EQ(static_cast<uint32_t>(1 << 14), alignu ^ aligned);
}

// §4.4.122: the immediate forms pack a 2-bit `form` at 14:13 (01 rb, 10 lpb,
// 11 lb); 00 is UNDEFINED. ObjectType is set before the tag test.
TEST_F(CheriAssemblerArm64Test,
       seal_immediate_forms_differ_only_in_the_form_field) {
  SETUP_ENCODER();
  uint32_t rb = 0;
  uint32_t lpb = 0;
  uint32_t lb = 0;

  ENCODE(rb, enc.Seal(c19, c18, Cheri::kSealFormRb));
  ENCODE(lpb, enc.Seal(c19, c18, Cheri::kSealFormLpb));
  ENCODE(lb, enc.Seal(c19, c18, Cheri::kSealFormLb));

  CHECK_EQ(19u, rb & 0x1F);
  CHECK_EQ(18u, (rb >> 5) & 0x1F);
  CHECK_EQ(rb & ~0x6000u, lpb & ~0x6000u);
  CHECK_EQ(rb & ~0x6000u, lb & ~0x6000u);
  CHECK_EQ(0x2000u, rb & 0x6000u);
  CHECK_EQ(0x4000u, lpb & 0x6000u);
  CHECK_EQ(0x6000u, lb & 0x6000u);
}

TEST_F(CheriAssemblerArm64Test,
       seal_immediate_lpb_and_lb_set_the_matching_object_type) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Seal(c19, c18, Cheri::kSealFormLpb);
  __ Seal(c20, c18, Cheri::kSealFormLb);
  END();
  RUN();

  // The form field *is* the object type for the immediate forms.
  CHECK_EQ(2u, C_TYPE(c19));
  CHECK_EQ(3u, C_TYPE(c20));
  CHECK_EQ(1u, C_SEALED(c19));
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(buf_addr, C_ADDR(c20));
  CHECK_EQ(C_LEN(c18), C_LEN(c20));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Capability logical operations.
// ---------------------------------------------------------------------------

// There is no AND_c/ORR_c/EOR_c, so the wrappers combine the address halves in
// X registers and re-attach rn's provenance with SCVALUE; Eor had no
// capability branch at all.
//
// SCVALUE clears the tag when the value leaves the bounds (spec 5.390), so XOR
// is invalidated in hardware while OR and AND stay inside the region.
TEST_F(CheriAssemblerArm64Test,
       capability_logical_ops_combine_the_address_halves) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t region =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf)) &
      ~static_cast<uint64_t>(0x1FFF);

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, region + 0x10, 8192);
  __ Mov(x4, region + 0x20);
  __ Scvalue(c19, c18, x4);
  __ Orr(c20, c18, Operand(c19));
  __ Eor(c21, c18, Operand(c19));
  __ And(c22, c18, Operand(c19));
  END();
  RUN();

  CHECK_EQ(region + 0x30, C_ADDR(c20));
  CHECK_EQ(0x30u, C_ADDR(c21));
  CHECK_EQ(region, C_ADDR(c22));

  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(1u, C_TAG(c22));
  CHECK_EQ(0u, C_TAG(c21));

  // The two results that stay in bounds take every field but the value from rn.
  // The invalidated XOR is excluded: for an untagged capability the metadata is
  // unspecified, so its base and bounds read back as garbage rather than c18's.
  const Register tagged[] = {c20, c22};
  for (const Register& result : tagged) {
    CHECK_EQ(C_BASE(c18), C_BASE(result));
    CHECK_EQ(C_LEN(c18), C_LEN(result));
    CHECK_EQ(C_PERM(c18), C_PERM(result));
    CHECK_EQ(0u, C_SEALED(result));
  }
  CHECK(buf[0] != 0);
}

// The mask form works in place, as the alignment idioms do. Writing rd.X()
// clears its tag, so the result is checked for tag as well as value; rn == rd
// needs a scratch from the temporary list.
TEST_F(CheriAssemblerArm64Test,
       capability_logical_ops_with_immediate_and_aliased_destination) {
  alignas(64) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  // c20 covers the same region but sits 0x2F past its base.
  __ Mov(x4, buf_addr + 0x2F);
  __ Scvalue(c20, c18, x4);
  __ And(c20, c20, Operand(~0x3F));  // Align down, in place.
  __ Orr(c21, c18, Operand(0x3F));
  __ Eor(c22, c18, Operand(0x3F));
  END();
  RUN();

  CHECK_EQ(buf_addr, C_ADDR(c20));
  CHECK_EQ(buf_addr | 0x3F, C_ADDR(c21));
  CHECK_EQ(buf_addr ^ 0x3F, C_ADDR(c22));
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_EQ(1u, C_TAG(c22));
  CHECK_EQ(buf_addr, C_BASE(c20));
  CHECK_EQ(C_LEN(c18), C_LEN(c20));
  CHECK(buf[0] != 0);
}

// The integer path must be untouched: it goes straight to LogicalMacro, with no
// SCVALUE, so the result is an untagged 64-bit value even though a C
// destination register number names the same hardware register.
TEST_F(CheriAssemblerArm64Test, integer_logical_ops_keep_the_x_path) {
  alignas(16) uint64_t buf[1024];
  alignas(16) uint64_t other[8];
  memset(buf, 0xAA, sizeof(buf));
  memset(other, 0xBB, sizeof(other));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));
  const uint64_t other_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(other));

  SETUP();
  START();
  __ Mov(x18, buf_addr);
  __ Mov(x19, other_addr);
  __ Orr(x20, x18, Operand(x19));
  __ Eor(x21, x18, Operand(x19));
  __ And(x22, x18, Operand(~0x3F));
  END();
  RUN();

  CHECK_EQ(buf_addr | other_addr, X64(x20));
  CHECK_EQ(buf_addr ^ other_addr, X64(x21));
  CHECK_EQ(buf_addr & ~static_cast<uint64_t>(0x3F), X64(x22));
  // The X view of a destination that was written by these operations carries no
  // tag, which is what distinguishes it from the capability wrappers.
  CHECK_EQ(0u, C_TAG(c20));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Integer-only branches fed a capability register.
// ---------------------------------------------------------------------------

// TB[N]Z/CB[N]Z have no capability form, so these test the X view. Tbz used to
// emit the out-of-range path's second test on the *capability* register; SF()
// has no 128-bit case, so it encoded as the 32-bit form and cannot name bit 63.
TEST_F(CheriAssemblerArm64Test,
       out_of_range_branches_test_the_x_view_of_a_capability) {
  SETUP_ENCODER();
  SETUP_FAR_ENCODER();

  // The oracle for the first word of the extra-instructions path: the raw
  // 64-bit emitter, one instruction ahead of its own target, which is exactly
  // where the macro's `done` label sits.
  uint32_t tbnz_ref = 0;
  uint32_t tbz_ref = 0;
  uint32_t cbnz_ref = 0;
  uint32_t cbz_ref = 0;
  ENCODE(tbnz_ref, Label d; enc.tbnz(x18, 63, &d); enc.bind(&d));
  ENCODE(tbz_ref, Label d; enc.tbz(x18, 63, &d); enc.bind(&d));
  ENCODE(cbnz_ref, Label d; enc.cbnz(x18, &d); enc.bind(&d));
  ENCODE(cbz_ref, Label d; enc.cbz(x18, &d); enc.bind(&d));

  Label target;
  far.Bind(&target);
  far.nop();
  // In reach here, so the near path must be a single instruction: without this
  // the size checks below would pass even if every branch took the long path.
  int near_pc = far.pc_offset();
  far.Tbz(c18, 3, &target);
  CHECK_EQ(kInstrSize, far.pc_offset() - near_pc);

  for (int i = 0; i < CHERI_FAR_WALK; i++) {
    far.nop();
  }
  // Past both the +-32 KiB TB[N]Z range and the +-1 MiB CB[N]Z range.
  CHECK_LT(1 << 20, far.pc_offset());

// Each macro emits two instructions and the capability spelling must produce
// the same test as the X spelling. The reference label is one instruction ahead
// while the inverted test jumps two, so the displacement field is masked out.
#define CHECK_FAR(EMIT_CAP, EMIT_X, REF, DISP_MASK)                       \
  do {                                                                    \
    int p = far.pc_offset();                                              \
    EMIT_CAP;                                                             \
    int q = far.pc_offset();                                              \
    EMIT_X;                                                               \
    int r = far.pc_offset();                                              \
    CHECK_EQ(2 * kInstrSize, q - p);                                      \
    CHECK_EQ(2 * kInstrSize, r - q);                                      \
    const uint32_t* words =                                               \
        reinterpret_cast<const uint32_t*>(far.buffer_start() + p);        \
    CHECK_EQ((REF) & ~(DISP_MASK), words[0] & ~(DISP_MASK));              \
    CHECK_EQ(words[0], words[2]);                                         \
    CHECK_EQ(0x14000000u, words[1] & 0xFC000000u); /* b, unconditional */ \
    CHECK_EQ(0x14000000u, words[3] & 0xFC000000u);                        \
  } while (0)

  // Test-branch displacement is bits 17:5, compare-branch is bits 23:5.
  const uint32_t kTbDisp = 0x0003FFE0;
  const uint32_t kCbDisp = 0x00FFFFE0;
  CHECK_FAR(far.Tbz(c18, 63, &target), far.Tbz(x18, 63, &target), tbnz_ref,
            kTbDisp);
  CHECK_FAR(far.Tbnz(c18, 63, &target), far.Tbnz(x18, 63, &target), tbz_ref,
            kTbDisp);
  CHECK_FAR(far.Cbz(c18, &target), far.Cbz(x18, &target), cbnz_ref, kCbDisp);
  CHECK_FAR(far.Cbnz(c18, &target), far.Cbnz(x18, &target), cbz_ref, kCbDisp);

#undef CHECK_FAR
}

// Encoding equality only proves the two spellings agree. This runs the four
// macros against a real capability to show that they read its value field and
// leave the capability itself untouched.
TEST_F(CheriAssemblerArm64Test,
       branch_macros_test_the_value_field_of_a_capability) {
  // 32-byte, not 16-byte, alignment: the test below relies on bit 4 of the
  // address being clear as well as bits 0-2.
  alignas(32) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  // The buffer is 32-byte aligned, so at buf + 8 bit 3 of the address is set
  // and bit 4 is clear.
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr + 8, sizeof(buf) - 8);

  Label skip1, skip2, skip3, skip4, skip5;
  __ Mov(x2, 0);
  __ Tbz(c18, 3, &skip1);  // Bit set: not taken.
  __ Mov(x2, 1);
  __ Bind(&skip1);

  __ Mov(x3, 0);
  __ Tbz(c18, 4, &skip2);  // Bit clear: taken.
  __ Mov(x3, 1);
  __ Bind(&skip2);

  __ Mov(x4, 0);
  __ Tbnz(c18, 4, &skip3);  // Bit clear: not taken.
  __ Mov(x4, 1);
  __ Bind(&skip3);

  __ Mov(x5, 0);
  __ Cbz(c18, &skip4);  // Non-null: not taken.
  __ Mov(x5, 1);
  __ Bind(&skip4);

  __ Mov(x6, 0);
  __ Cbnz(c18, &skip5);  // Non-null: taken.
  __ Mov(x6, 1);
  __ Bind(&skip5);
  END();
  RUN();

  CHECK_EQ(1u, X64(x2));
  CHECK_EQ(0u, X64(x3));
  CHECK_EQ(1u, X64(x4));
  CHECK_EQ(1u, X64(x5));
  CHECK_EQ(0u, X64(x6));
  CHECK_EQ(1u, C_TAG(c18));
  CHECK_EQ(buf_addr + 8, C_ADDR(c18));
  CHECK(buf[8] != 0);
}

// MacroAssembler::Csel must reach CSEL_c: A64 conditional selects cannot move
// a tag. The reference word comes from §4.4.39: bits 31:21 = 11000010110,
// 20:16 = Cm, 15:12 = cond, 11:10 = 11, 9:5 = Cn, 4:0 = Cd.
TEST_F(CheriAssemblerArm64Test, csel_macro_emits_the_capability_form) {
  SETUP_ENCODER();
  const uint32_t fixed = (0x616u << 21) | (3u << 10);
  const uint32_t cd = 1u;
  const uint32_t cn = 2u << 5;
  const uint32_t cm = 3u << 16;
  const uint32_t czr_cm = 31u << 16;
  // The NE condition field, which is the only difference from the EQ reference.
  const uint32_t ne_field = 1u << 12;
  uint32_t via_csel = 0;
  uint32_t via_cselc = 0;
  uint32_t via_macro = 0;
  uint32_t via_macro_zero = 0;
  ENCODE(via_csel, enc.csel(c1, c2, c3, eq));
  ENCODE(via_cselc, enc.cselc(c1, c2, c3, eq));
  ENCODE(via_macro, enc.Csel(c1, c2, Operand(c3), eq));
  ENCODE(via_macro_zero, enc.Csel(c1, c2, Operand(0), ne));

  const uint32_t ref = fixed | cm | cn | cd;
  CHECK_EQ(ref, via_csel);
  CHECK_EQ(ref, via_cselc);
  CHECK_EQ(ref, via_macro);
  CHECK_EQ(fixed | czr_cm | ne_field | cn | cd, via_macro_zero);
}

// ---------------------------------------------------------------------------
// Addressing-mode displacement fields.
// ---------------------------------------------------------------------------

// LDR/STR (capability, unsigned offset, 4.4.104/4.4.136) counts the
// displacement in 16-byte cells, so the reach is 0..65520 rather than the
// 0..4080 an unscaled byte field of the same width would give.
TEST_F(CheriAssemblerArm64Test,
       capability_unsigned_offset_field_counts_sixteen_byte_cells) {
  SETUP_ENCODER();
  const uint32_t kRegs = (20u << 5) | 18u;
  const int offsets[] = {0, 16, 4096, 65520};
  for (int offset : offsets) {
    uint32_t ldr = 0;
    uint32_t str = 0;
    ENCODE(ldr, enc.Ldr(c18, MemOperand(c20, offset)));
    ENCODE(str, enc.Str(c18, MemOperand(c20, offset)));
    const uint32_t field =
        static_cast<uint32_t>(offset >> 4) << 10;  // ImmLSUnsigned, 21:10.
    CHECK_EQ(0xC2400000u | field | kRegs, ldr);
    CHECK_EQ(0xC2000000u | field | kRegs, str);
    CHECK_EQ(1u << 22, ldr ^ str);
  }

  // 65536 would need a field of 4096, one past the 12-bit reach, so the
  // displacement is materialized in a register instead.
  enc.Reset();
  CHECK_EQ(0, enc.pc_offset());
  enc.Ldr(c18, MemOperand(c20, 65536));
  CHECK_EQ(2 * kInstrSize, enc.pc_offset());
}

// LDR/STR (capability, unscaled offset, 4.4.111/4.4.112) keeps imm9 signed and
// in *bytes*. It is the only class that reaches an unaligned displacement, and
// where the two classes overlap (multiples of 16 in 0..255) the scaled form is
// the one selected.
TEST_F(CheriAssemblerArm64Test,
       capability_unscaled_offset_field_is_signed_and_in_bytes) {
  SETUP_ENCODER();
  const uint32_t kRegs = (20u << 5) | 18u;
  const int offsets[] = {-256, -16, -1, 8, 255};
  for (int offset : offsets) {
    uint32_t ldr = 0;
    uint32_t str = 0;
    ENCODE(ldr, enc.Ldr(c18, MemOperand(c20, offset)));
    ENCODE(str, enc.Str(c18, MemOperand(c20, offset)));
    const uint32_t field =
        (static_cast<uint32_t>(offset) & 0x1FFu) << 12;  // ImmLS, 20:12.
    CHECK_EQ(0xA2400000u | field | kRegs, ldr);
    CHECK_EQ(0xA2000000u | field | kRegs, str);
  }

  // 264 is past the signed field, so it costs two instructions.
  enc.Reset();
  CHECK_EQ(0, enc.pc_offset());
  enc.Str(c18, MemOperand(c20, 264));
  CHECK_EQ(2 * kInstrSize, enc.pc_offset());
}

// The pre/post-index capability classes (4.4.113/4.4.114) scale imm9 by 16 as
// well, so their reach is +/-4080 in 16-byte steps -- but the macro assembler
// gates them on the *unscaled* range of +/-255 bytes, leaving 4080/255 = 16x of
// the field unused, and writes back from the value it just loaded or stored.
TEST_F(CheriAssemblerArm64Test,
       capability_pre_and_post_index_fields_count_sixteen_byte_cells) {
  SETUP_ENCODER();
  const uint32_t kRegs = (20u << 5) | 18u;
  const int offsets[] = {16, -16, 240, -240};
  for (int offset : offsets) {
    uint32_t pre = 0;
    uint32_t post = 0;
    uint32_t pre_store = 0;
    ENCODE(pre, enc.Ldr(c18, MemOperand(c20, offset, PreIndex)));
    ENCODE(post, enc.Ldr(c18, MemOperand(c20, offset, PostIndex)));
    ENCODE(pre_store, enc.Str(c18, MemOperand(c20, offset, PreIndex)));
    const uint32_t field =
        (static_cast<uint32_t>(offset >> 4) & 0x1FFu) << 12;
    CHECK_EQ(0xA2400C00u | field | kRegs, pre);
    CHECK_EQ(0xA2400400u | field | kRegs, post);
    CHECK_EQ(1u << 11, pre ^ post);
    CHECK_EQ(1u << 22, pre ^ pre_store);
  }

  // 400 fits the field's units but not the gate, so the writeback is a separate
  // capability add.
  enc.Reset();
  CHECK_EQ(0, enc.pc_offset());
  enc.Ldr(c18, MemOperand(c20, 400, PreIndex));
  CHECK_EQ(2 * kInstrSize, enc.pc_offset());
}

// Every pair displacement (4.4.118 through 4.4.125) is scaled by the access
// size, which for capabilities is 16, giving -1024..1008.
TEST_F(CheriAssemblerArm64Test,
       capability_pair_displacement_field_is_scaled_in_every_addr_mode) {
  SETUP_ENCODER();
  const uint32_t kRegs = (20u << 5) | (19u << 10) | 18u;
  const int offsets[] = {16, -16, 1008, -1024};
  for (int offset : offsets) {
    const uint32_t field =
        (static_cast<uint32_t>(offset >> 4) & 0x7Fu) << 15;  // ImmLSPair.
    uint32_t ld = 0;
    uint32_t st = 0;
    uint32_t ld_pre = 0;
    uint32_t ld_post = 0;
    uint32_t ldpc = 0;
    uint32_t stpc = 0;
    ENCODE(ld, enc.Ldp(c18, c19, MemOperand(c20, offset)));
    ENCODE(st, enc.Stp(c18, c19, MemOperand(c20, offset)));
    ENCODE(ld_pre, enc.Ldp(c18, c19, MemOperand(c20, offset, PreIndex)));
    ENCODE(ld_post, enc.Ldp(c18, c19, MemOperand(c20, offset, PostIndex)));
    ENCODE(ldpc, enc.Ldpc(c18, c19, MemOperand(c20, offset)));
    ENCODE(stpc, enc.Stpc(c18, c19, MemOperand(c20, offset)));
    CHECK_EQ(0x42C00000u | field | kRegs, ld);
    CHECK_EQ(0x42800000u | field | kRegs, st);
    CHECK_EQ(0x62C00000u | field | kRegs, ld_pre);
    CHECK_EQ(0x22C00000u | field | kRegs, ld_post);
    CHECK_EQ(ld, ldpc);
    CHECK_EQ(st, stpc);
  }
}

// The register-offset class (4.4.80) has a single "a12" bit that scales the
// index by the access size; there is no shift-by-n field, so only LSL #4 (or
// #0) is expressible.
TEST_F(CheriAssemblerArm64Test, capability_register_offset_scaling_is_one_bit) {
  SETUP_ENCODER();
  uint32_t scaled = 0;
  uint32_t unscaled = 0;
  uint32_t uxtw = 0;
  ENCODE(scaled, enc.Ldr(c18, MemOperand(c20, x4, LSL, 4)));
  ENCODE(unscaled, enc.Ldr(c18, MemOperand(c20, x4, LSL, 0)));
  ENCODE(uxtw, enc.Ldr(c18, MemOperand(c20, x4, UXTW, 0)));
  CHECK_EQ(1u << 12, scaled ^ unscaled);
  // LSL is spelled UXTX in the option field, one bit away from UXTW.
  CHECK_EQ(1u << 13, uxtw ^ unscaled);
}

// Assembler::LoadStore/LoadStorePair normalize the base with .C() before
// looking at its class, so an X-numbered base silently means the same-numbered
// capability register instead of being rejected.
TEST_F(CheriAssemblerArm64Test,
       capability_accesses_ignore_the_base_register_class) {
  SETUP_ENCODER();
  uint32_t cap_base = 0;
  uint32_t int_base = 0;
  ENCODE(cap_base, enc.Ldr(c18, MemOperand(c20, 16)));
  ENCODE(int_base, enc.Ldr(c18, MemOperand(x20, 16)));
  CHECK_EQ(cap_base, int_base);

  uint32_t cap_pair = 0;
  uint32_t int_pair = 0;
  ENCODE(cap_pair, enc.Ldp(c18, c19, MemOperand(c20, 16)));
  ENCODE(int_pair, enc.Ldp(c18, c19, MemOperand(x20, 16)));
  CHECK_EQ(cap_pair, int_pair);
}

// ---------------------------------------------------------------------------
// Capability add/sub with a shifted immediate.
// ---------------------------------------------------------------------------

// ADD/SUB (immediate, capability, 4.4.2/4.4.155) reuses the A64 imm12 + sh
// field: anything above 4095 must be a multiple of 4096 and is stored shifted.
// The port puts "sh" in bit 22 and the add/sub selector in bit 23, which the
// header declares as the top half of the same two-bit ShiftAddSubCapability
// field -- the executed test below is what settles that.
TEST_F(CheriAssemblerArm64Test,
       capability_add_sub_immediates_use_the_shifted_field) {
  SETUP_ENCODER();
  const uint32_t kRegs = (19u << 5) | 18u;

  const int64_t small_imms[] = {1, 4095};
  for (int64_t imm : small_imms) {
    uint32_t add = 0;
    uint32_t sub = 0;
    ENCODE(add, enc.Add(c18, c19, Operand(imm)));
    ENCODE(sub, enc.Sub(c18, c19, Operand(imm)));
    CHECK_EQ(0x02000000u | (static_cast<uint32_t>(imm) << 10) | kRegs, add);
    CHECK_EQ(1u << 23, add ^ sub);
  }

  const int64_t big_imms[] = {4096, 8192, 16773120};
  for (int64_t imm : big_imms) {
    uint32_t add = 0;
    uint32_t sub = 0;
    ENCODE(add, enc.Add(c18, c19, Operand(imm)));
    ENCODE(sub, enc.Sub(c18, c19, Operand(imm)));
    const uint32_t field =
        (1u << 22) | (static_cast<uint32_t>(imm >> 12) << 10);
    CHECK_EQ(0x02000000u | field | kRegs, add);
    CHECK_EQ(1u << 23, add ^ sub);
  }

  // A negative immediate becomes the opposite direction, never a sign-extended
  // field.
  uint32_t add_neg = 0;
  uint32_t sub_pos = 0;
  uint32_t sub_neg = 0;
  uint32_t add_pos = 0;
  ENCODE(add_neg, enc.Add(c18, c19, Operand(-4096)));
  ENCODE(sub_pos, enc.Sub(c18, c19, Operand(4096)));
  ENCODE(sub_neg, enc.Sub(c18, c19, Operand(-4096)));
  ENCODE(add_pos, enc.Add(c18, c19, Operand(4096)));
  CHECK_EQ(sub_pos, add_neg);
  CHECK_EQ(add_pos, sub_neg);
}

// The shifted subtract sets both bit 22 and bit 23. If bit 23 were really part
// of the shift field rather than the add/sub selector, this round trip would
// land somewhere else.
TEST_F(CheriAssemblerArm64Test,
       capability_shifted_add_sub_immediate_round_trips) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Add(c19, c18, Operand(4095));      // imm12, unshifted
  __ Add(c20, c18, Operand(4096));      // sh = 1, imm12 = 1
  __ Add(c21, c18, Operand(16773120));  // sh = 1, imm12 = 4095
  __ Sub(c22, c21, Operand(16773120));  // the same immediate, subtracted
  END();
  RUN();

  CHECK_EQ(buf_addr + 4095, C_ADDR(c19));
  CHECK_EQ(buf_addr + 4096, C_ADDR(c20));
  CHECK_EQ(buf_addr + 16773120, C_ADDR(c21));
  CHECK_EQ(buf_addr, C_ADDR(c22));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Sealed sources.
// ---------------------------------------------------------------------------

// Derivation is monotonic and a sealed capability is non-derivable, so every
// instruction that takes Cn as its source has to reject it by clearing the tag,
// not only ADD_c. The rejection is of the tag only: the metadata still gets
// written, so the destination is the newly bounded region untagged.
TEST_F(CheriAssemblerArm64Test,
       sealed_source_blocks_every_transfer_and_bounds_instruction) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Seal(c19, c18, Cheri::kSealFormRb);
  __ Mov(x2, buf_addr + 32);
  __ Mov(x3, 64);
  __ Scvalue(c20, c19, x2);
  __ Scbnds(c21, c19, Operand(x3));
  __ Scbnds(c22, c19, Operand(16));
  __ scbnds_imm_scaled(c23, c19, 4);
  __ Scbndse(c24, c19, x3);
  __ AlignD(c25, c19, Operand(4));
  __ AlignU(c26, c19, Operand(4));
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(1u, C_SEALED(c19));
  CHECK_EQ(0u, C_TAG(c20));
  CHECK_EQ(0u, C_TAG(c21));
  CHECK_EQ(0u, C_TAG(c22));
  CHECK_EQ(0u, C_TAG(c23));
  CHECK_EQ(0u, C_TAG(c24));
  CHECK_EQ(0u, C_TAG(c25));
  CHECK_EQ(0u, C_TAG(c26));
  // The value transfer itself still happened.
  CHECK_EQ(buf_addr + 32, C_ADDR(c20));
  // "Blocked" means the tag is refused, not that the instruction is a no-op:
  // DDI0606 4.4.116 applies the new bounds and only then clears the tag for a
  // sealed source, so the destination carries the requested region untagged.
  CHECK_EQ(C_BASE(c19), C_BASE(c24));
  CHECK_EQ(X64(x3), C_LEN(c24));
  CHECK_EQ(64u, C_LEN(c21));
  CHECK_EQ(16u, C_LEN(c22));
  CHECK_EQ(64u, C_LEN(c23));
  // The sealed source survives its uses: SEAL is not consuming.
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_EQ(1u, C_SEALED(c19));
  CHECK(buf[0] != 0);
}

// ---------------------------------------------------------------------------
// Lowerings that fall out of the immediate ranges.
// ---------------------------------------------------------------------------

// A displacement outside both immediate classes is materialized in a register
// and used as the index. The fallback spells that index LSL #0, which for the
// capability class (DDI0606 4.4.137) selects the register's raw 64-bit value:
// the extend options are UXTW, LSL, SXTW, SXTX, and the address is a modular
// 64-bit add, so a negative value really does reach below the base.
TEST_F(CheriAssemblerArm64Test,
       out_of_range_displacement_falls_back_to_an_unscaled_register_index) {
  SETUP_ENCODER();
  enc.Reset();
  CHECK_EQ(0, enc.pc_offset());
  enc.Ldr(c18, MemOperand(c20, -4096));
  const int count = enc.pc_offset() / kInstrSize;
  CHECK_LT(1, count);  // -4096 fits neither immediate field.

  const uint32_t* words =
      reinterpret_cast<const uint32_t*>(enc.buffer_start());
  const uint32_t access = words[count - 1];
  // The capability register-offset class plus the load bit.
  CHECK_EQ(0xA2604800u, access & 0xFFE04C00u);
  // ExtendMode 15:13 = 0b011 (the raw 64-bit value), not 0b010 (UXTW).
  CHECK_EQ(3u, (access >> 13) & 7u);
  // The scale-by-16 bit is clear: the index is used unscaled.
  CHECK_EQ(0u, (access >> 12) & 1u);
}

// The other half of that claim, executed: each cell holds a marker capability
// whose address identifies the cell, so which one was read is observable. The
// base points at offset 4096 and the displacement is -4096, so the read has to
// land on the offset-0 marker.
TEST_F(CheriAssemblerArm64Test,
       negative_displacement_below_the_immediate_range_reaches_below_the_base) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  __ Add(c19, c18, Operand(2048));  // A marker that is not c18.
  __ Str(c19, MemOperand(c18, 0));
  __ Str(c18, MemOperand(c18, 4096));
  __ Add(c20, c18, Operand(4096));  // Base at the second cell.
  __ Ldr(c21, MemOperand(c20, -4096));
  __ Ldr(c22, MemOperand(c20, 0));  // Control: the cell the base points at.
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c20));
  CHECK_EQ(buf_addr + 4096, C_ADDR(c20));
  // The negative displacement wrapped down to the first cell.
  CHECK_EQ(1u, C_TAG(c21));
  CHECK_EQ(buf_addr + 2048, C_ADDR(c21));
  CHECK_EQ(1u, C_TAG(c22));
  CHECK_EQ(buf_addr, C_ADDR(c22));
}

// Push/Pop of three registers pairs one access with a single one, and the load
// order is the reverse of the store order: a round trip has to pop in the
// opposite register order to the push.
TEST_F(CheriAssemblerArm64Test,
       push_pop_three_capability_registers_round_trips_in_reverse_order) {
  alignas(16) uint64_t buf[1024];
  memset(buf, 0xAA, sizeof(buf));
  const uint64_t buf_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf));

  SETUP();
  START();
  __ Mov(c28, csp);
  CHERI_MAKE_CAP(c18, x0, x1, buf_addr, sizeof(buf));
  CHERI_MAKE_CAP(c19, x2, x3, buf_addr + 64, sizeof(buf) - 64);
  CHERI_MAKE_CAP(c20, x4, x5, buf_addr + 128, sizeof(buf) - 128);
  __ Mov(c21, c18);
  __ Mov(c22, c19);
  __ Mov(c23, c20);
  __ Push(c18, c19, c20);
  __ Mov(c18, c28);
  __ Mov(c19, c28);
  __ Mov(c20, c28);
  __ Pop(c20, c19, c18);
  __ Mov(c25, csp);
  __ Gcvalue(x6, c28);
  __ Gcvalue(x7, c25);
  END();
  RUN();

  CHECK_EQ(1u, C_TAG(c18));
  CHECK_CAP_SAME(c21, c18);
  CHECK_EQ(1u, C_TAG(c19));
  CHECK_CAP_SAME(c22, c19);
  CHECK_EQ(1u, C_TAG(c20));
  CHECK_CAP_SAME(c23, c20);
  CHECK_EQ(X64(x6), X64(x7));  // csp is back where it started.
  CHECK(buf[0] != 0);
}

// The register forms of Claim/Drop scale the count by the unit size, and unlike
// the immediate forms nothing checks that the result stays 16-byte aligned.
TEST_F(CheriAssemblerArm64Test,
       claim_and_drop_with_register_counts_scale_and_skip_the_alignment_check) {
  SETUP();
  START();
  __ Mov(c28, csp);
  __ Mov(x4, 4);
  __ Claim(x4, kCRegSize);
  __ Mov(c25, csp);
  __ Drop(x4, kCRegSize);
  __ Mov(x5, 3);
  __ Claim(x5, kXRegSize);  // 24 bytes, where the ABI wants 32.
  __ Mov(c26, csp);
  __ Drop(x5, kXRegSize);
  __ Gcvalue(x1, c28);
  __ Gcvalue(x2, c25);
  __ Gcvalue(x3, c26);
  __ Gcvalue(x6, csp);
  __ Gctag(x7, c26);
  END();
  RUN();

  const uint64_t base_sp = X64(x1);
  CHECK_EQ(base_sp - 4 * kCRegSize, X64(x2));
  CHECK_EQ(1u, X64(x7));  // csp stayed tagged throughout.
  CHECK_EQ(0u, X64(x2) % 16);
  // Three doublewords, not rounded up to four: nothing rejected the result.
  CHECK_EQ(base_sp - 3 * kXRegSize, X64(x3));
  CHECK_EQ(8u, X64(x3) % 16);
  CHECK_EQ(base_sp, X64(x6));  // Both Drops restored the frame.
}

#endif  // V8_TARGET_CHERI && __CHERI_PURE_CAPABILITY__ && !USE_SIMULATOR

}  // namespace internal
}  // namespace v8

// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Disassembler tests for the Morello capability instructions. Expected text is
// from Arm DDI0606 A.k section 4.4, cross-checked against llvm-mc, except that
// the acquire/release forms name their base Cn and CTPIDR prints by name.

#include <stdint.h>
#include <string.h>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/codegen/arm64/decoder-arm64-inl.h"
#include "src/codegen/arm64/macro-assembler-arm64-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/arm64/disasm-arm64.h"
#include "src/execution/simulator.h"
#include "test/common/assembler-tester.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

#if V8_TARGET_CHERI && defined(__CHERI_PURE_CAPABILITY__) && \
    !defined(USE_SIMULATOR)

namespace {

class MorelloDisassembler {
 public:
  MorelloDisassembler() { decoder_.AppendVisitor(&disassembler_); }

  const char* Decode(uint32_t encoding) {
    decoder_.Decode(reinterpret_cast<Instruction*>(&encoding));
    return disassembler_.GetOutput();
  }

 private:
  Decoder<DispatchingDecoderVisitor> decoder_;
  DisassemblingDecoder disassembler_;
};

struct MorelloDisassembly {
  uint32_t encoding;
  const char* text;
};

}  // namespace

using MorelloDisasmTest = TestWithIsolate;

// Two representatives -- lowest and highest mnemonic-bearing encoding -- of
// every (visitor, mnemonic) pair the capability decoder can produce, plus the
// register-31 aliases and the fields shared by the load/store forms.
static const MorelloDisassembly kMorelloDisassemblyTests[] = {
  // AddSubCapExtended
  {0xC2A00041, "add c1, c2, w0, uxtb"},
  {0xC2A0205F, "add csp, c2, w0, uxth"},
  {0xC2A047E1, "add c1, csp, w0, uxtw #1"},
  {0xC2A0805F, "add csp, c2, w0, sxtb"},
  {0xC2A0A7E1, "add c1, csp, w0, sxth #1"},
  {0xC2BFE041, "add c1, c2, xzr, sxtx"},
  {0xC2BFF041, "add c1, c2, xzr, sxtx #4"},
  // AddSubCapImmediate
  {0x02000041, "add c1, c2, #0"},
  {0x0200005F, "add csp, c2, #0"},
  {0x027FFC41, "add c1, c2, #4095, lsl #12"},
  {0x02800041, "sub c1, c2, #0"},
  {0x02DF0041, "sub c1, c2, #1984, lsl #12"},
  {0x02FFFC41, "sub c1, c2, #4095, lsl #12"},
  // AlignCapability
  {0xC2C01841, "alignd c1, c2, #0"},
  {0xC2DF1841, "alignd c1, c2, #62"},
  {0xC2DF9841, "alignd c1, c2, #63"},
  {0xC2C05841, "alignu c1, c2, #0"},
  {0xC2DFD841, "alignu c1, c2, #63"},
  {0xC2C03841, "scbnds c1, c2, #0"},
  {0xC2DF3841, "scbnds c1, c2, #62"},
  {0xC2DFF841, "scbnds c1, c2, #63, lsl #4"},
  // AtomicMemoryCap
  {0xA2A07C41, "cas c0, c1, [c2]"},
  {0xA2BF7C41, "cas czr, c1, [c2]"},
  {0xA2E07C41, "casa c0, c1, [c2]"},
  {0xA2FF7C41, "casa czr, c1, [c2]"},
  {0xA2E0FC41, "casal c0, c1, [c2]"},
  {0xA2FFFC41, "casal czr, c1, [c2]"},
  {0xA2FFFFFF, "casal czr, czr, [csp]"},
  {0xA2A0FC41, "casl c0, c1, [c2]"},
  {0xA2BFFC41, "casl czr, c1, [c2]"},
  {0xA23FC041, "ldapr c1, [c2]"},
  {0xA2208041, "swp c0, c1, [c2]"},
  {0xA23F8041, "swp czr, c1, [c2]"},
  {0xA2A08041, "swpa c0, c1, [c2]"},
  {0xA2BF8041, "swpa czr, c1, [c2]"},
  {0xA2E08041, "swpal c0, c1, [c2]"},
  {0xA2FF8041, "swpal czr, c1, [c2]"},
  {0xA2608041, "swpl c0, c1, [c2]"},
  {0xA27F8041, "swpl czr, c1, [c2]"},
  // BranchToRegisterCap
  {0xC2D01041, "blr [c2, #0]"},
  {0xC2DF1041, "blr [c2, #-128]"},
  {0xC2DF3041, "blr [c2, #-112]"},
  {0xC2DF7041, "blr [c2, #-80]"},
  {0xC2DF9041, "blr [c2, #-64]"},
  {0xC2DFB041, "blr [c2, #-48]"},
  {0xC2DFD041, "blr [c2, #-32]"},
  {0xC2DFF041, "blr [c2, #-16]"},
  // CapSystemRegister
  {0xC2900041, "mrs c1, #2, #0, #0, #0, #2"},
  {0xC29F0041, "mrs c1, #3, #7, #0, #0, #2"},
  {0xC29FD041, "mrs c1, #3, #7, #13, #0, #2"},
  {0xC29FFC41, "mrs c1, #3, #7, #15, #12, #2"},
  // The CTPIDR registers occupy (op1, op2) slots that are not contiguous.
  {0xC29BD041, "mrs c1, CTPIDR_EL0"},
  {0xC298D081, "mrs c1, CTPIDR_EL1"},
  {0xC29CD041, "mrs c1, CTPIDR_EL2"},
  {0xC29ED041, "mrs c1, CTPIDR_EL3"},
  {0xC2800041, "msr #2, #0, #0, #0, #2, c1"},
  {0xC28FFC41, "msr #3, #7, #15, #12, #2, c1"},
  // CapabilityArithmetic
  {0xC2E01841, "cvt c1, c2, x0"},
  {0xC2FF1841, "cvt c1, c2, xzr"},
  {0xC2E05841, "cvtz c1, c2, x0"},
  {0xC2FF5841, "cvtz c1, c2, xzr"},
  {0xC2E00841, "orrflgs c1, c2, #0"},
  {0xC2FF0841, "orrflgs c1, c2, #248"},
  {0xC2FFE841, "orrflgs c1, c2, #255"},
  // CapabilityFlags
  {0xC2E00041, "bicflgs c1, c2, #0"},
  {0xC2FF0041, "bicflgs c1, c2, #248"},
  {0xC2FFE041, "bicflgs c1, c2, #255"},
  {0xC2E01041, "eorflgs c1, c2, #0"},
  {0xC2FF1041, "eorflgs c1, c2, #248"},
  {0xC2FFF041, "eorflgs c1, c2, #255"},
  // CapabilityMisc
  {0xC2C02841, "bicflgs c1, c2, x0"},
  {0xC2DF2841, "bicflgs c1, c2, xzr"},
  {0xC2C00441, "build c1, c2, c0"},
  {0xC2DF0441, "build c1, c2, csp"},
  {0xC2C15041, "cfhi x1, c2"},
  {0xC2C0A441, "chkeq c2, c0"},
  {0xC2DFA441, "chkeq c2, czr"},
  {0xC2C21041, "chksld c2"},
  {0xC2C08441, "chkss c2, c0"},
  {0xC2C087E1, "chkss csp, c0"},
  {0xC2DF8441, "chkss c2, csp"},
  {0xC2C08841, "chkssu c1, c2, c0"},
  {0xC2DF8841, "chkssu c1, c2, csp"},
  {0xC2C23041, "chktgd c2"},
  {0xC2C61041, "clrperm c1, c2, #0"},
  {0xC2C6F041, "clrperm c1, c2, rwx"},
  {0xC2C19041, "clrtag c1, c2"},
  {0xC2C1905F, "clrtag csp, c2"},
  {0xC2C1921F, "clrtag csp, c16"},
  {0xC2C193FF, "clrtag csp, csp"},
  {0xC2C02441, "cpytype c1, c2, c0"},
  {0xC2DF2441, "cpytype c1, c2, czr"},
  {0xC2C06441, "cpyvalue c1, c2, c0"},
  {0xC2DF6441, "cpyvalue c1, c2, czr"},
  {0xC2C04441, "cseal c1, c2, c0"},
  {0xC2DF4441, "cseal c1, c2, csp"},
  {0xC2C0E841, "cthi c1, c2, x0"},
  {0xC2DFE841, "cthi c1, c2, xzr"},
  {0xC2C51041, "cvtd x1, c2"},
  {0xC2C59041, "cvtd c1, x2"},
  {0xC2C5D041, "cvtdz c1, x2"},
  {0xC2C53041, "cvtp x1, c2"},
  {0xC2C5B041, "cvtp c1, x2"},
  {0xC2C5F041, "cvtpz c1, x2"},
  {0xC2C0A841, "eorflgs c1, c2, x0"},
  {0xC2DFA841, "eorflgs c1, c2, xzr"},
  {0xC2C01041, "gcbase x1, c2"},
  {0xC2C13041, "gcflgs x1, c2"},
  {0xC2C03041, "gclen x1, c2"},
  {0xC2C11041, "gclim x1, c2"},
  {0xC2C07041, "gcoff x1, c2"},
  {0xC2C0D041, "gcperm x1, c2"},
  {0xC2C0B041, "gcseal x1, c2"},
  {0xC2C09041, "gctag x1, c2"},
  {0xC2C0F041, "gctype x1, c2"},
  {0xC2C05041, "gcvalue x1, c2"},
  {0xC2C4B041, "ldct x1, [c2]"},
  {0xC2C43041, "ldpblr c1, [c2]"},
  {0xC2C41041, "ldpbr c1, [c2]"},
  {0xC2C1D041, "mov c1, c2"},
  {0xC2C1D21F, "mov csp, c16"},
  {0xC2C1D3FD, "mov c29, csp"},
  {0xC2C1D3FF, "mov csp, csp"},
  {0xC2C06841, "orrflgs c1, c2, x0"},
  {0xC2DF6841, "orrflgs c1, c2, xzr"},
  {0xC2C71041, "rrlen x1, x2"},
  {0xC2C73041, "rrmask x1, x2"},
  {0xC2C00841, "seal c1, c2, c0"},
  {0xC2DF0841, "seal c1, c2, czr"},
  {0xC2C49041, "stct x1, [c2]"},
  {0xC2C04841, "unseal c1, c2, c0"},
  {0xC2DF4841, "unseal c1, c2, czr"},
  // CompareCapabilities
  {0xC2E09841, "subs x1, c2, c0"},
  {0xC2FF9841, "subs x1, c2, czr"},
  // ConditionalSelectCap
  {0xC2C00C41, "csel c1, c2, c0, eq"},
  {0xC2C17C41, "csel c1, c2, c1, vc"},
  {0xC2D07C41, "csel c1, c2, c16, vc"},
  {0xC2DF0C41, "csel c1, c2, czr, eq"},
  {0xC2DF1C41, "csel c1, c2, czr, ne"},
  {0xC2DF2C41, "csel c1, c2, czr, hs"},
  {0xC2DF3C41, "csel c1, c2, czr, lo"},
  {0xC2DF6C41, "csel c1, c2, czr, vs"},
  {0xC2DFBC41, "csel c1, c2, czr, lt"},
  {0xC2DFCC41, "csel c1, c2, czr, gt"},
  {0xC2DFFC41, "csel c1, c2, czr, nv"},
  // LoadLiteralCap
  {0x82000041, "ldr c1, #32"},
  {0x823FFC41, "ldr c1, #-480"},
  // LoadStoreCapAcquireRelease
  {0x425F7C41, "ldar c1, [c2]"},
  {0x427FFC41, "ldar w1, [x2]"},
  {0x427F7C41, "ldarb w1, [x2]"},
  {0x421F7C41, "stlr c1, [c2]"},
  {0x423FFC41, "stlr w1, [x2]"},
  {0x423F7C41, "stlrb w1, [x2]"},
  // LoadStoreCapExclusive
  {0x227F8041, "ldaxp c1, c0, [c2]"},
  {0x227FFC41, "ldaxp c1, czr, [c2]"},
  {0x225FFC41, "ldaxr c1, [c2]"},
  {0x227F0041, "ldxp c1, c0, [c2]"},
  {0x227F7C41, "ldxp c1, czr, [c2]"},
  {0x225F7C41, "ldxr c1, [c2]"},
  {0x22208041, "stlxp w0, c1, c0, [c2]"},
  {0x223FFC41, "stlxp wzr, c1, czr, [c2]"},
  {0x2200FC41, "stlxr w0, c1, [c2]"},
  {0x221FFC41, "stlxr wzr, c1, [c2]"},
  {0x22200041, "stxp w0, c1, c0, [c2]"},
  {0x223F0041, "stxp wzr, c1, c0, [c2]"},
  {0x223F7C41, "stxp wzr, c1, czr, [c2]"},
  {0x22007C41, "stxr w0, c1, [c2]"},
  {0x221F7C41, "stxr wzr, c1, [c2]"},
  // LoadStoreCapRegisterOffsetAltBase
  {0xC2E04C41, "ldr c1, [x2, w0, uxtw]"},
  {0xC2FFFC41, "ldr c1, [x2, xzr, sxtx #4]"},
  {0xC2E04441, "str c1, [x2, w0, uxtw]"},
  {0xC2FFF441, "str c1, [x2, xzr, sxtx #4]"},
  // LoadStoreCapRegisterOffsetNormal
  {0xA2604841, "ldr c1, [c2, w0, uxtw]"},
  {0xA27FF841, "ldr c1, [c2, xzr, sxtx #4]"},
  {0xA2204841, "str c1, [c2, w0, uxtw]"},
  {0xA23FF841, "str c1, [c2, xzr, sxtx #4]"},
  // LoadStoreCapUnscaledOffsetNormal
  {0xA2400041, "ldur c1, [c2, #0]"},
  {0xA25FF041, "ldur c1, [c2, #-1]"},
  {0xA2000041, "stur c1, [c2, #0]"},
  {0xA21FF041, "stur c1, [c2, #-1]"},
  // LoadStoreCapUnscaledOffsetTranslated
  {0xA2400841, "ldtr c1, [c2, #0]"},
  {0xA25F0841, "ldtr c1, [c2, #-256]"},
  {0xA25FF841, "ldtr c1, [c2, #-16]"},
  {0xA2000841, "sttr c1, [c2, #0]"},
  {0xA21F0841, "sttr c1, [c2, #-256]"},
  {0xA21FF841, "sttr c1, [c2, #-16]"},
  // LoadStoreCapUnsignedOffsetAltBase
  {0x82600041, "ldr c1, [x2, #0]"},
  {0x827F0041, "ldr c1, [x2, #7936]"},
  {0x827FFC41, "ldr x1, [x2, #4088]"},
  {0x82600441, "ldrb w1, [x2, #0]"},
  {0x827FF441, "ldrb w1, [x2, #511]"},
  {0x82400041, "str c1, [x2, #0]"},
  {0x825F0041, "str c1, [x2, #7936]"},
  {0x825FFC41, "str x1, [x2, #4088]"},
  {0x82400441, "strb w1, [x2, #0]"},
  {0x825FF441, "strb w1, [x2, #511]"},
  // LoadStoreCapUnsignedOffsetCapNormal
  {0xC2400041, "ldr c1, [c2, #0]"},
  {0xC27FFC41, "ldr c1, [c2, #65520]"},
  {0xC2000041, "str c1, [c2, #0]"},
  {0xC23FFC41, "str c1, [c2, #65520]"},
  // LoadStorePairCapNonTemporal
  {0x62400041, "ldnp c1, c0, [c2, #0]"},
  {0x627FFC41, "ldnp c1, czr, [c2, #-16]"},
  {0x62000041, "stnp c1, c0, [c2, #0]"},
  {0x621F0041, "stnp c1, c0, [c2, #992]"},
  {0x623FFC41, "stnp c1, czr, [c2, #-16]"},
  // LoadStorePairCapOffset
  {0x42C00041, "ldp c1, c0, [c2, #0]"},
  {0x42DF0041, "ldp c1, c0, [c2, #992]"},
  {0x42FFFC41, "ldp c1, czr, [c2, #-16]"},
  {0x42800041, "stp c1, c0, [c2, #0]"},
  {0x42BFFC41, "stp c1, czr, [c2, #-16]"},
  // LoadStorePairCapPostIndex
  {0x22C00041, "ldp c1, c0, [c2], #0"},
  {0x22DF0041, "ldp c1, c0, [c2], #992"},
  {0x22FFFC41, "ldp c1, czr, [c2], #-16"},
  {0x22800041, "stp c1, c0, [c2], #0"},
  {0x22BFFC41, "stp c1, czr, [c2], #-16"},
  // LoadStorePairCapPreIndex
  {0x62C00041, "ldp c1, c0, [c2, #0]!"},
  {0x62FFFC41, "ldp c1, czr, [c2, #-16]!"},
  {0x62800041, "stp c1, c0, [c2, #0]!"},
  {0x629F0041, "stp c1, c0, [c2, #992]!"},
  {0x62BFFC41, "stp c1, czr, [c2, #-16]!"},
  // LoadStorePostCapIndex
  {0xA2400441, "ldr c1, [c2], #0"},
  {0xA25F0441, "ldr c1, [c2], #-256"},
  {0xA25FF441, "ldr c1, [c2], #-16"},
  {0xA2000441, "str c1, [c2], #0"},
  {0xA21F0441, "str c1, [c2], #-256"},
  {0xA21FF441, "str c1, [c2], #-16"},
  // LoadStorePreCapIndex
  {0xA2400C41, "ldr c1, [c2, #0]!"},
  {0xA25FFC41, "ldr c1, [c2, #-16]!"},
  {0xA2000C41, "str c1, [c2, #0]!"},
  {0xA21FFC41, "str c1, [c2, #-16]!"},
  // LoadStoreRegisterOffsetAltBase
  {0x82E04041, "ldr w1, [x2, w0, uxtw]"},
  {0x82FF4041, "ldr w1, [x2, wzr, uxtw]"},
  {0x82FF7041, "ldr w1, [x2, xzr, lsl #2]"},
  {0x82FFC041, "ldr w1, [x2, wzr, sxtw]"},
  {0x82FFE041, "ldr w1, [x2, xzr, sxtx]"},
  {0x82FFFC41, "ldr s1, [x2, xzr, sxtx #2]"},
  {0x82C04041, "ldrb w1, [x2, w0, uxtw]"},
  {0x82DF4041, "ldrb w1, [x2, wzr, uxtw]"},
  {0x82DF7041, "ldrb w1, [x2, xzr, lsl #0]"},
  {0x82DFC041, "ldrb w1, [x2, wzr, sxtw]"},
  {0x82DFE041, "ldrb w1, [x2, xzr, sxtx]"},
  {0x82DFF041, "ldrb w1, [x2, xzr, sxtx #0]"},
  {0x82C04C41, "ldrh w1, [x2, w0, uxtw]"},
  {0x82DF4C41, "ldrh w1, [x2, wzr, uxtw]"},
  {0x82DFDC41, "ldrh w1, [x2, wzr, sxtw #1]"},
  {0x82DFEC41, "ldrh w1, [x2, xzr, sxtx]"},
  {0x82DFFC41, "ldrh w1, [x2, xzr, sxtx #1]"},
  {0x82804441, "ldrsb x1, [x2, w0, uxtw]"},
  {0x829F4441, "ldrsb x1, [x2, wzr, uxtw]"},
  {0x829F7441, "ldrsb x1, [x2, xzr, lsl #0]"},
  {0x829FC441, "ldrsb x1, [x2, wzr, sxtw]"},
  {0x829FE441, "ldrsb x1, [x2, xzr, sxtx]"},
  {0x82DFF441, "ldrsb w1, [x2, xzr, sxtx #0]"},
  {0x82804841, "ldrsh x1, [x2, w0, uxtw]"},
  {0x829F4841, "ldrsh x1, [x2, wzr, uxtw]"},
  {0x829F7841, "ldrsh x1, [x2, xzr, lsl #1]"},
  {0x829FC841, "ldrsh x1, [x2, wzr, sxtw]"},
  {0x829FE841, "ldrsh x1, [x2, xzr, sxtx]"},
  {0x82DFF841, "ldrsh w1, [x2, xzr, sxtx #1]"},
  {0x82A04041, "str w1, [x2, w0, uxtw]"},
  {0x82BF4041, "str w1, [x2, wzr, uxtw]"},
  {0x82BF7041, "str w1, [x2, xzr, lsl #2]"},
  {0x82BFC041, "str w1, [x2, wzr, sxtw]"},
  {0x82BFE041, "str w1, [x2, xzr, sxtx]"},
  {0x82BFFC41, "str s1, [x2, xzr, sxtx #2]"},
  {0x82804041, "strb w1, [x2, w0, uxtw]"},
  {0x829F4041, "strb w1, [x2, wzr, uxtw]"},
  {0x829F7041, "strb w1, [x2, xzr, lsl #0]"},
  {0x829FC041, "strb w1, [x2, wzr, sxtw]"},
  {0x829FE041, "strb w1, [x2, xzr, sxtx]"},
  {0x829FF041, "strb w1, [x2, xzr, sxtx #0]"},
  {0x82804C41, "strh w1, [x2, w0, uxtw]"},
  {0x829F4C41, "strh w1, [x2, wzr, uxtw]"},
  {0x829FDC41, "strh w1, [x2, wzr, sxtw #1]"},
  {0x829FEC41, "strh w1, [x2, xzr, sxtx]"},
  {0x829FFC41, "strh w1, [x2, xzr, sxtx #1]"},
  // LoadStoreUnscaledOffsetAltBase
  {0xE2200C41, "ldur q1, [x2, #0]"},
  {0xE2FFF441, "ldur d1, [x2, #-1]"},
  {0xE2000441, "ldurb w1, [x2, #0]"},
  {0xE21F0441, "ldurb w1, [x2, #-16]"},
  {0xE21FF441, "ldurb w1, [x2, #-1]"},
  {0xE2400441, "ldurh w1, [x2, #0]"},
  {0xE25FF441, "ldurh w1, [x2, #-1]"},
  {0xE2000841, "ldursb x1, [x2, #0]"},
  {0xE21F0841, "ldursb x1, [x2, #-16]"},
  {0xE21FFC41, "ldursb w1, [x2, #-1]"},
  {0xE2400841, "ldursh x1, [x2, #0]"},
  {0xE25FFC41, "ldursh w1, [x2, #-1]"},
  {0xE2800841, "ldursw x1, [x2, #0]"},
  {0xE29F0841, "ldursw x1, [x2, #-16]"},
  {0xE29FF841, "ldursw x1, [x2, #-1]"},
  {0xE2200041, "stur b1, [x2, #0]"},
  {0xE2FFF041, "stur d1, [x2, #-1]"},
  {0xE2000041, "sturb w1, [x2, #0]"},
  {0xE21F0041, "sturb w1, [x2, #-16]"},
  {0xE21FF041, "sturb w1, [x2, #-1]"},
  {0xE2400041, "sturh w1, [x2, #0]"},
  {0xE25FF041, "sturh w1, [x2, #-1]"},
  // SetField1
  {0xC2C0A041, "clrperm c1, c2, x0"},
  {0xC2DFA041, "clrperm c1, c2, xzr"},
  {0xC2C0C041, "cvt x1, c2, c0"},
  {0xC2DFC041, "cvt x1, c2, czr"},
  {0xC2C00041, "scbnds c1, c2, x0"},
  {0xC2DF0041, "scbnds c1, c2, xzr"},
  {0xC2C02041, "scbndse c1, c2, x0"},
  {0xC2DF2041, "scbndse c1, c2, xzr"},
  {0xC2C0E041, "scflgs c1, c2, x0"},
  {0xC2DFE041, "scflgs c1, c2, xzr"},
  {0xC2C06041, "scoff c1, c2, x0"},
  {0xC2DF6041, "scoff c1, c2, xzr"},
  {0xC2C08041, "sctag c1, c2, x0"},
  {0xC2DF8041, "sctag c1, c2, xzr"},
  {0xC2C04041, "scvalue c1, c2, x0"},
  {0xC2DF4041, "scvalue c1, c2, xzr"},

  // RES0 fields must hold their mandated value: these would decode as
  // instructions if Rt were the required 1 rather than 31.
  {0xC2C0A45F, "unallocated (Unallocated)"},
  {0xC2C0845F, "unallocated (Unallocated)"},
  {0xC2C2105F, "unallocated (Unallocated)"},
  {0xC2C2305F, "unallocated (Unallocated)"},
  {0xC2D0105F, "unallocated (Unallocated)"},
};

TEST_F(MorelloDisasmTest, DecodesMorelloEncodings) {
  MorelloDisassembler disassembler;
  for (const MorelloDisassembly& test : kMorelloDisassemblyTests) {
    EXPECT_STREQ(test.text, disassembler.Decode(test.encoding))
        << "encoding 0x" << std::hex << test.encoding;
  }
}

// Emits each instruction through the assembler, checks the word against the
// golden encoding, then disassembles that word. A mistake in either the encoder
// or the decoder shows up as a mismatch even if the two agree with each other.
TEST_F(MorelloDisasmTest, RoundTripsAssemblerOutput) {
  Isolate* isolate = i_isolate();
  CHECK_NOT_NULL(isolate);
  auto owned_buf = AllocateAssemblerBuffer(4096);
  MacroAssembler enc(isolate, CodeObjectRequired::kYes,
                     owned_buf->CreateView());
  HandleScope handle_scope(isolate);
  // swp_c and cas_c DCHECK that the LSE feature is enabled.
  CpuFeatureScope lse_scope(&enc, LSE, CpuFeatureScope::kDontCheckSupported);
  MorelloDisassembler disassembler;

#define COMPARE(STMT, EXP_WORD, EXP_TEXT)                               \
  do {                                                                  \
    enc.Reset();                                                        \
    CHECK_EQ(0, enc.pc_offset());                                       \
    enc.STMT;                                                           \
    CHECK_EQ(kInstrSize, enc.pc_offset());                              \
    uint32_t encoding =                                                 \
        *reinterpret_cast<const uint32_t*>(enc.buffer_start());          \
    EXPECT_EQ(EXP_WORD, encoding) << #STMT;                             \
    EXPECT_STREQ(EXP_TEXT, disassembler.Decode(encoding)) << #STMT;     \
  } while (false)

  // Immediate forms: the align, bounds and flag immediates all sit in
  // different bit fields, so a swapped field is caught by the word alone.
  COMPARE(alignd(c1, c2, Operand(0)), 0xC2C01841, "alignd c1, c2, #0");
  COMPARE(alignd(c1, c2, Operand(63)), 0xC2DF9841, "alignd c1, c2, #63");
  COMPARE(alignu(c1, c2, Operand(1)), 0xC2C0D841, "alignu c1, c2, #1");
  COMPARE(alignu(c1, c2, Operand(42)), 0xC2D55841, "alignu c1, c2, #42");
  COMPARE(scbnds_imm_scaled(c1, c2, 1), 0xC2C0F841,
          "scbnds c1, c2, #1, lsl #4");
  COMPARE(scbnds_imm_scaled(c1, c2, 63), 0xC2DFF841,
          "scbnds c1, c2, #63, lsl #4");
  COMPARE(scbnds_imm_unscaled(c1, c2, 1), 0xC2C0B841, "scbnds c1, c2, #1");
  COMPARE(scbnds_imm_unscaled(c1, c2, 63), 0xC2DFB841, "scbnds c1, c2, #63");

  COMPARE(scbnds_reg(c1, c2, x3), 0xC2C30041, "scbnds c1, c2, x3");
  COMPARE(scbndse(c1, c2, x3), 0xC2C32041, "scbndse c1, c2, x3");
  COMPARE(scvalue(c1, c2, x3), 0xC2C34041, "scvalue c1, c2, x3");
  COMPARE(build(c1, c2, c3), 0xC2C30441, "build c1, c2, c3");

  COMPARE(seal(c1, c2, Cheri::kSealFormRb), 0xC2C33041, "seal c1, c2, rb");
  COMPARE(seal(c1, c2, Cheri::kSealFormLpb), 0xC2C35041, "seal c1, c2, lpb");
  COMPARE(seal(c1, c2, Cheri::kSealFormLb), 0xC2C37041, "seal c1, c2, lb");
  COMPARE(seal(c1, c2, c3), 0xC2C30841, "seal c1, c2, c3");

  COMPARE(cselc(c1, c2, c3, eq), 0xC2C30C41, "csel c1, c2, c3, eq");
  COMPARE(cselc(c1, c2, c3, ne), 0xC2C31C41, "csel c1, c2, c3, ne");
  COMPARE(cselc(c1, c2, c3, hs), 0xC2C32C41, "csel c1, c2, c3, hs");
  COMPARE(chktgd(c2), 0xC2C23041, "chktgd c2");
  COMPARE(capmov(c1, c2), 0xC2C1D041, "mov c1, c2");

  COMPARE(gcbase(x1, c2), 0xC2C01041, "gcbase x1, c2");
  COMPARE(gclen(x1, c2), 0xC2C03041, "gclen x1, c2");
  COMPARE(gcvalue(x1, c2), 0xC2C05041, "gcvalue x1, c2");
  COMPARE(gctag(x1, c2), 0xC2C09041, "gctag x1, c2");
  COMPARE(gcseal(x1, c2), 0xC2C0B041, "gcseal x1, c2");
  COMPARE(gcperm(x1, c2), 0xC2C0D041, "gcperm x1, c2");
  COMPARE(subsc(x1, c2, Operand(c3)), 0xC2E39841, "subs x1, c2, c3");

  // Capability memory accesses. The acquire/release and exclusive forms put
  // RES0 register fields at 31, so the round trip pins those bits too.
  COMPARE(ldar_c(c1, c2), 0x425FFC41, "ldar c1, [c2]");
  COMPARE(stlr_c(c1, c2), 0x421FFC41, "stlr c1, [c2]");
  COMPARE(ldaxr_c(c1, c2), 0x225FFC41, "ldaxr c1, [c2]");
  COMPARE(stlxr_c(w3, c1, c2), 0x2203FC41, "stlxr w3, c1, [c2]");
  COMPARE(swp_c(c3, c1, MemOperand(c2)), 0xA2238041, "swp c3, c1, [c2]");
  COMPARE(swpa_c(c3, c1, MemOperand(c2)), 0xA2A38041, "swpa c3, c1, [c2]");
  COMPARE(cas_c(c3, c1, MemOperand(c2)), 0xA2A37C41, "cas c3, c1, [c2]");
  COMPARE(casa_c(c3, c1, MemOperand(c2)), 0xA2E37C41, "casa c3, c1, [c2]");
  COMPARE(casl_c(c3, c1, MemOperand(c2)), 0xA2A3FC41, "casl c3, c1, [c2]");
  COMPARE(casal_c(c3, c1, MemOperand(c2)), 0xA2E3FC41, "casal c3, c1, [c2]");

  COMPARE(add(c1, c2, Operand(0)), 0x02000041, "add c1, c2, #0");
  COMPARE(add(c1, c2, Operand(1)), 0x02000441, "add c1, c2, #1");
  COMPARE(add(c1, c2, Operand(4095 << 12)), 0x027FFC41,
          "add c1, c2, #4095, lsl #12");
  COMPARE(sub(c1, c2, Operand(1)), 0x02800441, "sub c1, c2, #1");

  // Alternate-base load: the 9-bit immediate is scaled by the access size in
  // the disassembly, so the word and the text diverge by 16x.
  COMPARE(ldr_alternate(c1, x2, 8), 0x82608041, "ldr c1, [x2, #128]");
  COMPARE(ldr_alternate(c1, x2, 255), 0x826FF041, "ldr c1, [x2, #4080]");
  COMPARE(ldr_alternate(c1, x2, 511), 0x827FF041, "ldr c1, [x2, #8176]");

  // The generic entry points, as the register allocator and macro assembler
  // call them: a missing capability branch would emit a plain A64 instruction.
  // A 16-byte aligned offset takes the unsigned form, any other the unscaled
  // one; 65520 and -256 are the respective field limits.
  COMPARE(ldr(c1, MemOperand(c2, 16)), 0xC2400441, "ldr c1, [c2, #16]");
  COMPARE(str(c1, MemOperand(c2, 16)), 0xC2000441, "str c1, [c2, #16]");
  COMPARE(ldr(c1, MemOperand(c2, 65520)), 0xC27FFC41, "ldr c1, [c2, #65520]");
  COMPARE(ldr(c1, MemOperand(c2, 8)), 0xA2408041, "ldur c1, [c2, #8]");
  COMPARE(str(c1, MemOperand(c2, -16)), 0xA21F0041, "stur c1, [c2, #-16]");
  COMPARE(str(c1, MemOperand(c2, -256)), 0xA2100041, "stur c1, [c2, #-256]");
  COMPARE(ldr(c1, MemOperand(c2, x3)), 0xA2636841, "ldr c1, [c2, x3]");
  COMPARE(str(c1, MemOperand(c2, x3)), 0xA2236841, "str c1, [c2, x3]");
  COMPARE(ldr(c1, MemOperand(c2, x3, LSL, 4)), 0xA2637841,
          "ldr c1, [c2, x3, lsl #4]");
  COMPARE(str(c1, MemOperand(c2, x3, LSL, 4)), 0xA2237841,
          "str c1, [c2, x3, lsl #4]");
  COMPARE(ldr(c1, MemOperand(c2, x3, UXTW, 4)), 0xA2635841,
          "ldr c1, [c2, w3, uxtw #4]");
  COMPARE(ldr(c1, MemOperand(c2, 16, PreIndex)), 0xA2401C41,
          "ldr c1, [c2, #16]!");
  COMPARE(str(c1, MemOperand(c2, 16, PreIndex)), 0xA2001C41,
          "str c1, [c2, #16]!");
  COMPARE(ldr(c1, MemOperand(c2, 16, PostIndex)), 0xA2401441,
          "ldr c1, [c2], #16");
  COMPARE(str(c1, MemOperand(c2, 16, PostIndex)), 0xA2001441,
          "str c1, [c2], #16");
  COMPARE(ldp(c1, c3, MemOperand(c2, 16)), 0x42C08C41, "ldp c1, c3, [c2, #16]");
  COMPARE(stp(c1, c3, MemOperand(c2, 16)), 0x42808C41, "stp c1, c3, [c2, #16]");
  COMPARE(ldp(c1, c3, MemOperand(c2, 16, PreIndex)), 0x62C08C41,
          "ldp c1, c3, [c2, #16]!");
  COMPARE(stp(c1, c3, MemOperand(c2, 16, PostIndex)), 0x22808C41,
          "stp c1, c3, [c2], #16");
  COMPARE(ldar(c1, c2), 0x425FFC41, "ldar c1, [c2]");
  COMPARE(stlxr(w3, c1, c2), 0x2203FC41, "stlxr w3, c1, [c2]");
  COMPARE(csel(c1, c2, c3, ne), 0xC2C31C41, "csel c1, c2, c3, ne");

  // Add (extended register): the shift amount is the extension amount, and
  // UXTX is named here where the load/store forms fold it into the class.
  COMPARE(add(c1, c2, Operand(x3, UXTX, 0)), 0xC2A36041,
          "add c1, c2, x3, uxtx");
  COMPARE(add(c1, c2, Operand(x3, UXTX, 4)), 0xC2A37041,
          "add c1, c2, x3, uxtx #4");
  COMPARE(add(c1, c2, Operand(x3, UXTW, 2)), 0xC2A34841,
          "add c1, c2, w3, uxtw #2");

  // Load literal: the 17-bit immediate is scaled by 16.
  COMPARE(ldr_pcrel(c1, 1), 0x82000021, "ldr c1, #16");
  COMPARE(ldr_pcrel(c1, -1), 0x823FFFE1, "ldr c1, #-16");

  // Pairs, including the index writeback modes.
  COMPARE(ldpc(c1, c3, MemOperand(c2, 16)), 0x42C08C41,
          "ldp c1, c3, [c2, #16]");
  COMPARE(ldpc(c1, c3, MemOperand(c2, -32)), 0x42FF0C41,
          "ldp c1, c3, [c2, #-32]");
  COMPARE(stpc(c1, c3, MemOperand(c2, 64)), 0x42820C41,
          "stp c1, c3, [c2, #64]");
  COMPARE(ldpc(c1, c3, MemOperand(c2, 16, PreIndex)), 0x62C08C41,
          "ldp c1, c3, [c2, #16]!");
  COMPARE(stpc(c1, c3, MemOperand(c2, 16, PostIndex)), 0x22808C41,
          "stp c1, c3, [c2], #16");

#undef COMPARE
}

// Sweep the capability encoding space: op0 in bits 31:29, operation in 23:10,
// bits 28:24 fixed. Rt and Rt2 hold 1 and 2, the RES0 fields' allocated
// values. Nothing may reach the "unimplemented" catch-all, and the 35279 A.k
// reserved words must all report as unallocated.
TEST_F(MorelloDisasmTest, DecodesEveryMorelloEncoding) {
  MorelloDisassembler disassembler;
  int unallocated = 0;
  int unimplemented = 0;
  uint32_t first_unimplemented = 0;
  for (uint32_t op0 = 0; op0 < 8; op0++) {
    for (uint32_t field = 0; field < (1u << 14); field++) {
      uint32_t encoding =
          (op0 << 29) | (0x2u << 24) | (field << 10) | (2u << 5) | 1u;
      const char* text = disassembler.Decode(encoding);
      if (strcmp(text, "unallocated (Unallocated)") == 0) {
        unallocated++;
      } else if (strncmp(text, "unimplemented", 13) == 0) {
        if (unimplemented == 0) first_unimplemented = encoding;
        unimplemented++;
      }
    }
  }
  EXPECT_EQ(0, unimplemented)
      << "first unimplemented encoding: 0x" << std::hex << first_unimplemented;
  EXPECT_EQ(35279, unallocated);
}

#endif  // V8_TARGET_CHERI && __CHERI_PURE_CAPABILITY__ && !USE_SIMULATOR

}  // namespace internal
}  // namespace v8

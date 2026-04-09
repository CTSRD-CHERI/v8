// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_MACRO_ASSEMBLER_ARM64_INL_H_
#define V8_CODEGEN_ARM64_MACRO_ASSEMBLER_ARM64_INL_H_

#include <ctype.h>

#include "src/base/bits.h"
#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/codegen/arm64/assembler-arm64.h"
#include "src/codegen/macro-assembler.h"
#include "src/common/globals.h"
#include "src/execution/isolate-data.h"

namespace v8 {
namespace internal {

MemOperand FieldMemOperand(Register object, int offset) {
  return MemOperand(object, offset - kHeapObjectTag);
}

void MacroAssembler::And(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
#if V8_TARGET_CHERI
  if (rn.IsC()) {
    DCHECK(rd.IsC());
    if (AreAliased(rn, rd) || rd.IsSP()) {
      UseScratchRegisterScope temps(this);
      Register temp = temps.AcquireX();
      DCHECK(!AreAliased(rn, temp));
      LogicalMacro(temp, rn.X(), operand, AND);
      Scvalue(rd, rn, temp);
    } else {
      LogicalMacro(rd.X(), rn.X(), operand, AND);
      Scvalue(rd, rn, rd.X());
    }
    return;
  }
#endif
  LogicalMacro(rd, rn, operand, AND);
}

void MacroAssembler::Ands(const Register& rd, const Register& rn,
                          const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
#if V8_TARGET_CHERI
  if (rn.IsC()) {
    DCHECK(rd.IsC());
    if (AreAliased(rn, rd) || rd.IsSP()) {
      UseScratchRegisterScope temps(this);
      Register temp = temps.AcquireX();
      DCHECK(!AreAliased(rn, temp));
      LogicalMacro(temp, rn.X(), operand, ANDS);
      Scvalue(rd, rn, temp);
    } else {
      LogicalMacro(rd.X(), rn.X(), operand, ANDS);
      Scvalue(rd, rn, rd.X());
    }
    return;
  }
  DCHECK(!rd.IsC());
#endif
  LogicalMacro(rd, rn, operand, ANDS);
}

void MacroAssembler::Tst(const Register& rn, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  LogicalMacro(AppropriateZeroRegFor(integer_rn), integer_rn, integer_operand,
               ANDS);
}

void MacroAssembler::Bic(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rn.IsC());
  DCHECK(!rd.IsC());
  LogicalMacro(rd, rn, operand, BIC);
}

void MacroAssembler::Bics(const Register& rd, const Register& rn,
                          const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rn.IsC());
  DCHECK(!rd.IsC());
  LogicalMacro(rd, rn, operand, BICS);
}

void MacroAssembler::Orr(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
#if V8_TARGET_CHERI
  if (rn.IsC()) {
    DCHECK(rd.IsC());
    if (AreAliased(rn, rd) || rd.IsSP()) {
      UseScratchRegisterScope temps(this);
      Register temp = temps.AcquireX();
      DCHECK(!AreAliased(rn, temp));
      LogicalMacro(temp, rn.X(), operand, ORR);
      Scvalue(rd, rn, temp);
    } else {
      LogicalMacro(rd.X(), rn.X(), operand, ORR);
      Scvalue(rd, rn, rd.X());
    }
    return;
  }
  DCHECK(!rd.IsC());
#endif
  LogicalMacro(rd, rn, operand, ORR);
}

void MacroAssembler::Orn(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rn.IsC());
  DCHECK(!rd.IsC());
  LogicalMacro(rd, rn, operand, ORN);
}

void MacroAssembler::Eor(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rn.IsC());
  DCHECK(!rd.IsC());
  LogicalMacro(rd, rn, operand, EOR);
}

void MacroAssembler::Eon(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  LogicalMacro(rd, rn, operand, EON);
}

void MacroAssembler::Ccmp(const Register& rn, const Operand& operand,
                          StatusFlags nzcv, Condition cond) {
  DCHECK(allow_macro_instructions());
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  if (integer_operand.IsImmediate() && (integer_operand.ImmediateValue() < 0)) {
    ConditionalCompareMacro(integer_rn, -integer_operand.ImmediateValue(), nzcv,
                            cond, CCMN);
  } else {
    ConditionalCompareMacro(integer_rn, integer_operand, nzcv, cond, CCMP);
  }
}

void MacroAssembler::CcmpTagged(const Register& rn, const Operand& operand,
                                StatusFlags nzcv, Condition cond) {
  if (COMPRESS_POINTERS_BOOL) {
    Ccmp(rn.W(), operand.ToW(), nzcv, cond);
  } else {
    Ccmp(rn, operand, nzcv, cond);
  }
}

void MacroAssembler::Ccmn(const Register& rn, const Operand& operand,
                          StatusFlags nzcv, Condition cond) {
  DCHECK(allow_macro_instructions());
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  if (integer_operand.IsImmediate() && (integer_operand.ImmediateValue() < 0)) {
    ConditionalCompareMacro(integer_rn, -integer_operand.ImmediateValue(), nzcv,
                            cond, CCMP);
  } else {
    ConditionalCompareMacro(integer_rn, integer_operand, nzcv, cond, CCMN);
  }
}

void MacroAssembler::Add(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  if (operand.IsImmediate() && (operand.ImmediateValue() < 0) &&
      IsImmAddSub(-operand.ImmediateValue())) {
    AddSubMacro(rd, rn, -operand.ImmediateValue(), LeaveFlags, SubOpFor(rd));
  } else {
    AddSubMacro(rd, rn, operand, LeaveFlags, AddOpFor(rd));
  }
}

void MacroAssembler::Adds(const Register& rd, const Register& rn,
                          const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  if (operand.IsImmediate() && (operand.ImmediateValue() < 0) &&
      IsImmAddSub(-operand.ImmediateValue())) {
    AddSubMacro(rd, rn, -operand.ImmediateValue(), SetFlags, SUB);
  } else {
    DCHECK_IMPLIES(V8_TARGET_CHERI_BOOL && (operand.IsExtendedRegister() ||
                                            operand.IsShiftedRegister()),
                   !operand.reg().IsC());
    AddSubMacro(rd, rn, operand, SetFlags, ADD);
  }
}

#if V8_TARGET_CHERI
void MacroAssembler::Cpy(const Register& cd, const Register& cn) {
  DCHECK(allow_macro_instructions());
  DCHECK(cd.IsC());
  DCHECK(cn.IsC());
  cpy(cd, cn);
}

// Conditionally zero the destination capability register.
void MacroAssembler::CzeroC(const Register& cd, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!cd.IsSP() && cd.Is128Bits());
  DCHECK((cond != al) && (cond != nv));
  cselc(cd, czr, cd, cond);
}

void MacroAssembler::Chktgd(const Register& cn) {
  DCHECK(allow_macro_instructions());
  DCHECK(cn.Is128Bits());
  chktgd(cn);
}

void MacroAssembler::Gctag(const Register& rd, const Register& cn) {
  DCHECK(allow_macro_instructions());
  DCHECK(cn.Is128Bits());
  DCHECK(rd.Is64Bits());
  gctag(rd, cn);
}

void MacroAssembler::Gcvalue(const Register& rd, const Register& cn) {
  DCHECK(allow_macro_instructions());
  DCHECK(cn.Is128Bits());
  DCHECK(rd.Is64Bits());
  gcvalue(rd, cn);
}

void MacroAssembler::Gclen(const Register& rd, const Register& cn) {
  DCHECK(allow_macro_instructions());
  DCHECK(cn.Is128Bits());
  DCHECK(rd.Is64Bits());
  gclen(rd, cn);
}

void MacroAssembler::Gcbase(const Register& rd, const Register& cn) {
  DCHECK(allow_macro_instructions());
  DCHECK(cn.Is128Bits());
  DCHECK(rd.Is64Bits());
  gcbase(rd, cn);
}

void MacroAssembler::Gcseal(const Register& rd, const Register& cn) {
  DCHECK(allow_macro_instructions());
  DCHECK(cn.Is128Bits());
  DCHECK(rd.Is64Bits());
  gcseal(rd, cn);
}

void MacroAssembler::Scvalue(const Register& cd, const Register& cn,
                             const Register& rm) {
  DCHECK(allow_macro_instructions());
  scvalue(cd, cn, rm);
}

void MacroAssembler::Scbnds(const Register& cd, const Register& cn,
                            const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(cd.IsC());
  DCHECK(cn.IsC());
  if (operand.IsImmediate()) {
    uint64_t imm = static_cast<uint64_t>(operand.ImmediateValue());
    if (IsImmSBUnscaled(imm)) {
      scbnds_imm_unscaled(cd, cn, imm);
    } else if (IsImmSBScaled(imm)) {
      scbnds_imm_scaled(cd, cn, imm << 4);
    } else {
      UseScratchRegisterScope temps(this);
      Register temp = temps.AcquireX();
      Mov(temp, imm);
      scbnds_reg(cd, cn, temp);
    }
  } else {
    DCHECK(operand.reg().Is64Bits());
    scbnds_reg(cd, cn, operand.reg());
  }
}

void MacroAssembler::Scbndse(const Register& cd, const Register& cn,
                             const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(cd.Is128Bits());
  DCHECK(cn.Is128Bits());
  DCHECK(rm.Is64Bits());
  scbndse(cd, cn, rm);
}

void MacroAssembler::Build(const Register& cd, const Register& cn,
                           const Register& cm) {
  DCHECK(allow_macro_instructions());
  DCHECK(cd.Is128Bits());
  DCHECK(cn.Is128Bits());
  DCHECK(cm.Is128Bits());
  build(cd, cn, cm);
}

void MacroAssembler::Seal(const Register& cd, const Register& cn,
                          Cheri::SealImmediateForm form) {
  DCHECK(allow_macro_instructions());
  DCHECK(cd.Is128Bits());
  DCHECK(cn.Is128Bits());
  DCHECK_NE(form & 0b11, 0);
  seal(cd, cn, form);
}

void MacroAssembler::Seal(const Register& cd, const Register& cn,
                          const Register& cm) {
  DCHECK(allow_macro_instructions());
  DCHECK(cd.Is128Bits());
  DCHECK(cn.Is128Bits());
  DCHECK(cm.Is128Bits());
  seal(cd, cn, cm);
}

void MacroAssembler::Subsc(const Register& rd, const Register& cn,
                           const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(operand.reg().IsC());
  DCHECK(!operand.IsShiftedRegister());
  if (cn.code() == kSPRegInternalCode) {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireC();
    DCHECK(!AreAliased(operand.reg(), temp));
    Cpy(temp, cn);
    subsc(rd, temp, operand);
  } else {
    subsc(rd, cn, operand);
  }
}

void MacroAssembler::AlignU(const Register& cd, const Register& cn,
                            const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(operand.IsImmediate());
  alignu(cd, cn, operand);
}

void MacroAssembler::AlignD(const Register& cd, const Register& cn,
                            const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(operand.IsImmediate());
  alignd(cd, cn, operand);
}

void MacroAssembler::Sigprot(Register sentinel_register, int imm9) {
  ASM_CODE_COMMENT(this);
  DCHECK(is_uint9(imm9));
  ldr_alternate(sentinel_register, sentinel_register.X(), imm9);
}

void MacroAssembler::DebugAssertCapabilityIsTagged(const Register& cs) {
  DCHECK(allow_macro_instructions());
  if (v8_flags.debug_code) {
    UseScratchRegisterScope temps(this);
    Label out;
    Register temp = temps.AcquireX();
    Gctag(temp, cs);
    Tbnz(temp, 0, &out);
    DebugBreak();
    Bind(&out);
  }
}

void MacroAssembler::DebugAssertValidContext(const Register& cs) {
  DCHECK(allow_macro_instructions());
  if (v8_flags.debug_code) {
    Label out;
    Cmp(cs.X(), 0);
    B(eq, &out);
    DebugAssertCapabilityIsTagged(cs);
    Bind(&out);
  }
}
#else
void MacroAssembler::Cpy(const Register& cd, const Register& cn) {}
void MacroAssembler::CzeroC(const Register& cd, Condition cond) {
  CzeroX(cd, cond);
}
void MacroAssembler::Chktgd(const Register& cn) {}
void MacroAssembler::Gctag(const Register& rd, const Register& cn) {}
void MacroAssembler::Gcvalue(const Register& rd, const Register& cn) {}
void MacroAssembler::Gclen(const Register& rd, const Register& cn) {}
void MacroAssembler::Gcbase(const Register& rd, const Register& cn) {}
void MacroAssembler::Gcseal(const Register& rd, const Register& cn) {}
void MacroAssembler::Scvalue(const Register& cd, const Register& cn,
                             const Register& rm) {}
void MacroAssembler::Scbndse(const Register& cd, const Register& cn,
                             const Register& rm) {}
void MacroAssembler::Build(const Register& cd, const Register& cn,
                           const Register& cm) {}
void MacroAssembler::Seal(const Register& cd, const Register& cn,
                          Cheri::SealImmediateForm form) {}
void MacroAssembler::Subsc(const Register& rd, const Register& cn,
                           const Operand& operand) {}
void MacroAssembler::AlignU(const Register& cd, const Register& cn,
                            const Operand& operand) {}
void MacroAssembler::AlignD(const Register& cd, const Register& cn,
                            const Operand& operand) {}
void MacroAssembler::Sigprot(Register sentinel_register, int imm9) {}
void MacroAssembler::DebugAssertCapabilityIsTagged(const Register& cs) {}
void MacroAssembler::DebugAssertValidContext(const Register& cs) {}
#endif

void MacroAssembler::Sub(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  if (operand.IsImmediate() && (operand.ImmediateValue() < 0) &&
      IsImmAddSub(-operand.ImmediateValue())) {
    AddSubMacro(rd, rn, -operand.ImmediateValue(), LeaveFlags, AddOpFor(rd));
  } else {
    AddSubMacro(rd, rn, operand, LeaveFlags, SubOpFor(rd));
  }
}

void MacroAssembler::Subs(const Register& rd, const Register& rn,
                          const Operand& operand) {
  DCHECK(allow_macro_instructions());
  const Register& integer_rd = rd.IsC() ? rd.X() : rd;
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  if (integer_operand.IsImmediate() && (integer_operand.ImmediateValue() < 0) &&
      IsImmAddSub(-integer_operand.ImmediateValue())) {
    AddSubMacro(integer_rd, integer_rn, -integer_operand.ImmediateValue(),
                SetFlags, ADD);
  } else {
    AddSubMacro(integer_rd, integer_rn, integer_operand, SetFlags, SUB);
  }
}

void MacroAssembler::Cmn(const Register& rn, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  Adds(AppropriateZeroRegFor(rn), integer_rn, integer_operand);
}

void MacroAssembler::Cmp(const Register& rn, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  Subs(AppropriateZeroRegFor(rn), rn, operand);
}

#if V8_TARGET_CHERI
void MacroAssembler::Cmpc(const Register& cn, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(cn.IsC());
  Subsc(xzr, cn, operand);
}
#else
void MacroAssembler::Cmpc(const Register& cn, const Operand& operand) {}
#endif

void MacroAssembler::CmpTagged(const Register& rn, const Operand& operand) {
  if (COMPRESS_POINTERS_BOOL) {
    Cmp(rn.W(), operand.ToW());
  } else {
    Cmp(rn, operand);
  }
}

void MacroAssembler::Neg(const Register& rd, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  const Register& integer_rd = rd.IsC() ? rd.X() : rd;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  if (integer_operand.IsImmediate()) {
    Mov(integer_rd, -integer_operand.ImmediateValue());
  } else {
    Sub(integer_rd, AppropriateZeroRegFor(integer_rd), integer_operand);
  }
}

void MacroAssembler::Negs(const Register& rd, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  Subs(rd, AppropriateZeroRegFor(rd), operand);
}

void MacroAssembler::Adc(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  const Register& integer_rd = rd.IsC() ? rd.X() : rd;
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  AddSubWithCarryMacro(integer_rd, integer_rn, integer_operand, LeaveFlags,
                       ADC);
}

void MacroAssembler::Adcs(const Register& rd, const Register& rn,
                          const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  const Register& integer_rd = rd.IsC() ? rd.X() : rd;
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  AddSubWithCarryMacro(integer_rd, integer_rn, integer_operand, SetFlags, ADC);
}

void MacroAssembler::Sbc(const Register& rd, const Register& rn,
                         const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  const Register& integer_rd = rd.IsC() ? rd.X() : rd;
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  AddSubWithCarryMacro(integer_rd, integer_rn, integer_operand, LeaveFlags,
                       SBC);
}

void MacroAssembler::Sbcs(const Register& rd, const Register& rn,
                          const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  const Register& integer_rd = rd.IsC() ? rd.X() : rd;
  const Register& integer_rn = rn.IsC() ? rn.X() : rn;
  const Operand& integer_operand = operand.IsC() ? operand.ToX() : operand;
  AddSubWithCarryMacro(integer_rd, integer_rn, integer_operand, SetFlags, SBC);
}

void MacroAssembler::Ngc(const Register& rd, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  Register zr = AppropriateZeroRegFor(rd);
  Sbc(rd, zr, operand);
}

void MacroAssembler::Ngcs(const Register& rd, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  Register zr = AppropriateZeroRegFor(rd);
  Sbcs(rd, zr, operand);
}

void MacroAssembler::Mvn(const Register& rd, uint64_t imm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  Mov(rd, ~imm);
}

#define DEFINE_FUNCTION(FN, REGTYPE, REG, OP)                          \
  void MacroAssembler::FN(const REGTYPE REG, const MemOperand& addr) { \
    DCHECK(allow_macro_instructions());                                \
    LoadStoreMacro(REG, addr, OP);                                     \
  }
LS_MACRO_LIST(DEFINE_FUNCTION)
#undef DEFINE_FUNCTION

#define DEFINE_FUNCTION(FN, REGTYPE, REG, REG2, OP)              \
  void MacroAssembler::FN(const REGTYPE REG, const REGTYPE REG2, \
                          const MemOperand& addr) {              \
    DCHECK(allow_macro_instructions());                          \
    LoadStorePairMacro(REG, REG2, addr, OP);                     \
  }
LSPAIR_MACRO_LIST(DEFINE_FUNCTION)
#undef DEFINE_FUNCTION

#define DEFINE_FUNCTION(FN, OP)                                     \
  void MacroAssembler::FN(const Register& rt, const Register& rn) { \
    DCHECK(allow_macro_instructions());                             \
    OP(rt, rn);                                                     \
  }
LDA_STL_MACRO_LIST(DEFINE_FUNCTION)
#if V8_TARGET_CHERI
LDA_STL_CAP_MACRO_LIST(DEFINE_FUNCTION)
#endif
#undef DEFINE_FUNCTION

#define DEFINE_FUNCTION(FN, OP)                                   \
  void MacroAssembler::FN(const Register& rs, const Register& rt, \
                          const Register& rn) {                   \
    DCHECK(allow_macro_instructions());                           \
    OP(rs, rt, rn);                                               \
  }
STLX_MACRO_LIST(DEFINE_FUNCTION)
#if V8_TARGET_CHERI
STLX_CAP_MACRO_LIST(DEFINE_FUNCTION)
#endif
#undef DEFINE_FUNCTION

#define DEFINE_FUNCTION(FN, OP)                                   \
  void MacroAssembler::FN(const Register& rs, const Register& rt, \
                          const MemOperand& src) {                \
    DCHECK(allow_macro_instructions());                           \
    OP(rs, rt, src);                                              \
  }
CAS_SINGLE_MACRO_LIST(DEFINE_FUNCTION)
#if V8_TARGET_CHERI
CAS_SINGLE_CAP_MACRO_LIST(DEFINE_FUNCTION)
#endif
#undef DEFINE_FUNCTION

#define DEFINE_FUNCTION(FN, OP)                                    \
  void MacroAssembler::FN(const Register& rs, const Register& rs2, \
                          const Register& rt, const Register& rt2, \
                          const MemOperand& src) {                 \
    DCHECK(allow_macro_instructions());                            \
    OP(rs, rs2, rt, rt2, src);                                     \
  }
CAS_PAIR_MACRO_LIST(DEFINE_FUNCTION)
#undef DEFINE_FUNCTION

#define DEFINE_LOAD_FUNCTION(FN, OP)                              \
  void MacroAssembler::FN(const Register& rs, const Register& rt, \
                          const MemOperand& src) {                \
    DCHECK(allow_macro_instructions_);                            \
    OP(rs, rt, src);                                              \
  }
#define DEFINE_STORE_FUNCTION(FN, OP)                                  \
  void MacroAssembler::FN(const Register& rs, const MemOperand& src) { \
    DCHECK(allow_macro_instructions_);                                 \
    OP(rs, src);                                                       \
  }

ATOMIC_MEMORY_SIMPLE_MACRO_LIST(ATOMIC_MEMORY_LOAD_MACRO_MODES,
                                DEFINE_LOAD_FUNCTION, Ld, ld)
ATOMIC_MEMORY_SIMPLE_MACRO_LIST(ATOMIC_MEMORY_STORE_MACRO_MODES,
                                DEFINE_STORE_FUNCTION, St, st)

#define DEFINE_SWP_FUNCTION(FN, OP)                               \
  void MacroAssembler::FN(const Register& rs, const Register& rt, \
                          const MemOperand& src) {                \
    DCHECK(allow_macro_instructions_);                            \
    OP(rs, rt, src);                                              \
  }

ATOMIC_MEMORY_LOAD_MACRO_MODES(DEFINE_SWP_FUNCTION, Swp, swp)
#if V8_TARGET_CHERI
DEFINE_SWP_FUNCTION(Swp_C, swpa_c)
DEFINE_SWP_FUNCTION(Swpa_C, swp_c)
#endif

void MacroAssembler::Asr(const Register& rd, const Register& rn,
                         unsigned shift) {
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  asr(rd, rn, shift);
}

void MacroAssembler::Asr(const Register& rd, const Register& rn,
                         const Register& rm) {
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  asrv(rd, rn, rm);
}

void MacroAssembler::B(Label* label) {
  DCHECK(allow_macro_instructions());
  b(label);
  CheckVeneerPool(false, false);
}

void MacroAssembler::B(Condition cond, Label* label) {
  DCHECK(allow_macro_instructions());
  B(label, cond);
}

void MacroAssembler::Bfi(const Register& rd, const Register& rn, unsigned lsb,
                         unsigned width) {
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  bfi(rd, rn, lsb, width);
}

void MacroAssembler::Bfxil(const Register& rd, const Register& rn, unsigned lsb,
                           unsigned width) {
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  bfxil(rd, rn, lsb, width);
}

void MacroAssembler::Bind(Label* label, BranchTargetIdentifier id) {
  DCHECK(allow_macro_instructions());
  if (id == BranchTargetIdentifier::kNone) {
    bind(label);
  } else {
    // Emit this inside an InstructionAccurateScope to ensure there are no extra
    // instructions between the bind and the target identifier instruction.
    InstructionAccurateScope scope(this, 1);
    bind(label);
    if (id == BranchTargetIdentifier::kPacibsp) {
      pacibsp();
    } else {
      bti(id);
    }
  }
}

void MacroAssembler::CodeEntry() { CallTarget(); }

void MacroAssembler::ExceptionHandler() { JumpTarget(); }

void MacroAssembler::BindExceptionHandler(Label* label) {
  BindJumpTarget(label);
}

void MacroAssembler::JumpTarget() {
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  bti(BranchTargetIdentifier::kBtiJump);
#endif
}

void MacroAssembler::BindJumpTarget(Label* label) {
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  Bind(label, BranchTargetIdentifier::kBtiJump);
#else
  Bind(label);
#endif
}

void MacroAssembler::CallTarget() {
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  bti(BranchTargetIdentifier::kBtiCall);
#endif
}

void MacroAssembler::JumpOrCallTarget() {
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  bti(BranchTargetIdentifier::kBtiJumpCall);
#endif
}

void MacroAssembler::BindCallTarget(Label* label) {
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  Bind(label, BranchTargetIdentifier::kBtiCall);
#else
  Bind(label);
#endif
}

void MacroAssembler::BindJumpOrCallTarget(Label* label) {
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  Bind(label, BranchTargetIdentifier::kBtiJumpCall);
#else
  Bind(label);
#endif
}

void MacroAssembler::Bl(Label* label) {
  DCHECK(allow_macro_instructions());
  bl(label);
}

void MacroAssembler::Blr(const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rn.IsZero());
  DCHECK_IMPLIES(V8_TARGET_CHERI_BOOL, rn.IsC());
  blr(rn);
}

void MacroAssembler::Br(const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rn.IsZero());
  DCHECK_IMPLIES(V8_TARGET_CHERI_BOOL, rn.IsC());
  br(rn);
}

void MacroAssembler::Brk(int code) {
  DCHECK(allow_macro_instructions());
  brk(code);
}

void MacroAssembler::Cinc(const Register& rd, const Register& rn,
                          Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  cinc(rd, rn, cond);
}

void MacroAssembler::Cinv(const Register& rd, const Register& rn,
                          Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  cinv(rd, rn, cond);
}

void MacroAssembler::Cls(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  cls(rd, rn);
}

void MacroAssembler::Clz(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  clz(rd, rn);
}

void MacroAssembler::Cneg(const Register& rd, const Register& rn,
                          Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  cneg(rd, rn, cond);
}

// Conditionally zero the destination register. Only X registers are supported
// due to the truncation side-effect when used on W registers.
void MacroAssembler::CzeroX(const Register& rd, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsSP() && rd.Is64Bits());
  DCHECK((cond != al) && (cond != nv));
  csel(rd, xzr, rd, cond);
}

// Conditionally move a value into the destination register. Only X registers
// are supported due to the truncation side-effect when used on W registers.
void MacroAssembler::CmovX(const Register& rd, const Register& rn,
                           Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsSP());
  DCHECK(rd.Is64Bits() && rn.Is64Bits());
  DCHECK((cond != al) && (cond != nv));
  if (rd != rn) {
    csel(rd, rn, rd, cond);
  }
}

void MacroAssembler::Csdb() {
  DCHECK(allow_macro_instructions());
  csdb();
}

void MacroAssembler::Cset(const Register& rd, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  DCHECK(!rd.IsC());
  cset(rd, cond);
}

void MacroAssembler::Csetm(const Register& rd, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  DCHECK(!rd.IsC());
  csetm(rd, cond);
}

void MacroAssembler::Csinc(const Register& rd, const Register& rn,
                           const Register& rm, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  csinc(rd, rn, rm, cond);
}

void MacroAssembler::Csinv(const Register& rd, const Register& rn,
                           const Register& rm, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  csinv(rd, rn, rm, cond);
}

void MacroAssembler::Csneg(const Register& rd, const Register& rn,
                           const Register& rm, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  csneg(rd, rn, rm, cond);
}

void MacroAssembler::Dmb(BarrierDomain domain, BarrierType type) {
  DCHECK(allow_macro_instructions());
  dmb(domain, type);
}

void MacroAssembler::Dsb(BarrierDomain domain, BarrierType type) {
  DCHECK(allow_macro_instructions());
  dsb(domain, type);
}

void MacroAssembler::Debug(const char* message, uint32_t code, Instr params) {
  DCHECK(allow_macro_instructions());
  debug(message, code, params);
}

void MacroAssembler::Extr(const Register& rd, const Register& rn,
                          const Register& rm, unsigned lsb) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  extr(rd, rn, rm, lsb);
}

void MacroAssembler::Fabs(const VRegister& fd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  fabs(fd, fn);
}

void MacroAssembler::Fadd(const VRegister& fd, const VRegister& fn,
                          const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fadd(fd, fn, fm);
}

void MacroAssembler::Fccmp(const VRegister& fn, const VRegister& fm,
                           StatusFlags nzcv, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK((cond != al) && (cond != nv));
  fccmp(fn, fm, nzcv, cond);
}

void MacroAssembler::Fccmp(const VRegister& fn, const double value,
                           StatusFlags nzcv, Condition cond) {
  DCHECK(allow_macro_instructions());
  UseScratchRegisterScope temps(this);
  VRegister tmp = temps.AcquireSameSizeAs(fn);
  Fmov(tmp, value);
  Fccmp(fn, tmp, nzcv, cond);
}

void MacroAssembler::Fcmp(const VRegister& fn, const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fcmp(fn, fm);
}

void MacroAssembler::Fcmp(const VRegister& fn, double value) {
  DCHECK(allow_macro_instructions());
  if (value != 0.0) {
    UseScratchRegisterScope temps(this);
    VRegister tmp = temps.AcquireSameSizeAs(fn);
    Fmov(tmp, value);
    fcmp(fn, tmp);
  } else {
    fcmp(fn, value);
  }
}

void MacroAssembler::Fcsel(const VRegister& fd, const VRegister& fn,
                           const VRegister& fm, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK((cond != al) && (cond != nv));
  fcsel(fd, fn, fm, cond);
}

void MacroAssembler::Fcvt(const VRegister& fd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  fcvt(fd, fn);
}

void MacroAssembler::Fcvtas(const Register& rd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  fcvtas(rd, fn);
}

void MacroAssembler::Fcvtau(const Register& rd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  fcvtau(rd, fn);
}

void MacroAssembler::Fcvtms(const Register& rd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  fcvtms(rd, fn);
}

void MacroAssembler::Fcvtmu(const Register& rd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  fcvtmu(rd, fn);
}

void MacroAssembler::Fcvtns(const Register& rd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  fcvtns(rd, fn);
}

void MacroAssembler::Fcvtnu(const Register& rd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  fcvtnu(rd, fn);
}

void MacroAssembler::Fcvtzs(const Register& rd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  fcvtzs(rd, fn);
}
void MacroAssembler::Fcvtzu(const Register& rd, const VRegister& fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  fcvtzu(rd, fn);
}

void MacroAssembler::Fdiv(const VRegister& fd, const VRegister& fn,
                          const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fdiv(fd, fn, fm);
}

void MacroAssembler::Fmadd(const VRegister& fd, const VRegister& fn,
                           const VRegister& fm, const VRegister& fa) {
  DCHECK(allow_macro_instructions());
  fmadd(fd, fn, fm, fa);
}

void MacroAssembler::Fmax(const VRegister& fd, const VRegister& fn,
                          const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fmax(fd, fn, fm);
}

void MacroAssembler::Fmaxnm(const VRegister& fd, const VRegister& fn,
                            const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fmaxnm(fd, fn, fm);
}

void MacroAssembler::Fmin(const VRegister& fd, const VRegister& fn,
                          const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fmin(fd, fn, fm);
}

void MacroAssembler::Fminnm(const VRegister& fd, const VRegister& fn,
                            const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fminnm(fd, fn, fm);
}

void MacroAssembler::Fmov(VRegister fd, VRegister fn) {
  DCHECK(allow_macro_instructions());
  // Only emit an instruction if fd and fn are different, and they are both D
  // registers. fmov(s0, s0) is not a no-op because it clears the top word of
  // d0. Technically, fmov(d0, d0) is not a no-op either because it clears the
  // top of q0, but VRegister does not currently support Q registers.
  if (fd != fn || !fd.Is64Bits()) {
    fmov(fd, fn);
  }
}

void MacroAssembler::Fmov(VRegister fd, Register rn) {
  DCHECK(allow_macro_instructions());
  fmov(fd, rn);
}

void MacroAssembler::Fmov(VRegister vd, double imm) {
  DCHECK(allow_macro_instructions());

  if (vd.Is1S() || vd.Is2S() || vd.Is4S()) {
    Fmov(vd, static_cast<float>(imm));
    return;
  }

  DCHECK(vd.Is1D() || vd.Is2D());
  if (IsImmFP64(imm)) {
    fmov(vd, imm);
  } else {
    uint64_t bits = base::bit_cast<uint64_t>(imm);
    if (vd.IsScalar()) {
      if (bits == 0) {
        fmov(vd, xzr);
      } else {
        UseScratchRegisterScope temps(this);
        Register tmp = temps.AcquireX();
        Mov(tmp, bits);
        fmov(vd, tmp);
      }
    } else {
      Movi(vd, bits);
    }
  }
}

void MacroAssembler::Fmov(VRegister vd, float imm) {
  DCHECK(allow_macro_instructions());
  if (vd.Is1D() || vd.Is2D()) {
    Fmov(vd, static_cast<double>(imm));
    return;
  }

  DCHECK(vd.Is1S() || vd.Is2S() || vd.Is4S());
  if (IsImmFP32(imm)) {
    fmov(vd, imm);
  } else {
    uint32_t bits = base::bit_cast<uint32_t>(imm);
    if (vd.IsScalar()) {
      if (bits == 0) {
        fmov(vd, wzr);
      } else {
        UseScratchRegisterScope temps(this);
        Register tmp = temps.AcquireW();
        Mov(tmp, bits);
        Fmov(vd, tmp);
      }
    } else {
      Movi(vd, bits);
    }
  }
}

void MacroAssembler::Fmov(Register rd, VRegister fn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  fmov(rd, fn);
}

void MacroAssembler::Fmsub(const VRegister& fd, const VRegister& fn,
                           const VRegister& fm, const VRegister& fa) {
  DCHECK(allow_macro_instructions());
  fmsub(fd, fn, fm, fa);
}

void MacroAssembler::Fmul(const VRegister& fd, const VRegister& fn,
                          const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fmul(fd, fn, fm);
}

void MacroAssembler::Fnmadd(const VRegister& fd, const VRegister& fn,
                            const VRegister& fm, const VRegister& fa) {
  DCHECK(allow_macro_instructions());
  fnmadd(fd, fn, fm, fa);
}

void MacroAssembler::Fnmsub(const VRegister& fd, const VRegister& fn,
                            const VRegister& fm, const VRegister& fa) {
  DCHECK(allow_macro_instructions());
  fnmsub(fd, fn, fm, fa);
}

void MacroAssembler::Fsub(const VRegister& fd, const VRegister& fn,
                          const VRegister& fm) {
  DCHECK(allow_macro_instructions());
  fsub(fd, fn, fm);
}

void MacroAssembler::Hint(SystemHint code) {
  DCHECK(allow_macro_instructions());
  hint(code);
}

void MacroAssembler::Hlt(int code) {
  DCHECK(allow_macro_instructions());
  hlt(code);
}

void MacroAssembler::Isb() {
  DCHECK(allow_macro_instructions());
  isb();
}

void MacroAssembler::Ldr(const CPURegister& rt, const Operand& operand) {
  DCHECK(allow_macro_instructions());
  ldr(rt, operand);
}

void MacroAssembler::Lsl(const Register& rd, const Register& rn,
                         unsigned shift) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rn.IsC());
  DCHECK(!rd.IsC());
  lsl(rd, rn, shift);
}

void MacroAssembler::Lsl(const Register& rd, const Register& rn,
                         const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rn.IsC());
  DCHECK(!rd.IsC());
  DCHECK(!rm.IsC());
  lslv(rd, rn, rm);
}

void MacroAssembler::Lsr(const Register& rd, const Register& rn,
                         unsigned shift) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  lsr(rd, rn, shift);
}

void MacroAssembler::Lsr(const Register& rd, const Register& rn,
                         const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  lsrv(rd, rn, rm);
}

void MacroAssembler::Madd(const Register& rd, const Register& rn,
                          const Register& rm, const Register& ra) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  DCHECK(!ra.IsC());
  madd(rd, rn, rm, ra);
}

void MacroAssembler::Mneg(const Register& rd, const Register& rn,
                          const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  mneg(rd, rn, rm);
}

void MacroAssembler::Movk(const Register& rd, uint64_t imm, int shift) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  movk(rd, imm, shift);
}

void MacroAssembler::Mrs(const Register& rt, SystemRegister sysreg) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rt.IsZero());
  mrs(rt, sysreg);
}

void MacroAssembler::Msr(SystemRegister sysreg, const Register& rt) {
  DCHECK(allow_macro_instructions());
  msr(sysreg, rt);
}

void MacroAssembler::Msub(const Register& rd, const Register& rn,
                          const Register& rm, const Register& ra) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  DCHECK(!ra.IsC());
  msub(rd, rn, rm, ra);
}

void MacroAssembler::Mul(const Register& rd, const Register& rn,
                         const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  mul(rd, rn, rm);
}

void MacroAssembler::Rbit(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  rbit(rd, rn);
}

void MacroAssembler::Ret(const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rn.IsZero());
  ret(rn);
  CheckVeneerPool(false, false);
}

void MacroAssembler::Rev(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  rev(rd, rn);
}

void MacroAssembler::Rev16(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  rev16(rd, rn);
}

void MacroAssembler::Rev32(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  rev32(rd, rn);
}

void MacroAssembler::Ror(const Register& rd, const Register& rs,
                         unsigned shift) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rs.IsC());
  ror(rd, rs, shift);
}

void MacroAssembler::Ror(const Register& rd, const Register& rn,
                         const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  rorv(rd, rn, rm);
}

void MacroAssembler::Sbfx(const Register& rd, const Register& rn, unsigned lsb,
                          unsigned width) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  sbfx(rd, rn, lsb, width);
}

void MacroAssembler::Scvtf(const VRegister& fd, const Register& rn,
                           unsigned fbits) {
  DCHECK(allow_macro_instructions());
  scvtf(fd, rn, fbits);
}

void MacroAssembler::Sdiv(const Register& rd, const Register& rn,
                          const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  sdiv(rd, rn, rm);
}

void MacroAssembler::Smaddl(const Register& rd, const Register& rn,
                            const Register& rm, const Register& ra) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  DCHECK(!ra.IsC());
  smaddl(rd, rn, rm, ra);
}

void MacroAssembler::Smsubl(const Register& rd, const Register& rn,
                            const Register& rm, const Register& ra) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  DCHECK(!ra.IsC());
  smsubl(rd, rn, rm, ra);
}

void MacroAssembler::Smull(const Register& rd, const Register& rn,
                           const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  smull(rd, rn, rm);
}

void MacroAssembler::Smulh(const Register& rd, const Register& rn,
                           const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  smulh(rd, rn, rm);
}

void MacroAssembler::Umull(const Register& rd, const Register& rn,
                           const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  umaddl(rd, rn, rm, xzr);
}

void MacroAssembler::Umulh(const Register& rd, const Register& rn,
                           const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  umulh(rd, rn, rm);
}

void MacroAssembler::Sxtb(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  sxtb(rd, rn);
}

void MacroAssembler::Sxth(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  sxth(rd, rn);
}

void MacroAssembler::Sxtw(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  sxtw(rd, rn);
}

void MacroAssembler::Ubfiz(const Register& rd, const Register& rn, unsigned lsb,
                           unsigned width) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  ubfiz(rd, rn, lsb, width);
}

void MacroAssembler::Sbfiz(const Register& rd, const Register& rn, unsigned lsb,
                           unsigned width) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  sbfiz(rd, rn, lsb, width);
}

void MacroAssembler::Ubfx(const Register& rd, const Register& rn, unsigned lsb,
                          unsigned width) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  ubfx(rd, rn, lsb, width);
}

void MacroAssembler::Ucvtf(const VRegister& fd, const Register& rn,
                           unsigned fbits) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rn.IsC());
  ucvtf(fd, rn, fbits);
}

void MacroAssembler::Udiv(const Register& rd, const Register& rn,
                          const Register& rm) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  udiv(rd, rn, rm);
}

void MacroAssembler::Umaddl(const Register& rd, const Register& rn,
                            const Register& rm, const Register& ra) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  DCHECK(!ra.IsC());
  umaddl(rd, rn, rm, ra);
}

void MacroAssembler::Umsubl(const Register& rd, const Register& rn,
                            const Register& rm, const Register& ra) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  DCHECK(!rm.IsC());
  DCHECK(!ra.IsC());
  umsubl(rd, rn, rm, ra);
}

void MacroAssembler::Uxtb(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  uxtb(rd, rn);
}

void MacroAssembler::Uxth(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  uxth(rd, rn);
}

void MacroAssembler::Uxtw(const Register& rd, const Register& rn) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK(!rd.IsC());
  DCHECK(!rn.IsC());
  uxtw(rd, rn);
}

void MacroAssembler::InitializeRootRegister() {
  ExternalReference isolate_root = ExternalReference::isolate_root(isolate());
  Mov(kRootRegister, Operand(isolate_root));
#ifdef V8_COMPRESS_POINTERS
  LoadRootRelative(kPtrComprCageBaseRegister, IsolateData::cage_base_offset());
#endif
}

void MacroAssembler::SmiTag(Register dst, Register src) {
  DCHECK(dst.Is64Bits() && src.Is64Bits());
  DCHECK(SmiValuesAre32Bits() || SmiValuesAre31Bits());
  Lsl(dst, src, kSmiShift);
}

void MacroAssembler::SmiTag(Register smi) { SmiTag(smi, smi); }

void MacroAssembler::SmiUntag(Register dst, Register src) {
  DCHECK_IMPLIES(!V8_TARGET_CHERI_BOOL, dst.Is64Bits() && src.Is64Bits());
  if (v8_flags.enable_slow_asserts) {
    AssertSmi(src);
  }
  DCHECK(SmiValuesAre32Bits() || SmiValuesAre31Bits());
  if (COMPRESS_POINTERS_BOOL) {
    Sbfx(dst.X(), src.W(), kSmiShift, kSmiValueSize);
  } else {
    Asr(dst.X(), src.X(), kSmiShift);
  }
}

void MacroAssembler::SmiUntag(Register dst, const MemOperand& src) {
  DCHECK(dst.Is64Bits());
  if (SmiValuesAre32Bits()) {
    if (src.IsImmediateOffset() && src.shift_amount() == 0) {
      // Load value directly from the upper half-word.
      // Assumes that Smis are shifted by 32 bits and little endianness.
      DCHECK_EQ(kSmiShift, 32);
      Ldrsw(dst,
            MemOperand(src.base(), src.offset() + (kSmiShift / kBitsPerByte),
                       src.addrmode()));

    } else {
      Ldr(dst, src);
      SmiUntag(dst);
    }
  } else {
    DCHECK(SmiValuesAre31Bits());
    if (COMPRESS_POINTERS_BOOL) {
      Ldr(dst.W(), src);
    } else {
      Ldr(dst, src);
    }
    SmiUntag(dst);
  }
}

void MacroAssembler::SmiUntag(Register smi) { SmiUntag(smi, smi); }

void MacroAssembler::SmiToInt32(Register smi) { SmiToInt32(smi, smi); }

void MacroAssembler::SmiToInt32(Register dst, Register smi) {
  DCHECK(dst.Is64Bits() || dst.IsC());
  if (v8_flags.enable_slow_asserts) {
    AssertSmi(smi);
  }
  DCHECK(SmiValuesAre32Bits() || SmiValuesAre31Bits());
  if (COMPRESS_POINTERS_BOOL) {
    Asr(dst.W(), smi.W(), kSmiShift);
  } else {
    Lsr(dst.X(), smi.X(), kSmiShift);
  }
}

void MacroAssembler::JumpIfSmi(Register value, Label* smi_label,
                               Label* not_smi_label) {
  static_assert((kSmiTagSize == 1) && (kSmiTag == 0));
  // Check if the tag bit is set.
  if (smi_label) {
    Tbz(value, 0, smi_label);
    if (not_smi_label) {
      B(not_smi_label);
    }
  } else {
    DCHECK(not_smi_label);
    Tbnz(value, 0, not_smi_label);
  }
}

void MacroAssembler::JumpIfEqual(Register x, int32_t y, Label* dest) {
  CompareAndBranch(x, y, eq, dest);
}

void MacroAssembler::JumpIfLessThan(Register x, int32_t y, Label* dest) {
  CompareAndBranch(x, y, lt, dest);
}

void MacroAssembler::JumpIfNotSmi(Register value, Label* not_smi_label) {
  JumpIfSmi(value, nullptr, not_smi_label);
}

inline void MacroAssembler::AssertFeedbackVector(Register object) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  AssertFeedbackVector(object, scratch);
}

void MacroAssembler::jmp(Label* L) { B(L); }

template <MacroAssembler::StoreLRMode lr_mode>
void MacroAssembler::Push(const CPURegister& src0, const CPURegister& src1,
                          const CPURegister& src2, const CPURegister& src3) {
  // XXX(cheri): This is quite messy, but it seems to be the "cleanest" way to
  // resolve calls from the baseline assembler that explicitly pass padreg with
  // no information about the other registers being passed in.
  if (src0.is_valid() && !src0.IsC() && src0.code() == padreg.code() &&
      src1.IsC()) {
    Push(padregc, src1, src2, src3);
    return;
  }
  DCHECK(AreSameSizeAndType(src0, src1, src2, src3));
  DCHECK_IMPLIES((lr_mode == kSignLR), ((src0 == lr) || (src1 == lr) ||
                                        (src2 == lr) || (src3 == lr)));
  DCHECK_IMPLIES((lr_mode == kDontStoreLR), ((src0 != lr) && (src1 != lr) &&
                                             (src2 != lr) && (src3 != lr)));

#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  if (lr_mode == kSignLR) {
    Pacibsp();
  }
#endif

  int count = 1 + src1.is_valid() + src2.is_valid() + src3.is_valid();
  int size = src0.SizeInBytes();
  DCHECK_EQ(0, (size * count) % 16);

  PushHelper(count, size, src0, src1, src2, src3);
}

template <MacroAssembler::StoreLRMode lr_mode>
void MacroAssembler::Push(const Register& src0, const VRegister& src1) {
  DCHECK_IMPLIES((lr_mode == kSignLR), ((src0 == lr) || (src1 == lr)));
  DCHECK_IMPLIES((lr_mode == kDontStoreLR), ((src0 != lr) && (src1 != lr)));
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  if (lr_mode == kSignLR) {
    Pacibsp();
  }
#endif

  int size = src0.SizeInBytes() + src1.SizeInBytes();
  DCHECK_EQ(0, size % 16);

  // Reserve room for src0 and push src1.
  str(src1, MemOperand(csp, -size, PreIndex));
  // Fill the gap with src0.
  str(src0, MemOperand(csp, src1.SizeInBytes()));
}

template <MacroAssembler::LoadLRMode lr_mode>
void MacroAssembler::Pop(const CPURegister& dst0, const CPURegister& dst1,
                         const CPURegister& dst2, const CPURegister& dst3) {
  // It is not valid to pop into the same register more than once in one
  // instruction, not even into the zero register.
  DCHECK(!AreAliased(dst0, dst1, dst2, dst3));
  // XXX(cheri): This is quite messy, but it seems to be the "cleanest" way to
  // resolve calls from the baseline assembler that explicitly pass padreg with
  // no information about the other registers being passed in.
  if (dst1.is_valid() && !dst1.IsC() && dst1.code() == padreg.code() &&
      dst0.IsC()) {
    Pop(dst0, padregc, dst2, dst3);
    return;
  }
  DCHECK(AreSameSizeAndType(dst0, dst1, dst2, dst3));
  DCHECK(dst0.is_valid());

  int count = 1 + dst1.is_valid() + dst2.is_valid() + dst3.is_valid();
  int size = dst0.SizeInBytes();
  DCHECK_EQ(0, (size * count) % 16);

  PopHelper(count, size, dst0, dst1, dst2, dst3);

  DCHECK_IMPLIES((lr_mode == kAuthLR), ((dst0 == lr) || (dst1 == lr) ||
                                        (dst2 == lr) || (dst3 == lr)));
  DCHECK_IMPLIES((lr_mode == kDontLoadLR), ((dst0 != lr) && (dst1 != lr)) &&
                                               (dst2 != lr) && (dst3 != lr));

#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  if (lr_mode == kAuthLR) {
    Autibsp();
  }
#endif
}

template <MacroAssembler::StoreLRMode lr_mode>
void MacroAssembler::Poke(const CPURegister& src, const Operand& offset) {
  DCHECK_IMPLIES((lr_mode == kSignLR), (src == lr));
  DCHECK_IMPLIES((lr_mode == kDontStoreLR), (src != lr));
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  if (lr_mode == kSignLR) {
    Pacibsp();
  }
#endif

  if (offset.IsImmediate()) {
    DCHECK_GE(offset.ImmediateValue(), 0);
  } else if (v8_flags.debug_code) {
    Cmp(xzr, offset);
    Check(le, AbortReason::kStackAccessBelowStackPointer);
  }

  Str(src, MemOperand(csp, offset));
}

template <MacroAssembler::LoadLRMode lr_mode>
void MacroAssembler::Peek(const CPURegister& dst, const Operand& offset) {
  if (offset.IsImmediate()) {
    DCHECK_GE(offset.ImmediateValue(), 0);
  } else if (v8_flags.debug_code) {
    Cmp(xzr, offset);
    Check(le, AbortReason::kStackAccessBelowStackPointer);
  }

  Ldr(dst, MemOperand(sp, offset));

  DCHECK_IMPLIES((lr_mode == kAuthLR), (dst == lr));
  DCHECK_IMPLIES((lr_mode == kDontLoadLR), (dst != lr));
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  if (lr_mode == kAuthLR) {
    Autibsp();
  }
#endif
}

void MacroAssembler::Claim(int64_t count, uint64_t unit_size) {
  DCHECK_GE(count, 0);
  uint64_t size = count * unit_size;

  if (size == 0) {
    return;
  }
  DCHECK_EQ(size % 16, 0);
#ifdef V8_TARGET_OS_WIN
  while (size > kStackPageSize) {
    Sub(sp, sp, kStackPageSize);
    Str(xzr, MemOperand(sp));
    size -= kStackPageSize;
  }
#endif
#if V8_TARGET_CHERI
  if (IsImmAddSub(size)) {
    Sub(csp, csp, size);
  } else {
    UseScratchRegisterScope temps(this);
    size_t popcount = __builtin_popcount(temps.Available()->bits());
    if (popcount < 2) {
      CHECK_EQ(popcount, 1);
      Str(c0, MemOperand(csp, -kSystemPointerSize));
      temps.Include(c0);
      Sub(csp, csp, size);
      Ldr(c0, MemOperand(csp, size - kSystemPointerSize));
    } else {
      Sub(csp, csp, size);
    }
  }
#else
  Sub(sp, sp, size);
#endif
}

void MacroAssembler::Claim(const Register& count, uint64_t unit_size,
                           bool assume_sp_aligned) {
  if (unit_size == 0) return;
  DCHECK(base::bits::IsPowerOfTwo(unit_size));

  const int shift = base::bits::CountTrailingZeros(unit_size);
  const Operand size(count, LSL, shift);

  if (size.IsZero()) {
    return;
  }
  AssertPositiveOrZero(count);

#ifdef V8_TARGET_OS_WIN
  // "Functions that allocate 4k or more worth of stack must ensure that each
  // page prior to the final page is touched in order." Source:
  // https://docs.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions?view=vs-2019#stack

  // Callers expect count register to not be clobbered, so copy it.
  UseScratchRegisterScope temps(this);
  Register bytes_scratch = temps.AcquireX();
  Mov(bytes_scratch, size);

  Label check_offset;
  Label touch_next_page;
  B(&check_offset);
  Bind(&touch_next_page);
  Sub(sp, sp, kStackPageSize);
  // Just to touch the page, before we increment further.
  if (assume_sp_aligned) {
    Str(xzr, MemOperand(sp));
  } else {
    Register sp_copy = temps.AcquireX();
    Mov(sp_copy, sp);
    Str(xzr, MemOperand(sp_copy));
  }
  Sub(bytes_scratch, bytes_scratch, kStackPageSize);

  Bind(&check_offset);
  Cmp(bytes_scratch, kStackPageSize);
  B(gt, &touch_next_page);

  Sub(sp, sp, bytes_scratch);
#else
  Sub(csp, csp, size);
#endif
}

void MacroAssembler::Drop(int64_t count, uint64_t unit_size) {
  DCHECK_GE(count, 0);
  uint64_t size = count * unit_size;

  if (size == 0) {
    return;
  }

  Add(csp, csp, size);
  DCHECK_EQ(size % 16, 0);
}

void MacroAssembler::Drop(const Register& count, uint64_t unit_size) {
  if (unit_size == 0) return;
  DCHECK(base::bits::IsPowerOfTwo(unit_size));

  const int shift = base::bits::CountTrailingZeros(unit_size);
  const Operand size(count, LSL, shift);

  if (size.IsZero()) {
    return;
  }

  AssertPositiveOrZero(count);
  Add(csp, csp, size);
}

void MacroAssembler::DropArguments(const Register& count,
                                   ArgumentsCountMode mode) {
  int extra_slots = 1;  // Padding slot.
  if (mode == kCountExcludesReceiver) {
    // Add a slot for the receiver.
    ++extra_slots;
  }
  UseScratchRegisterScope temps(this);
  Register tmp = temps.AcquireX();
  Add(tmp, count, extra_slots);
  Bic(tmp, tmp, 1);
  Drop(tmp, kCRegSize);
}

void MacroAssembler::DropArguments(int64_t count, ArgumentsCountMode mode) {
  if (mode == kCountExcludesReceiver) {
    // Add a slot for the receiver.
    ++count;
  }
  Drop(RoundUp(count, 2), kCRegSize);
}

void MacroAssembler::DropSlots(int64_t count) {
  Drop(RoundUp(count, 2), kCRegSize);
}

void MacroAssembler::PushArgument(const Register& arg) {
   Push(padregc, arg.C());
}

void MacroAssembler::CompareAndBranch(const Register& lhs, const Operand& rhs,
                                      Condition cond, Label* label) {
  if (rhs.IsImmediate() && (rhs.ImmediateValue() == 0) &&
      ((cond == eq) || (cond == ne) || (cond == hi) || (cond == ls))) {
    if ((cond == eq) || (cond == ls)) {
      Cbz(lhs, label);
    } else {
      Cbnz(lhs, label);
    }
  } else {
    Cmp(lhs, rhs);
    B(cond, label);
  }
}

void MacroAssembler::CompareTaggedAndBranch(const Register& lhs,
                                            const Operand& rhs, Condition cond,
                                            Label* label) {
  if (COMPRESS_POINTERS_BOOL) {
    CompareAndBranch(lhs.W(), rhs.ToW(), cond, label);
  } else {
    CompareAndBranch(lhs, rhs, cond, label);
  }
}

void MacroAssembler::TestAndBranchIfAnySet(const Register& reg,
                                           const uint64_t bit_pattern,
                                           Label* label) {
  Register reduced_reg = reg.IsC() ? reg.X() : reg;
  int bits = reduced_reg.SizeInBits();
  DCHECK_GT(CountSetBits(bit_pattern, bits), 0);
  if (CountSetBits(bit_pattern, bits) == 1) {
    Tbnz(reduced_reg, MaskToBit(bit_pattern), label);
  } else {
    Tst(reduced_reg, bit_pattern);
    B(ne, label);
  }
}

void MacroAssembler::TestAndBranchIfAllClear(const Register& reg,
                                             const uint64_t bit_pattern,
                                             Label* label) {
  Register reduced_reg = reg.IsC() ? reg.X() : reg;
  int bits = reduced_reg.SizeInBits();
  DCHECK_GT(CountSetBits(bit_pattern, bits), 0);
  if (CountSetBits(bit_pattern, bits) == 1) {
    Tbz(reduced_reg, MaskToBit(bit_pattern), label);
  } else {
    Tst(reduced_reg, bit_pattern);
    B(eq, label);
  }
}

void MacroAssembler::MoveHeapNumber(Register dst, double value) {
  Mov(dst, Operand::EmbeddedHeapNumber(value));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM64_MACRO_ASSEMBLER_ARM64_INL_H_

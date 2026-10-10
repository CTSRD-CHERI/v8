// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_INTERFACE_DESCRIPTORS_ARM64_INL_H_
#define V8_CODEGEN_ARM64_INTERFACE_DESCRIPTORS_ARM64_INL_H_

#if V8_TARGET_ARCH_ARM64

#include "src/base/template-utils.h"
#include "src/codegen/interface-descriptors.h"
#include "src/execution/frames.h"

namespace v8 {
namespace internal {

// TODO(cheri): Go through all of these and verify that we have the right
// register types.
constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  auto registers = RegisterArray(c0, c1, c2, c3, c4);
  static_assert(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultDoubleRegisterArray() {
  auto registers = DoubleRegisterArray(d0, d1, d2, d3, d4, d5, d6);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultReturnRegisterArray() {
  auto registers =
      RegisterArray(kReturnRegister0, kReturnRegister1, kReturnRegister2);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultReturnDoubleRegisterArray() {
  // Padding to have as many double return registers as GP return registers.
  auto registers = DoubleRegisterArray(kFPReturnRegister0, no_dreg, no_dreg);
  return registers;
}

#if DEBUG
template <typename DerivedDescriptor>
void StaticCallInterfaceDescriptor<DerivedDescriptor>::
    VerifyArgumentRegisterCount(CallInterfaceDescriptorData* data, int argc) {
  RegList allocatable_regs = data->allocatable_registers();
  if (argc >= 1) DCHECK(allocatable_regs.has(c0));
  if (argc >= 2) DCHECK(allocatable_regs.has(c1));
  if (argc >= 3) DCHECK(allocatable_regs.has(c2));
  if (argc >= 4) DCHECK(allocatable_regs.has(c3));
  if (argc >= 5) DCHECK(allocatable_regs.has(c4));
  if (argc >= 6) DCHECK(allocatable_regs.has(c5));
  if (argc >= 7) DCHECK(allocatable_regs.has(c6));
  if (argc >= 8) DCHECK(allocatable_regs.has(c7));
}
#endif  // DEBUG

// static
constexpr auto WriteBarrierDescriptor::registers() {
  // TODO(leszeks): Remove x7 which is just there for padding.
  return RegisterArray(c1, c5, c4, c2, c0, c3, kContextRegister, c7);
}

// static
constexpr Register LoadDescriptor::ReceiverRegister() {
  return c1;  // kReceiver (MachineType::AnyTagged)
}
// static
constexpr Register LoadDescriptor::NameRegister() {
  return c2;  // kName (MachineType::AnyTagged)
}
// static
constexpr Register LoadDescriptor::SlotRegister() { return x0; }

// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() { return c3; }

// static
constexpr Register KeyedLoadBaselineDescriptor::ReceiverRegister() {
  return c1;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::NameRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::SlotRegister() { return x2; }

// static
constexpr Register KeyedLoadWithVectorDescriptor::VectorRegister() {
  return c3;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::EnumIndexRegister() {
  return x4;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::CacheTypeRegister() {
  return x5;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::SlotRegister() {
  return x2;
}

// static
constexpr Register KeyedHasICBaselineDescriptor::ReceiverRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() { return c1; }
// static
constexpr Register KeyedHasICBaselineDescriptor::SlotRegister() { return x2; }

// static
constexpr Register KeyedHasICWithVectorDescriptor::VectorRegister() {
  return c3;
}

// static
constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return c4;
}

// static
constexpr Register StoreDescriptor::ReceiverRegister() { return c1; }
// static
constexpr Register StoreDescriptor::NameRegister() { return c2; }
// static
constexpr Register StoreDescriptor::ValueRegister() { return c0; }
// static
constexpr Register StoreDescriptor::SlotRegister() { return x4; }

// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() { return c3; }

// static
constexpr Register DefineKeyedOwnDescriptor::FlagsRegister() { return x5; }

// static
constexpr Register StoreTransitionDescriptor::MapRegister() { return c5; }

// static
constexpr Register ApiGetterDescriptor::CallbackRegister() { return c3; }

// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() {
  return c0; // kObject (MachineType::AnyTagged)
}
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() {
  return c3; // kKey (MachineType::AnyTagged)
}

// static
constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return x3;
}
// static
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() { return x4; }

// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() {
  return c0; // kArgument (MachineType::AnyTagged)
}

// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::FlagsRegister() {
  return x8;
}
// static
constexpr Register MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::
    FeedbackVectorRegister() {
  return c9;
}
// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::TemporaryRegister() {
  return x5;
}

// static
constexpr auto TypeofDescriptor::registers() { return RegisterArray(c0); }

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // x1: target
  // x0: number of arguments
  return RegisterArray(c1, x0);
}

constexpr auto CopyDataPropertiesWithExcludedPropertiesDescriptor::registers() {
  // r1 : the source
  // r0 : the excluded property count
  return RegisterArray(c1, c0);
}

constexpr auto
CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor::registers() {
  // r1 : the source
  // r0 : the excluded property count
  // x2 : the excluded property base
  return RegisterArray(c1, x0, x2);
}

// static
constexpr auto CallVarargsDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x4 : arguments list length (untagged)
  // x2 : arguments list (FixedArray)
  return RegisterArray(c1, x0, x4, c2);
}

// static
constexpr auto CallForwardVarargsDescriptor::registers() {
  // x1: target
  // x0: number of arguments
  // x2: start index (to supported rest parameters)
  return RegisterArray(c1, x0, x2);
}

// static
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // x1 : function template info
  // x2 : number of arguments (on the stack)
  return RegisterArray(c1, x2);
}

// static
constexpr auto CallFunctionTemplateGenericDescriptor::registers() {
  // x1 : function template info
  // x2 : number of arguments (on the stack)
  // x3 : topmost script-having context
  return RegisterArray(c1, x2, c3);
}

// static
constexpr auto CallWithSpreadDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x2 : the object to spread
  return RegisterArray(c1, x0, c2);
}

// static
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // x1 : the target to call
  // x2 : the arguments list
  return RegisterArray(c1, c2);
}

// static
constexpr auto ConstructVarargsDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x3 : the new target
  // x4 : arguments list length (untagged)
  // x2 : arguments list (FixedArray)
  return RegisterArray(c1, c3, x0, x4, c2);
}

// static
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // x3: new target
  // x1: target
  // x0: number of arguments
  // x2: start index (to supported rest parameters)
  return RegisterArray(c1, c3, x0, x2);
}

// static
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x3 : the new target
  // x2 : the object to spread
  return RegisterArray(c1, c3, x0, c2);
}

// static
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // x1 : the target to call
  // x3 : the new target
  // x2 : the arguments list
  return RegisterArray(c1, c3, c2);
}

// static
constexpr auto ConstructStubDescriptor::registers() {
  // x3: new target
  // x1: target
  // x0: number of arguments
  return RegisterArray(x1, x3, x0);
}

// static
constexpr auto AbortDescriptor::registers() {
  return RegisterArray(c1); // kMessageOrMessageId (MachineType::AnyTagged)
}

// static
constexpr auto CompareDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  return RegisterArray(c1, c0);
}

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  // x2: feedback slot
  return RegisterArray(c1, c0, c2);
}

#ifdef V8_ENABLE_SPARKPLUG_PLUS
constexpr auto CompareAndTryPatchCodeDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  // x2: current feedback value
  // x3: feedback offset
  return RegisterArray(c1, c0, x2, x3);
}
#endif  // V8_ENABLE_SPARKPLUG_PLUS

// static
constexpr auto Compare_WithEmbeddedFeedbackOffsetDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  // x2: feedback offset
  return RegisterArray(c1, c0, x2);
}

// static
constexpr auto BinaryOpDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  return RegisterArray(c1, c0);
}

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  // x2: feedback slot
  return RegisterArray(c1, c0, x2);
}

// static
constexpr auto BinarySmiOp_BaselineDescriptor::registers() {
  // x0: left operand
  // x1: right operand
  // x2: feedback slot
  return RegisterArray(c0, x1, x2);
}

// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister() {
  return c1;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister() {
  return x2;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister() {
  return c3;
}

// static
constexpr Register
CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister() {
  return x2;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::TopmostScriptHavingContextRegister() {
  return c1;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister() {
  return c3;
}

// static
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// static
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
  // FIXME(ds815): Some of these don't look like capabilities.
  return RegisterArray(c0,   // argument count
                       c2,   // address of first argument
                       c1);  // the target callable to be call
}

// static
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
  // FIXME(ds815): Some of these don't look like capabilities.
  return RegisterArray(
      c0,   // argument count
      c4,   // address of the first argument
      c1,   // constructor to call
      c3,   // new target
      c2);  // allocation site feedback if available, undefined otherwise
}

// static
constexpr auto ConstructForwardAllArgsDescriptor::registers() {
  return RegisterArray(c1,   // constructor to call
                       c3);  // new target
}

// static
constexpr auto ResumeGeneratorDescriptor::registers() {
  return RegisterArray(c0,   // the value to pass to the generator
                       c1);  // the JSGeneratorObject to resume
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
  return RegisterArray(c0, c1);
}

constexpr auto WasmJSToWasmWrapperDescriptor::registers() {
  // Arbitrarily picked register.
  return RegisterArray(c8);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64

#endif  // V8_CODEGEN_ARM64_INTERFACE_DESCRIPTORS_ARM64_INL_H_

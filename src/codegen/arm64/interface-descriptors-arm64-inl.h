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

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
#if defined(__CHERI_PURE_CAPABILITY__)
  auto registers = RegisterArray(c0, c1, c2, c3, c4);
#else
  auto registers = RegisterArray(x0, x1, x2, x3, x4);
#endif // __CHERI_PURE_CAPABILITY__
  static_assert(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

#if DEBUG
template <typename DerivedDescriptor>
void StaticCallInterfaceDescriptor<DerivedDescriptor>::
    VerifyArgumentRegisterCount(CallInterfaceDescriptorData* data, int argc) {
#if defined(__CHERI_PURE_CAPABILITY__)
  RegList allocatable_regs = data->allocatable_registers();
  if (argc >= 1) DCHECK(allocatable_regs.has(c0));
  if (argc >= 2) DCHECK(allocatable_regs.has(c1));
  if (argc >= 3) DCHECK(allocatable_regs.has(c2));
  if (argc >= 4) DCHECK(allocatable_regs.has(c3));
  if (argc >= 5) DCHECK(allocatable_regs.has(c4));
  if (argc >= 6) DCHECK(allocatable_regs.has(c5));
  if (argc >= 7) DCHECK(allocatable_regs.has(c6));
  if (argc >= 8) DCHECK(allocatable_regs.has(c7));
#else
  RegList allocatable_regs = data->allocatable_registers();
  if (argc >= 1) DCHECK(allocatable_regs.has(x0));
  if (argc >= 2) DCHECK(allocatable_regs.has(x1));
  if (argc >= 3) DCHECK(allocatable_regs.has(x2));
  if (argc >= 4) DCHECK(allocatable_regs.has(x3));
  if (argc >= 5) DCHECK(allocatable_regs.has(x4));
  if (argc >= 6) DCHECK(allocatable_regs.has(x5));
  if (argc >= 7) DCHECK(allocatable_regs.has(x6));
  if (argc >= 8) DCHECK(allocatable_regs.has(x7));
#endif // __CHERI_PURE_CAPABILITY__
}
#endif  // DEBUG

// static
constexpr auto WriteBarrierDescriptor::registers() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1, c5, c4, c2, c0, c3, kContextRegister, c7);
#else
  // TODO(leszeks): Remove x7 which is just there for padding.
  return RegisterArray(x1, x5, x4, x2, x0, x3, kContextRegister, x7);
#endif // __CHERI_PURE_CAPABILITY__
}

#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register LoadDescriptor::ReceiverRegister() {
  return c1;  // kReceiver (MachineType::AnyTagged)
}
#else
// static
constexpr Register LoadDescriptor::ReceiverRegister() { return x1; }
#endif // __CHERI_PURE_CAPABILITY__
#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register LoadDescriptor::NameRegister() {
  return c2; // kName (MachineType::AnyTagged)
}
#else
// static
constexpr Register LoadDescriptor::NameRegister() { return x2; }
#endif // __CHERI_PURE_CAPABILITY__
// static
constexpr Register LoadDescriptor::SlotRegister() { return x0; }

#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() {
  return c3; // kVector (MachineType::AnyTagged)
}
#else
// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() { return x3; }
#endif // __CHERI_PURE_CAPABILITY__

// static
constexpr Register KeyedLoadBaselineDescriptor::ReceiverRegister() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return c1; // kReceiver (MachineType::AnyTagged)
#else
  return x1;
#endif // __CHERI_PURE_CAPABILITY__
}
// static
constexpr Register KeyedLoadBaselineDescriptor::NameRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::SlotRegister() { return x2; }

// static
constexpr Register KeyedLoadWithVectorDescriptor::VectorRegister() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return c3; // kVector (MachineType::AnyTagged)
#else
  return x3;
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr Register KeyedHasICBaselineDescriptor::ReceiverRegister() {
  return kInterpreterAccumulatorRegister;
}
#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() {
  return c1; // kName (MachineType::AnyTagged)
}
#else
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() { return x1; }
#endif // __CHERI_PURE_CAPABILITY__
// static
constexpr Register KeyedHasICBaselineDescriptor::SlotRegister() { return x2; }

// static
constexpr Register KeyedHasICWithVectorDescriptor::VectorRegister() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return c3; // kVector (MachineType::AnyTagged)
#else
  return x3;
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return c4; // kLookupStartObject (MachineType::AnyTagged)
#else // __CHERI_PURE_CAPABILITY__
  return x4;
#endif // __CHERI_PURE_CAPABILITY__
}

#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register StoreDescriptor::ReceiverRegister() {
  return c1; // kReceiver (MachineType::AnyTagged)
}
// static
constexpr Register StoreDescriptor::NameRegister() {
  return c2; // kName (MachineType::AnyTagged)
}
// static
constexpr Register StoreDescriptor::ValueRegister() {
  return c0; // kValue (MachineType::AnyTagged)
}
#else
// static
constexpr Register StoreDescriptor::ReceiverRegister() { return x1; }
// static
constexpr Register StoreDescriptor::NameRegister() { return x2; }
// static
constexpr Register StoreDescriptor::ValueRegister() { return x0; }
#endif // __CHERI_PURE_CAPABILITY__
// static
constexpr Register StoreDescriptor::SlotRegister() { return x4; }

#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() {
  return c3; // kVector (machineType::AnyTagged)
}
#else
// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() { return x3; }
#endif // __CHERI_PURE_CAPABILITY__

// static
constexpr Register DefineKeyedOwnDescriptor::FlagsRegister() { return x5; }

#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register StoreTransitionDescriptor::MapRegister() {
  return c5; // kMap (machineType::AnyTagged)
}
#else
// static
constexpr Register StoreTransitionDescriptor::MapRegister() { return x5; }
#endif // __CHERI_PURE_CAPABILITY__

// static
#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register ApiGetterDescriptor::HolderRegister() {
  return c0; // kHolder(MachineType::AnyTagged)
}
// static
constexpr Register ApiGetterDescriptor::CallbackRegister() {
  return c3; // kCallback (MachineType::AnyTagged)
}
#else
// static
constexpr Register ApiGetterDescriptor::HolderRegister() { return x0; }
// static
constexpr Register ApiGetterDescriptor::CallbackRegister() { return x3; }
#endif // __CHERI_PURE_CAPABILITY__

// static
#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() {
  return c0; // kObject (MachineType::AnyTagged)
}
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() {
  return c3; // kKey (MachineType::AnyTagged)
}
#else
// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return x0; }
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return x3; }
#endif // __CHERI_PURE_CAPABILITY__

// static
constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return x3;
}
// static
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() { return x4; }

#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() {
  return c0; // kArgument (MachineType::AnyTagged)
}
#else
// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() { return x0; }
#endif // __CHERI_PURE_CAPABILITY__

#if defined(__CHERI_PURE_CAPABILITY__)
// static
constexpr auto TypeofDescriptor::registers() {
  return RegisterArray(c0); // kObject (MachineType::AnyTagged)
}
#else
// static
constexpr auto TypeofDescriptor::registers() { return RegisterArray(x0); }
#endif // __CHERI_PURE_CAPABILITY__

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // x1: target
  // x0: number of arguments
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kFunction (MachineType::AnyTagged)
                       x0); // kActualArgumentsCount (MachineType::Int32)
#else
  return RegisterArray(x1, x0);
#endif // __CHERI_PURE_CAPABILITY__
}

constexpr auto CopyDataPropertiesWithExcludedPropertiesDescriptor::registers() {
  // r1 : the source
  // r0 : the excluded property count
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kSource (MachineType::AnyTagged)
                       c0); // kExcludedPropertyCount (MachineType::AnyTagged)
#else
  return RegisterArray(x1, x0);
#endif // __CHERI_PURE_CAPABILITY__
}

constexpr auto
CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor::registers() {
  // r1 : the source
  // r0 : the excluded property count
  // x2 : the excluded property base
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kSource (MachineType::AnyTagged)
                       x0,  // kExcludedPropertCount (MachineType::IntPtr)
                       x2); // kExcludePropertyBase (MachineType::IntPtr)
#else
  return RegisterArray(x1, x0, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto CallVarargsDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x4 : arguments list length (untagged)
  // x2 : arguments list (FixedArray)
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kTarget (MachineType::AnyTaged)
                       x0,  // kActualArgumentsCount (MachineType::Int32)
		       x4,  // kArgumentsLength (MachineType::Int32)
		       c2); // kArgumentsList (MachineType::AnyTagged)
#else
  return RegisterArray(x1, x0, x4, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto CallForwardVarargsDescriptor::registers() {
  // x1: target
  // x0: number of arguments
  // x2: start index (to supported rest parameters)
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kTarget (MachineType::AnyTagged)
                       x0,  // kActualArgumentCount (MachineType::Int32)
		       x2); // kStartIndex (MachineType::Int32)
#else
  return RegisterArray(x1, x0, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // x1 : function template info
  // x2 : number of arguments (on the stack)
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kFunctionTemplateInfo (MachineType::AnyTagged)
                       x2); // kArgumentsCount (MachineType::IntPtr)
#else
  return RegisterArray(x1, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto CallWithSpreadDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x2 : the object to spread
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kTarget (MachineTYpe::AnyTagged)
                       x0,  // kArgumentsCount (MachineType::Int32)
		       c2); // kSpread (MachineType::AnyTagged)
#else
  return RegisterArray(x1, x0, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // x1 : the target to call
  // x2 : the arguments list
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kTarget (MachineType::AnyTagged)
                       c2); // kArgumentsList (MachineType::AnyTagged)
#else
  return RegisterArray(x1, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto ConstructVarargsDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x3 : the new target
  // x4 : arguments list length (untagged)
  // x2 : arguments list (FixedArray)
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,   // kTarget (MachineType::AnyTagged)
		       c3,   // kNewTarget (MachineType::AnyTagged)
		       x0,   // kActualArgumentsCount (MachineType::Int32)
		       x4,   // kArgumentsLength (MachineType::Int32)
		       c2);  // kArgumentsList (MachineType::AnyTagged)
#else
  return RegisterArray(x1, x3, x0, x4, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // x3: new target
  // x1: target
  // x0: number of arguments
  // x2: start index (to supported rest parameters)
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,   // kTarget (MachineType::AnyTagged)
		       c3,   // kNewTarget (MachineType::AnyTagged)
		       x0,   // kActualArgumentsCount (MachineType::Int32)
		       x2);  // kStartIndex (machineType::Int32)
#else
  return RegisterArray(x1, x3, x0, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x3 : the new target
  // x2 : the object to spread
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,   // kTarget (MachineType::AnyTagged)
		       c3,   // kNewTarget (MachineType::AnyTagged)
		       x0,   // kActualArgumentsCount (MachineType::Int32)
		       c2);  // kSpread (MachineType::AnyTagged)
#else
  return RegisterArray(x1, x3, x0, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // x1 : the target to call
  // x3 : the new target
  // x2 : the arguments list
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kTarget (MachineType::AnyTagged)
		       c3,  // kNewTarget (MachineType::AnyTagged)
		       x2); // kActualArgumentsCount (MachineType::Int32)
#else
  return RegisterArray(x1, x3, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto ConstructStubDescriptor::registers() {
  // x3: new target
  // x1: target
  // x0: number of arguments
  return RegisterArray(x1, x3, x0);
}

// static
#if defined(__CHERI_PURE_CAPABILITY__)
constexpr auto AbortDescriptor::registers() {
  return RegisterArray(c1); // kMessageOrMessageId (MachineType::AnyTagged)
}
#else
constexpr auto AbortDescriptor::registers() { return RegisterArray(x1); }
#endif // __CHERI_PURE_CAPABILITY__

// static
constexpr auto CompareDescriptor::registers() {
  // x1: left operand
  // x0: right operand
#if defined(__CHERI_PURE_CAPABILITY__)
  // TODO(gcjenkinson):CompareDescriptor doesn't defined types
  // for kLeft and kRight, check assumption that these should be capabilities.
  return RegisterArray(c1, c0);
#else
  return RegisterArray(x1, x0);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  // x2: feedback slot
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kLeft (MachineType::AnyTagged)
		       c0,  // kRight (MachineType::AnyTagged)
		       c2); // kSlot (MachineType::UintPtr)
#else
  return RegisterArray(x1, x0, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto BinaryOpDescriptor::registers() {
  // x1: left operand
  // x0: right operand
#if defined(__CHERI_PURE_CAPABILITY__)
  // TODO(gcjenkinson): BinaryOpDescriptor doesn't defined types
  // for kLeft and kRight, check assumption that these should be capabilities.
  return RegisterArray(c1, c0);
#else
  return RegisterArray(x1, x0);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  // x2: feedback slot
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,  // kLeft (MachineType::AnyTagged)
		       c0,  // kRight (MachineType::AnyTagged)
		       x2); // kSlot (MachineType::UintPtr)
#else
  return RegisterArray(x1, x0, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto BinarySmiOp_BaselineDescriptor::registers() {
  // x0: left operand
  // x1: right operand
  // x2: feedback slot
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c0,  // kLeft (MachineDescription::AnyTagged)
		       x1,  // kRight (MachineDescription::TaggedSigned)
		       x2); // kSlot (MachineType::UintPtr)
#else
  return RegisterArray(x0, x1, x2);
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto ApiCallbackDescriptor::registers() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c1,   // kApiFunctionAddress (MachineType::Pointer)
                       x2,   // kArgc (MachineType::IntPtr)
                       c3,   // kCallData (MachineType::AnyTagged)
                       c0);  // kHolder (MachineType::AnyTagged)
#else
  return RegisterArray(x1,   // kApiFunctionAddress
                       x2,   // kArgc
                       x3,   // kCallData
                       x0);  // kHolder
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// static
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c0,   // argument count
                       c2,   // address of first argument
                       c1);  // the target callable to be call
#else
  return RegisterArray(x0,   // argument count
                       x2,   // address of first argument
                       x1);  // the target callable to be call
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(
      c0,   // argument count
      c4,   // address of the first argument
      c1,   // constructor to call
      c3,   // new target
      c2);  // allocation site feedback if available, undefined otherwise
#else
  return RegisterArray(
      x0,   // argument count
      x4,   // address of the first argument
      x1,   // constructor to call
      x3,   // new target
      x2);  // allocation site feedback if available, undefined otherwise
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto ResumeGeneratorDescriptor::registers() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c0,   // the value to pass to the generator
			     // (Machineype::AnyTagged)
                       c1);  // the JSGeneratorObject to resume
			     // (MachineType::AnyTagged)
#else
  return RegisterArray(x0,   // the value to pass to the generator
                       x1);  // the JSGeneratorObject to resume
#endif // __CHERI_PURE_CAPABILITY__
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
#if defined(__CHERI_PURE_CAPABILITY__)
  return RegisterArray(c0,   // kRootRegisterValue (MachineType::Pointer)
                       c1);  // kMicrotaskQueue (MachineType::Pointer)
#else
  return RegisterArray(x0, x1);
#endif // __CHERI_PURE_CAPABILITY__
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64

#endif  // V8_CODEGEN_ARM64_INTERFACE_DESCRIPTORS_ARM64_INL_H_

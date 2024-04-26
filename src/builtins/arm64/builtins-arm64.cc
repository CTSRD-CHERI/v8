// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM64

#include "src/api/api-arguments.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/interface-descriptors-inl.h"
// For interpreter_entry_return_pc_offset. TODO(jkummerow): Drop.
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/register-configuration.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames.h"
#include "src/heap/heap-inl.h"
#include "src/logging/counters.h"
#include "src/objects/cell.h"
#include "src/objects/foreign.h"
#include "src/objects/heap-number.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-generator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/runtime/runtime.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#if defined(V8_OS_WIN)
#include "src/diagnostics/unwinding-info-win64.h"
#endif  // V8_OS_WIN

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void Builtins::Generate_Adaptor(MacroAssembler* masm, Address address) {
  __ CodeEntry();

  __ Mov(kJavaScriptCallExtraArg1Register, ExternalReference::Create(address));
  __ Jump(BUILTIN_CODE(masm->isolate(), AdaptorWithBuiltinExitFrame),
          RelocInfo::CODE_TARGET);
}

static void GenerateTailCallToReturnedCode(MacroAssembler* masm,
                                           Runtime::FunctionId function_id) {
  ASM_CODE_COMMENT(masm);
  // ----------- S t a t e -------------
  //  -- x0 : actual argument count
  //  -- x1 : target function (preserved for callee)
  //  -- x3 : new target (preserved for callee)
  // -----------------------------------
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    // Push a copy of the target function, the new target and the actual
    // argument count.
    __ SmiTag(kJavaScriptCallArgCountRegister);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Push(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
            kJavaScriptCallArgCountRegister.C(), padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Push(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
            kJavaScriptCallArgCountRegister, padreg);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Push another copy as a parameter to the runtime call.
    __ PushArgument(kJavaScriptCallTargetRegister);

    __ CallRuntime(function_id, 1);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c2, c0);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x2, x0);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Restore target function, new target and actual argument count.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(padregc, kJavaScriptCallArgCountRegister.C(),
           kJavaScriptCallNewTargetRegister, kJavaScriptCallTargetRegister);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(padreg, kJavaScriptCallArgCountRegister,
           kJavaScriptCallNewTargetRegister, kJavaScriptCallTargetRegister);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ SmiUntag(kJavaScriptCallArgCountRegister);
  }

#if defined(__CHERI_PURE_CAPABILITY__)
  static_assert(kJavaScriptCallCodeStartRegister == c2, "ABI mismatch");
  __ JumpCodeTObject(c2);
#else // defined(__CHERI_PURE_CAPABILITY__)
  static_assert(kJavaScriptCallCodeStartRegister == x2, "ABI mismatch");
  __ JumpCodeTObject(x2);
#endif // defined(__CHERI_PURE_CAPABILITY__)
}

namespace {

void Generate_JSBuiltinsConstructStubHelper(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0     : number of arguments
  //  -- x1     : constructor function
  //  -- x3     : new target
  //  -- cp     : context
  //  -- lr     : return address
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_JSConstructStubHelper");
  Label stack_overflow;

  __ StackOverflowCheck(x0, &stack_overflow);

  // Enter a construct frame.
  {
    FrameScope scope(masm, StackFrame::CONSTRUCT);
    Label already_aligned;
    Register argc = x0;

    if (FLAG_debug_code) {
      // Check that FrameScope pushed the context on to the stack already.
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Peek(c2, 0);
      __ Cmp(c2, cp);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Peek(x2, 0);
      __ Cmp(x2, cp);
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ Check(eq, AbortReason::kUnexpectedValue);
    }

    // Push number of arguments.
    __ SmiTag(x11, argc);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Push(c11, padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Push(x11, padreg);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Round up to maintain alignment.
    Register slot_count = x2;
    Register slot_count_without_rounding = x12;
    __ Add(slot_count_without_rounding, argc, 1);
    __ Bic(slot_count, slot_count_without_rounding, 1);
    __ Claim(slot_count);

    // Preserve the incoming parameters on the stack.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ LoadRoot(c4, RootIndex::kTheHoleValue);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadRoot(x4, RootIndex::kTheHoleValue);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Compute a pointer to the slot immediately above the location on the
    // stack to which arguments will be later copied.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ SlotAddress(c2, argc);
#else
    __ SlotAddress(x2, argc);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Store padding, if needed.
    __ Tbnz(slot_count_without_rounding, 0, &already_aligned);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Str(padregc, MemOperand(c2));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Str(padreg, MemOperand(x2));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Bind(&already_aligned);

    // TODO(victorgomes): When the arguments adaptor is completely removed, we
    // should get the formal parameter count and copy the arguments in its
    // correct position (including any undefined), instead of delaying this to
    // InvokeFunction.

    // Copy arguments to the expression stack.
    {
      Register count = x2;
#if defined(__CHERI_PURE_CAPABILITY__)
      Register dst = c10;
      Register src = c11;
#else // defined(__CHERI_PURE_CAPABILITY__)
      Register dst = x10;
      Register src = x11;
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ SlotAddress(dst, 0);
      // Poke the hole (receiver).
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Str(c4, MemOperand(dst));
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Str(x4, MemOperand(dst));
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ Add(dst, dst, kSystemPointerSize);  // Skip receiver.
      __ Add(src, fp,
             StandardFrameConstants::kCallerSPOffset +
                 kSystemPointerSize);  // Skip receiver.
      __ Sub(count, argc, kJSArgcReceiverSlots);
#if defined(__CHERI_PURE_CAPABILITY__)
      __ CopyCapabilities(dst, src, count);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ CopyDoubleWords(dst, src, count);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    }

    // ----------- S t a t e -------------
    //  --                           x0: number of arguments (untagged)
    //  --                           x1: constructor function
    //  --                           x3: new target
    // If argc is odd:
    //  --     sp[0*kSystemPointerSize]: the hole (receiver)
    //  --     sp[1*kSystemPointerSize]: argument 1
    //  --             ...
    //  -- sp[(n-1)*kSystemPointerSize]: argument (n - 1)
    //  -- sp[(n+0)*kSystemPointerSize]: argument n
    //  -- sp[(n+1)*kSystemPointerSize]: padding
    //  -- sp[(n+2)*kSystemPointerSize]: padding
    //  -- sp[(n+3)*kSystemPointerSize]: number of arguments (tagged)
    //  -- sp[(n+4)*kSystemPointerSize]: context (pushed by FrameScope)
    // If argc is even:
    //  --     sp[0*kSystemPointerSize]: the hole (receiver)
    //  --     sp[1*kSystemPointerSize]: argument 1
    //  --             ...
    //  -- sp[(n-1)*kSystemPointerSize]: argument (n - 1)
    //  -- sp[(n+0)*kSystemPointerSize]: argument n
    //  -- sp[(n+1)*kSystemPointerSize]: padding
    //  -- sp[(n+2)*kSystemPointerSize]: number of arguments (tagged)
    //  -- sp[(n+3)*kSystemPointerSize]: context (pushed by FrameScope)
    // -----------------------------------

    // Call the function.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ InvokeFunctionWithNewTarget(c1, c3, argc, InvokeType::kCall);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ InvokeFunctionWithNewTarget(x1, x3, argc, InvokeType::kCall);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Restore the context from the frame.
    __ Ldr(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
    // Restore smi-tagged arguments count from the frame. Use fp relative
    // addressing to avoid the circular dependency between padding existence and
    // argc parity.
    __ SmiUntag(x1, MemOperand(fp, ConstructFrameConstants::kLengthOffset));
    // Leave construct frame.
  }

  // Remove caller arguments from the stack and return.
  __ DropArguments(x1, TurboAssembler::kCountIncludesReceiver);
  __ Ret();

  __ Bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ Unreachable();
  }
}

}  // namespace

// The construct stub for ES5 constructor functions and ES6 class constructors.
void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0     : number of arguments
  //  -- x1     : constructor function
  //  -- x3     : new target
  //  -- lr     : return address
  //  -- cp     : context pointer
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_JSConstructStubGeneric");
  FrameScope scope(masm, StackFrame::MANUAL);
  // Enter a construct frame.
  __ EnterFrame(StackFrame::CONSTRUCT);
  Label post_instantiation_deopt_entry, not_create_implicit_receiver;

  if (FLAG_debug_code) {
    // Check that FrameScope pushed the context on to the stack already.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Peek(c2, 0);
    __ Cmp(c2, cp);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Peek(x2, 0);
    __ Cmp(x2, cp);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Check(eq, AbortReason::kUnexpectedValue);
  }

  // Preserve the incoming parameters on the stack.
  __ SmiTag(x0);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(c0, c1, padregc, c3);

  // ----------- S t a t e -------------
  //  --        sp[0*kSystemPointerSize]: new target
  //  -- x1 and sp[1*kSystemPointerSize]: constructor function
  //  --        sp[2*kSystemPointerSize]: number of arguments (tagged) , padding
  //  --        sp[3*kSystemPointerSize]: context (pushed by FrameScope)
  // -----------------------------------
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(x0, x1, padreg, x3);

  // ----------- S t a t e -------------
  //  --        sp[0*kSystemPointerSize]: new target
  //  --        sp[1*kSystemPointerSize]: padding
  //  -- x1 and sp[2*kSystemPointerSize]: constructor function
  //  --        sp[3*kSystemPointerSize]: number of arguments (tagged)
  //  --        sp[4*kSystemPointerSize]: context (pushed by FrameScope)
  // -----------------------------------
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c4, FieldMemOperand(c1, JSFunction::kSharedFunctionInfoOffset));
  __ Ldr(w4, FieldMemOperand(c4, SharedFunctionInfo::kFlagsOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      x4, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));
  __ Ldr(w4, FieldMemOperand(x4, SharedFunctionInfo::kFlagsOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(w4);
  __ JumpIfIsInRange(
      w4, static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor),
      &not_create_implicit_receiver);

  // If not derived class constructor: Allocate the new receiver object.
  __ Call(BUILTIN_CODE(masm->isolate(), FastNewObject), RelocInfo::CODE_TARGET);

  __ B(&post_instantiation_deopt_entry);

  // Else: use TheHoleValue as receiver for constructor call
  __ Bind(&not_create_implicit_receiver);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ LoadRoot(c0, RootIndex::kTheHoleValue);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadRoot(x0, RootIndex::kTheHoleValue);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // ----------- S t a t e -------------
  //  --                                x0: receiver
  //  -- Slot 4 / sp[0*kSystemPointerSize]: new target
  //  -- Slot 3 / sp[1*kSystemPointerSize]: padding
  //  -- Slot 2 / sp[2*kSystemPointerSize]: constructor function
  //  -- Slot 1 / sp[3*kSystemPointerSize]: number of arguments (tagged)
  //  -- Slot 0 / sp[4*kSystemPointerSize]: context
  // -----------------------------------
  // Deoptimizer enters here.
  masm->isolate()->heap()->SetConstructStubCreateDeoptPCOffset(
      masm->pc_offset());

  __ Bind(&post_instantiation_deopt_entry);

  // Restore new target from the top of the stack.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Peek(c3, 0 * kSystemPointerSize);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Peek(x3, 0 * kSystemPointerSize);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Restore constructor function and argument count.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c1, MemOperand(fp, ConstructFrameConstants::kConstructorOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x1, MemOperand(fp, ConstructFrameConstants::kConstructorOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ SmiUntag(x12, MemOperand(fp, ConstructFrameConstants::kLengthOffset));

  // Copy arguments to the expression stack. The called function pops the
  // receiver along with its arguments, so we need an extra receiver on the
  // stack, in case we have to return it later.

  // Overwrite the new target with a receiver.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Poke(c0, 0);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Poke(x0, 0);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Push two further copies of the receiver. One will be popped by the called
  // function. The second acts as padding if the number of arguments plus
  // receiver is odd - pushing receiver twice avoids branching. It also means
  // that we don't have to handle the even and odd cases specially on
  // InvokeFunction's return, as top of stack will be the receiver in either
  // case.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(c0, c0);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(x0, x0);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // ----------- S t a t e -------------
  //  --                              x3: new target
  //  --                             x12: number of arguments (untagged)
  //  --        sp[0*kSystemPointerSize]: implicit receiver (overwrite if argc
  //  odd)
  //  --        sp[1*kSystemPointerSize]: implicit receiver
  //  --        sp[2*kSystemPointerSize]: implicit receiver
  //  --        sp[3*kSystemPointerSize]: padding
  //  -- x1 and sp[4*kSystemPointerSize]: constructor function
  //  --        sp[5*kSystemPointerSize]: number of arguments (tagged)
  //  --        sp[6*kSystemPointerSize]: context
  // -----------------------------------

  // Round the number of arguments down to the next even number, and claim
  // slots for the arguments. If the number of arguments was odd, the last
  // argument will overwrite one of the receivers pushed above.
  Register argc_without_receiver = x11;
  __ Sub(argc_without_receiver, x12, kJSArgcReceiverSlots);
  __ Bic(x10, x12, 1);

  // Check if we have enough stack space to push all arguments.
  Label stack_overflow;
  __ StackOverflowCheck(x10, &stack_overflow);
  __ Claim(x10);

  // TODO(victorgomes): When the arguments adaptor is completely removed, we
  // should get the formal parameter count and copy the arguments in its
  // correct position (including any undefined), instead of delaying this to
  // InvokeFunction.

  // Copy the arguments.
  {
    Register count = x2;
#if defined(__CHERI_PURE_CAPABILITY__)
    Register dst = c10;
    Register src = c11;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register dst = x10;
    Register src = x11;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(count, argc_without_receiver);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Poke(c0, 0);          // Add the receiver.
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Poke(x0, 0);          // Add the receiver.
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ SlotAddress(dst, 1);  // Skip receiver.
    __ Add(src, fp,
           StandardFrameConstants::kCallerSPOffset + kSystemPointerSize);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CopyCapabilities(dst, src, count);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CopyDoubleWords(dst, src, count);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  // Call the function.
  __ Mov(x0, x12);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ InvokeFunctionWithNewTarget(c1, c3, x0, InvokeType::kCall);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ InvokeFunctionWithNewTarget(x1, x3, x0, InvokeType::kCall);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // ----------- S t a t e -------------
  //  -- sp[0*kSystemPointerSize]: implicit receiver
  //  -- sp[1*kSystemPointerSize]: padding
  //  -- sp[2*kSystemPointerSize]: constructor function
  //  -- sp[3*kSystemPointerSize]: number of arguments
  //  -- sp[4*kSystemPointerSize]: context
  // -----------------------------------

  // Store offset of return address for deoptimizer.
  masm->isolate()->heap()->SetConstructStubInvokeDeoptPCOffset(
      masm->pc_offset());

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, do_throw, leave_and_return, check_receiver;

  // If the result is undefined, we jump out to using the implicit receiver.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ CompareRoot(c0, RootIndex::kUndefinedValue);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ CompareRoot(x0, RootIndex::kUndefinedValue);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(ne, &check_receiver);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ Bind(&use_receiver);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Peek(c0, 0 * kSystemPointerSize);
  __ CompareRoot(c0, RootIndex::kTheHoleValue);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Peek(x0, 0 * kSystemPointerSize);
  __ CompareRoot(x0, RootIndex::kTheHoleValue);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(eq, &do_throw);

  __ Bind(&leave_and_return);
  // Restore smi-tagged arguments count from the frame.
  __ SmiUntag(x1, MemOperand(fp, ConstructFrameConstants::kLengthOffset));
  // Leave construct frame.
  __ LeaveFrame(StackFrame::CONSTRUCT);
  // Remove caller arguments from the stack and return.
  __ DropArguments(x1, TurboAssembler::kCountIncludesReceiver);
  __ Ret();

  // Otherwise we do a smi check and fall through to check if the return value
  // is a valid receiver.
  __ bind(&check_receiver);

  // If the result is a smi, it is *not* an object in the ECMA sense.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ JumpIfSmi(c0, &use_receiver);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ JumpIfSmi(x0, &use_receiver);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // If the type of the result (stored in its map) is less than
  // FIRST_JS_RECEIVER_TYPE, it is not an object in the ECMA sense.
  static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ JumpIfObjectType(c0, c4, x5, FIRST_JS_RECEIVER_TYPE, &leave_and_return,
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ JumpIfObjectType(x0, x4, x5, FIRST_JS_RECEIVER_TYPE, &leave_and_return,
#endif // defined(__CHERI_PURE_CAPABILITY__)
                      ge);
  __ B(&use_receiver);

  __ Bind(&do_throw);
  // Restore the context from the frame.
  __ Ldr(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  __ Unreachable();

  __ Bind(&stack_overflow);
  // Restore the context from the frame.
  __ Ldr(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowStackOverflow);
  __ Unreachable();
}
void Builtins::Generate_JSBuiltinsConstructStub(MacroAssembler* masm) {
  Generate_JSBuiltinsConstructStubHelper(masm);
}

void Builtins::Generate_ConstructedNonConstructable(MacroAssembler* masm) {
  FrameScope scope(masm, StackFrame::INTERNAL);
#if defined(__CHERI_PURE_CAPABILITY__)
  // TODO(gcjenkinson): Is this argument actually a capability value?
  __ PushArgument(c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ PushArgument(x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ CallRuntime(Runtime::kThrowConstructedNonConstructable);
  __ Unreachable();
}

static void AssertCodeTIsBaselineAllowClobber(MacroAssembler* masm,
                                              Register code, Register scratch) {
  // Verify that the code kind is baseline code via the CodeKind.
  __ Ldr(scratch, FieldMemOperand(code, CodeT::kFlagsOffset));
  __ DecodeField<CodeT::KindField>(scratch);
  __ Cmp(scratch, Operand(static_cast<int>(CodeKind::BASELINE)));
  __ Assert(eq, AbortReason::kExpectedBaselineData);
}

static void AssertCodeTIsBaseline(MacroAssembler* masm, Register code,
                                  Register scratch) {
  DCHECK(!AreAliased(code, scratch));
  return AssertCodeTIsBaselineAllowClobber(masm, code, scratch);
}

// TODO(v8:11429): Add a path for "not_compiled" and unify the two uses under
// the more general dispatch.
static void GetSharedFunctionInfoBytecodeOrBaseline(MacroAssembler* masm,
                                                    Register sfi_data,
                                                    Register scratch1,
                                                    Label* is_baseline) {
  ASM_CODE_COMMENT(masm);
  Label done;
#if defined(__CHERI_PURE_CAPABILITY__)
  __ CompareObjectType(sfi_data, scratch1, scratch1.X(), CODET_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ CompareObjectType(sfi_data, scratch1, scratch1, CODET_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  if (FLAG_debug_code) {
    Label not_baseline;
    __ B(ne, &not_baseline);
#if defined(__CHERI_PURE_CAPABILITY__)
    AssertCodeTIsBaseline(masm, sfi_data, scratch1.X());
#else // defined(__CHERI_PURE_CAPABILITY__)
    AssertCodeTIsBaseline(masm, sfi_data, scratch1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ B(eq, is_baseline);
    __ Bind(&not_baseline);
  } else {
    __ B(eq, is_baseline);
  }
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Cmp(scratch1.X(), INTERPRETER_DATA_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Cmp(scratch1, INTERPRETER_DATA_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(ne, &done);
  __ LoadTaggedPointerField(
      sfi_data,
      FieldMemOperand(sfi_data, InterpreterData::kBytecodeArrayOffset));
  __ Bind(&done);
}

// static
void Builtins::Generate_ResumeGeneratorTrampoline(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the value to pass to the generator
  //  -- x1 : the JSGeneratorObject to resume
  //  -- lr : return address
  // -----------------------------------
  // Store input value into generator object.
  __ StoreTaggedField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c0, FieldMemOperand(c1, JSGeneratorObject::kInputOrDebugPosOffset));
  __ RecordWriteField(c1, JSGeneratorObject::kInputOrDebugPosOffset, c0,
#else // defined(__CHERI_PURE_CAPABILITY__)
      x0, FieldMemOperand(x1, JSGeneratorObject::kInputOrDebugPosOffset));
  __ RecordWriteField(x1, JSGeneratorObject::kInputOrDebugPosOffset, x0,
#endif // defined(__CHERI_PURE_CAPABILITY__)
                      kLRHasNotBeenSaved, SaveFPRegsMode::kIgnore);
  // Check that x1 is still valid, RecordWrite might have clobbered it.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ AssertGeneratorObject(c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ AssertGeneratorObject(x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Load suspended function and context.
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c4, FieldMemOperand(c1, JSGeneratorObject::kFunctionOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      x4, FieldMemOperand(x1, JSGeneratorObject::kFunctionOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(cp,
#if defined(__CHERI_PURE_CAPABILITY__)
                            FieldMemOperand(c4, JSFunction::kContextOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
                            FieldMemOperand(x4, JSFunction::kContextOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Flood function if we are stepping.
  Label prepare_step_in_if_stepping, prepare_step_in_suspended_generator;
  Label stepping_prepared;
  ExternalReference debug_hook =
      ExternalReference::debug_hook_on_function_call_address(masm->isolate());
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c10, debug_hook);
  __ Ldrsb(x10, MemOperand(c10));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x10, debug_hook);
  __ Ldrsb(x10, MemOperand(x10));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ CompareAndBranch(x10, Operand(0), ne, &prepare_step_in_if_stepping);

  // Flood function if we need to continue stepping in the suspended generator.
  ExternalReference debug_suspended_generator =
      ExternalReference::debug_suspended_generator_address(masm->isolate());
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c10, debug_suspended_generator);
  __ Ldr(x10, MemOperand(c10));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x10, debug_suspended_generator);
  __ Ldr(x10, MemOperand(x10));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ CompareAndBranch(x10, Operand(x1), eq,
                      &prepare_step_in_suspended_generator);
  __ Bind(&stepping_prepared);

  // Check the stack for overflow. We are not trying to catch interruptions
  // (i.e. debug break and preemption) here, so check the "real stack limit".
  Label stack_overflow;
#if defined(__CHERI_PURE_CAPABILITY__)
  __ LoadStackLimit(c10, StackLimitKind::kRealStackLimit);
  __ Cmp(csp, c10);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadStackLimit(x10, StackLimitKind::kRealStackLimit);
  __ Cmp(sp, x10);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(lo, &stack_overflow);

  // Get number of arguments for generator function.
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c10, FieldMemOperand(c4, JSFunction::kSharedFunctionInfoOffset));
#else
      x10, FieldMemOperand(x4, JSFunction::kSharedFunctionInfoOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldrh(w10, FieldMemOperand(
#if defined(__CHERI_PURE_CAPABILITY__)
                   c10, SharedFunctionInfo::kFormalParameterCountOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
                   x10, SharedFunctionInfo::kFormalParameterCountOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ Sub(x10, x10, kJSArgcReceiverSlots);
  // Claim slots for arguments and receiver (rounded up to a multiple of two).
  __ Add(x11, x10, 2);
  __ Bic(x11, x11, 1);
  __ Claim(x11);

  // Store padding (which might be replaced by the last argument).
  __ Sub(x11, x11, 1);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Poke(padregc, Operand(x11, LSL, kSystemPointerSizeLog2));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Poke(padreg, Operand(x11, LSL, kSystemPointerSizeLog2));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Poke receiver into highest claimed slot.
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c5, FieldMemOperand(c1, JSGeneratorObject::kReceiverOffset));
  __ Poke(c5, __ ReceiverOperand(x10));
#else // defined(__CHERI_PURE_CAPABILITY__)
      x5, FieldMemOperand(x1, JSGeneratorObject::kReceiverOffset));
  __ Poke(x5, __ ReceiverOperand(x10));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // ----------- S t a t e -------------
  //  -- x1                       : the JSGeneratorObject to resume
  //  -- x4                       : generator function
  //  -- x10                      : argument count
  //  -- cp                       : generator context
  //  -- lr                       : return address
  //  -- sp[0 .. arg count]       : claimed for receiver and args
  // -----------------------------------

  // Copy the function arguments from the generator object's register file.
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c5,
      FieldMemOperand(c1, JSGeneratorObject::kParametersAndRegistersOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      x5,
      FieldMemOperand(x1, JSGeneratorObject::kParametersAndRegistersOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  {
    Label loop, done;
    __ Cbz(x10, &done);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ SlotAddress(c12, x10);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ SlotAddress(x12, x10);
#endif // defined(__CHERI_PURE_CAPABILITY__)
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Add(c5, c5, Operand(x10, LSL, kTaggedSizeLog2));
    __ Add(c5, c5, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Add(x5, x5, Operand(x10, LSL, kTaggedSizeLog2));
    __ Add(x5, x5, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Bind(&loop);
    __ Sub(x10, x10, 1);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ LoadAnyTaggedField(c11, MemOperand(c5, -kTaggedSize, PreIndex));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadAnyTaggedField(x11, MemOperand(x5, -kTaggedSize, PreIndex));
#endif // defined(__CHERI_PURE_CAPABILITY__)
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Str(c11, MemOperand(c12, -kSystemPointerSize, PostIndex));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Str(x11, MemOperand(x12, -kSystemPointerSize, PostIndex));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Cbnz(x10, &loop);
    __ Bind(&done);
  }

  // Underlying function needs to have bytecode available.
  if (FLAG_debug_code) {
    Label is_baseline;
    __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
        c3, FieldMemOperand(c4, JSFunction::kSharedFunctionInfoOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
        x3, FieldMemOperand(x4, JSFunction::kSharedFunctionInfoOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
        c3, FieldMemOperand(c3, SharedFunctionInfo::kFunctionDataOffset));
    // TODO(gcjenkinson): Fix use of the scratch register in GetSharedFunctionInfoBytecodeOrBaseline
    GetSharedFunctionInfoBytecodeOrBaseline(masm, c3, c0, &is_baseline);
    __ CompareObjectType(c3, c3, x3, BYTECODE_ARRAY_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
        x3, FieldMemOperand(x3, SharedFunctionInfo::kFunctionDataOffset));
    GetSharedFunctionInfoBytecodeOrBaseline(masm, x3, x0, &is_baseline);
    __ CompareObjectType(x3, x3, x3, BYTECODE_ARRAY_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Assert(eq, AbortReason::kMissingBytecodeArray);
    __ bind(&is_baseline);
  }

  // Resume (Ignition/TurboFan) generator object.
  {
    __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
        c0, FieldMemOperand(c4, JSFunction::kSharedFunctionInfoOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
        x0, FieldMemOperand(x4, JSFunction::kSharedFunctionInfoOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Ldrh(w0, FieldMemOperand(
#if defined(__CHERI_PURE_CAPABILITY__)
                    c0, SharedFunctionInfo::kFormalParameterCountOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
                    x0, SharedFunctionInfo::kFormalParameterCountOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    // We abuse new.target both to indicate that this is a resume call and to
    // pass in the generator object.  In ordinary calls, new.target is always
    // undefined because generator functions are non-constructable.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c3, c1);
    __ Mov(c1, c4);
    static_assert(kJavaScriptCallCodeStartRegister == c2, "ABI mismatch");
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x3, x1);
    __ Mov(x1, x4);
    static_assert(kJavaScriptCallCodeStartRegister == x2, "ABI mismatch");
#endif // defined(__CHERI_PURE_CAPABILITY__)
#if defined(__CHERI_PURE_CAPABILITY__)
    __ LoadTaggedPointerField(c2, FieldMemOperand(c1, JSFunction::kCodeOffset));
    __ JumpCodeTObject(c2);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadTaggedPointerField(x2, FieldMemOperand(x1, JSFunction::kCodeOffset));
    __ JumpCodeTObject(x2);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  __ Bind(&prepare_step_in_if_stepping);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    // Push hole as receiver since we do not use it for stepping.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ LoadRoot(c5, RootIndex::kTheHoleValue);
    __ Push(c1, padregc, c4, c5);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadRoot(x5, RootIndex::kTheHoleValue);
    __ Push(x1, padreg, x4, x5);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ CallRuntime(Runtime::kDebugOnFunctionCall);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(padregc, c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(padreg, x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
        c4, FieldMemOperand(c1, JSGeneratorObject::kFunctionOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
        x4, FieldMemOperand(x1, JSGeneratorObject::kFunctionOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ B(&stepping_prepared);

  __ Bind(&prepare_step_in_suspended_generator);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Push(c1, padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Push(x1, padreg);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ CallRuntime(Runtime::kDebugPrepareStepInSuspendedGenerator);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(padregc, c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(padreg, x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
        c4, FieldMemOperand(c1, JSGeneratorObject::kFunctionOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
        x4, FieldMemOperand(x1, JSGeneratorObject::kFunctionOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ B(&stepping_prepared);

  __ bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ Unreachable();  // This should be unreachable.
  }
}

namespace {

// Called with the native C calling convention. The corresponding function
// signature is either:
//
//   using JSEntryFunction = GeneratedCode<Address(
//       Address root_register_value, Address new_target, Address target,
//       Address receiver, intptr_t argc, Address** argv)>;
// or
//   using JSEntryFunction = GeneratedCode<Address(
//       Address root_register_value, MicrotaskQueue* microtask_queue)>;
//
// Input is either:
//   x0: root_register_value.
//   x1: new_target.
//   x2: target.
//   x3: receiver.
//   x4: argc.
//   x5: argv.
// or
//   x0: root_register_value.
//   x1: microtask_queue.
// Output:
//   x0: result.
void Generate_JSEntryVariant(MacroAssembler* masm, StackFrame::Type type,
                             Builtin entry_trampoline) {
  Label invoke, handler_entry, exit;

  {
    NoRootArrayScope no_root_array(masm);

#if defined(V8_OS_WIN)
    // In order to allow Windows debugging tools to reconstruct a call stack, we
    // must generate information describing how to recover at least fp, sp, and
    // pc for the calling frame. Here, JSEntry registers offsets to
    // xdata_encoder which then emits the offset values as part of the unwind
    // data accordingly.
    win64_unwindinfo::XdataEncoder* xdata_encoder = masm->GetXdataEncoder();
    if (xdata_encoder) {
      xdata_encoder->onFramePointerAdjustment(
          EntryFrameConstants::kDirectCallerFPOffset,
          EntryFrameConstants::kDirectCallerSPOffset);
    }
#endif

    __ PushCalleeSavedRegisters();

    // Set up the reserved register for 0.0.
    __ Fmov(fp_zero, 0.0);

    // Initialize the root register.
    // C calling convention. The first argument is passed in x0.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(kRootRegister, c0);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(kRootRegister, x0);
#endif // defined(__CHERI_PURE_CAPABILITY__)

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
    // Initialize the pointer cage base register.
    __ LoadRootRelative(kPtrComprCageBaseRegister,
                        IsolateData::cage_base_offset());
#endif
  }

  // Set up fp. It points to the {fp, lr} pair pushed as the last step in
  // PushCalleeSavedRegisters.
  static_assert(
      EntryFrameConstants::kCalleeSavedRegisterBytesPushedAfterFpLrPair == 0);
  static_assert(EntryFrameConstants::kOffsetToCalleeSavedRegisters == 0);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(fp, csp);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(fp, sp);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Build an entry frame (see layout below).

  // Push frame type markers.
  __ Mov(x12, StackFrame::TypeToMarker(type));
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(c12, czr);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(x12, xzr);
#endif // defined(__CHERI_PURE_CAPABILITY__)

#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c11, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                        masm->isolate()));
  __ Ldr(c10, MemOperand(c11));  // c10 = C entry FP.
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                        masm->isolate()));
  __ Ldr(x10, MemOperand(x11));  // x10 = C entry FP.
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Clear c_entry_fp, now we've loaded its value to be pushed on the stack.
  // If the c_entry_fp is not already zero and we don't clear it, the
  // SafeStackFrameIterator will assume we are executing C++ and miss the JS
  // frames on top.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(czr, MemOperand(c11));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(xzr, MemOperand(x11));
#endif // defined(__CHERI_PURE_CAPABILITY__)


  // Set js_entry_sp if this is the outermost JS call.
  Label done;
  ExternalReference js_entry_sp = ExternalReference::Create(
      IsolateAddressId::kJSEntrySPAddress, masm->isolate());
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c12, js_entry_sp);
  __ Ldr(c11, MemOperand(c12));  // x11 = previous JS entry SP.
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x12, js_entry_sp);
  __ Ldr(x11, MemOperand(x12));  // x11 = previous JS entry SP.
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Select between the inner and outermost frame marker, based on the JS entry
  // sp. We assert that the inner marker is zero, so we can use xzr to save a
  // move instruction.
  DCHECK_EQ(StackFrame::INNER_JSENTRY_FRAME, 0);
  __ Cmp(x11, 0);  // If x11 is zero, this is the outermost frame.
  // x11 = JS entry frame marker.
  __ Csel(x11, xzr, StackFrame::OUTERMOST_JSENTRY_FRAME, ne);
  __ B(ne, &done);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(fp, MemOperand(c12));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(fp, MemOperand(x12));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ Bind(&done);

#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(c10, c11);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(x10, x11);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // The frame set up looks like this:
  // sp[0] : JS entry frame marker.
  // sp[1] : C entry FP.
  // sp[2] : stack frame marker (0).
  // sp[3] : stack frame marker (type).
  // sp[4] : saved fp   <- fp points here.
  // sp[5] : saved lr
  // sp[6,24) : other saved registers

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the pending exception.
  __ B(&invoke);

  // Prevent the constant pool from being emitted between the record of the
  // handler_entry position and the first instruction of the sequence here.
  // There is no risk because Assembler::Emit() emits the instruction before
  // checking for constant pool emission, but we do not want to depend on
  // that.
  {
    Assembler::BlockPoolsScope block_pools(masm);

    // Store the current pc as the handler offset. It's used later to create the
    // handler table.
    __ BindExceptionHandler(&handler_entry);
    masm->isolate()->builtins()->SetJSEntryHandlerOffset(handler_entry.pos());

    // Caught exception: Store result (exception) in the pending exception
    // field in the JSEnv and return a failure sentinel. Coming in here the
    // fp will be invalid because UnwindAndFindHandler sets it to 0 to
    // signal the existence of the JSEntry frame.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c10,
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x10,
#endif // defined(__CHERI_PURE_CAPABILITY__)
           ExternalReference::Create(IsolateAddressId::kPendingExceptionAddress,
                                     masm->isolate()));
  }
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(c0, MemOperand(c10));
  __ LoadRoot(c0, RootIndex::kException);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(x0, MemOperand(x10));
  __ LoadRoot(x0, RootIndex::kException);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(&exit);

  // Invoke: Link this frame into the handler chain.
  __ Bind(&invoke);

  // Push new stack handler.
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize,
                "Unexpected offset for StackHandlerConstants::kSize");
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize,
                "Unexpected offset for StackHandlerConstants::kNextOffset");

  // Link the current handler as the next handler.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        masm->isolate()));
  __ Ldr(c10, MemOperand(c11));
  __ Push(padregc, c10);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        masm->isolate()));
  __ Ldr(x10, MemOperand(x11));
  __ Push(padreg, x10);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Set this new handler as the current one.
  {
    UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = temps.AcquireC();
    __ Mov(scratch, csp);
    __ Str(scratch, MemOperand(c11));
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = temps.AcquireX();
    __ Mov(scratch, sp);
    __ Str(scratch, MemOperand(x11));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  // If an exception not caught by another handler occurs, this handler
  // returns control to the code after the B(&invoke) above, which
  // restores all callee-saved registers (including cp and fp) to their
  // saved values before returning a failure to C.
  //
  // Invoke the function by calling through JS entry trampoline builtin and
  // pop the faked function when we return.
  Handle<CodeT> trampoline_code =
      masm->isolate()->builtins()->code_handle(entry_trampoline);
  __ Call(trampoline_code, RelocInfo::CODE_TARGET);

  // Pop the stack handler and unlink this frame from the handler chain.
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize,
                "Unexpected offset for StackHandlerConstants::kNextOffset");
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Pop(c10, padregc);
  __ Mov(c11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        masm->isolate()));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Pop(x10, padreg);
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        masm->isolate()));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Drop(StackHandlerConstants::kSlotCount - 2);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(c10, MemOperand(c11));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(x10, MemOperand(x11));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ Bind(&exit);
  // x0 holds the result.
  // The stack pointer points to the top of the entry frame pushed on entry from
  // C++ (at the beginning of this stub):
  // sp[0] : JS entry frame marker.
  // sp[1] : C entry FP.
  // sp[2] : stack frame marker (0).
  // sp[3] : stack frame marker (type).
  // sp[4] : saved fp   <- fp might point here, or might be zero.
  // sp[5] : saved lr
  // sp[6,24) : other saved registers

  // Check if the current stack frame is marked as the outermost JS frame.
  Label non_outermost_js_2;
  {
#if defined(__CHERI_PURE_CAPABILITY__)
   Register c_entry_fp = c11;
    __ PeekPair(c10, c_entry_fp, 0);
    __ Cmp(c10, StackFrame::OUTERMOST_JSENTRY_FRAME);
    __ B(ne, &non_outermost_js_2);
    __ Mov(c12, js_entry_sp);
    __ Str(czr, MemOperand(c12));
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register c_entry_fp = x11;
    __ PeekPair(x10, c_entry_fp, 0);
    __ Cmp(x10, StackFrame::OUTERMOST_JSENTRY_FRAME);
    __ B(ne, &non_outermost_js_2);
    __ Mov(x12, js_entry_sp);
    __ Str(xzr, MemOperand(x12));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Bind(&non_outermost_js_2);

    // Restore the top frame descriptors from the stack.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c12, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x12, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
#endif // defined(__CHERI_PURE_CAPABILITY__)
                                          masm->isolate()));
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Str(c_entry_fp, MemOperand(c12));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Str(c_entry_fp, MemOperand(x12));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  // Reset the stack to the callee saved registers.
  static_assert(
      EntryFrameConstants::kFixedFrameSize % (2 * kSystemPointerSize) == 0,
      "Size of entry frame is not a multiple of 16 bytes");
  __ Drop(EntryFrameConstants::kFixedFrameSize / kSystemPointerSize);
  // Restore the callee-saved registers and return.
  __ PopCalleeSavedRegisters();
  __ Ret();
}

}  // namespace

void Builtins::Generate_JSEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::ENTRY, Builtin::kJSEntryTrampoline);
}

void Builtins::Generate_JSConstructEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::CONSTRUCT_ENTRY,
                          Builtin::kJSConstructEntryTrampoline);
}

void Builtins::Generate_JSRunMicrotasksEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::ENTRY,
                          Builtin::kRunMicrotasksTrampoline);
}

// Input:
//   x1: new.target.
//   x2: function.
//   x3: receiver.
//   x4: argc.
//   x5: argv.
// Output:
//   x0: result.
static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
#if defined(__CHERI_PURE_CAPABILITY__)
  Register new_target = c1;
  Register function = c2;
  Register receiver = c3;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register new_target = x1;
  Register function = x2;
  Register receiver = x3;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register argc = x4;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register argv = c5;
  Register scratch = c10;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register argv = x5;
  Register scratch = x10;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register slots_to_claim = x11;

  {
    // Enter an internal frame.
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Setup the context (we need to use the caller context from the isolate).
    __ Mov(scratch, ExternalReference::Create(IsolateAddressId::kContextAddress,
                                              masm->isolate()));
    __ Ldr(cp, MemOperand(scratch));

    // Claim enough space for the arguments and the function, including an
    // optional slot of padding.
    constexpr int additional_slots = 2;
    __ Add(slots_to_claim, argc, additional_slots);
    __ Bic(slots_to_claim, slots_to_claim, 1);

    // Check if we have enough stack space to push all arguments.
    Label enough_stack_space, stack_overflow;
    __ StackOverflowCheck(slots_to_claim, &stack_overflow);
    __ B(&enough_stack_space);

    __ Bind(&stack_overflow);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ Unreachable();

    __ Bind(&enough_stack_space);
    __ Claim(slots_to_claim);

    // Store padding (which might be overwritten).
    __ SlotAddress(scratch, slots_to_claim);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Str(padregc, MemOperand(scratch, -kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Str(padreg, MemOperand(scratch, -kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Store receiver on the stack.
    __ Poke(receiver, 0);
    // Store function on the stack.
    __ SlotAddress(scratch, argc);
    __ Str(function, MemOperand(scratch));

    // Copy arguments to the stack in a loop, in reverse order.
    // x4: argc.
    // x5: argv.
    Label loop, done;

    // Skip the argument set up if we have no arguments.
    __ Cmp(argc, JSParameterCount(0));
    __ B(eq, &done);

    // scratch has been set to point to the location of the function, which
    // marks the end of the argument copy.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ SlotAddress(c0, 1);  // Skips receiver.
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ SlotAddress(x0, 1);  // Skips receiver.
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Bind(&loop);
    // Load the handle.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Ldr(c11, MemOperand(argv, kSystemPointerSize, PostIndex));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Ldr(x11, MemOperand(argv, kSystemPointerSize, PostIndex));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    // Dereference the handle.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Ldr(c11, MemOperand(c11));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Ldr(x11, MemOperand(x11));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    // Poke the result into the stack.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Str(c11, MemOperand(c0, kSystemPointerSize, PostIndex));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Str(x11, MemOperand(x0, kSystemPointerSize, PostIndex));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    // Loop if we've not reached the end of copy marker.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Cmp(c0, scratch);
#else // defined(_CHERI_PURE_CAPABILITY__)
    __ Cmp(x0, scratch);
#endif // defined(_CHERI_PURE_CAPABILITY__)
    __ B(lt, &loop);

    __ Bind(&done);

    __ Mov(x0, argc);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c3, new_target);
    __ Mov(c1, function);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x3, new_target);
    __ Mov(x1, function);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    // x0: argc.
    // x1: function.
    // x3: new.target.

    // Initialize all JavaScript callee-saved registers, since they will be seen
    // by the garbage collector as part of handlers.
    // The original values have been saved in JSEntry.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ LoadRoot(c19, RootIndex::kUndefinedValue);
    __ Mov(c20, c19);
    __ Mov(c21, c19);
    __ Mov(c22, c19);
    __ Mov(c23, c19);
    __ Mov(c24, c19);
    __ Mov(c25, c19);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadRoot(x19, RootIndex::kUndefinedValue);
    __ Mov(x20, x19);
    __ Mov(x21, x19);
    __ Mov(x22, x19);
    __ Mov(x23, x19);
    __ Mov(x24, x19);
    __ Mov(x25, x19);
#endif // defined(__CHERI_PURE_CAPABILITY__)
#ifndef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c28, c19);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x28, x19);
#endif // defined(__CHERI_PURE_CAPABILITY__)
#endif
    // Don't initialize the reserved registers.
    // x26 : root register (kRootRegister).
    // x27 : context pointer (cp).
    // x28 : pointer cage base register (kPtrComprCageBaseRegister).
    // x29 : frame pointer (fp).

    Handle<CodeT> builtin = is_construct
                                ? BUILTIN_CODE(masm->isolate(), Construct)
                                : masm->isolate()->builtins()->Call();
    __ Call(builtin, RelocInfo::CODE_TARGET);

    // Exit the JS internal frame and remove the parameters (except function),
    // and return.
  }

  // Result is in x0. Return.
  __ Ret();
}

void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}

void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}

void Builtins::Generate_RunMicrotasksTrampoline(MacroAssembler* masm) {
  // This expects two C++ function parameters passed by Invoke() in
  // execution.cc.
  //   x0: root_register_value
  //   x1: microtask_queue

#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(RunMicrotasksDescriptor::MicrotaskQueueRegister(), c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(RunMicrotasksDescriptor::MicrotaskQueueRegister(), x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Jump(BUILTIN_CODE(masm->isolate(), RunMicrotasks), RelocInfo::CODE_TARGET);
}

static void ReplaceClosureCodeWithOptimizedCode(MacroAssembler* masm,
                                                Register optimized_code,
                                                Register closure) {
  ASM_CODE_COMMENT(masm);
  DCHECK(!AreAliased(optimized_code, closure));
  // Store code entry in the closure.
  __ AssertCodeT(optimized_code);
  __ StoreTaggedField(optimized_code,
                      FieldMemOperand(closure, JSFunction::kCodeOffset));
  __ RecordWriteField(closure, JSFunction::kCodeOffset, optimized_code,
                      kLRHasNotBeenSaved, SaveFPRegsMode::kIgnore,
                      SmiCheck::kOmit);
}

static void LeaveInterpreterFrame(MacroAssembler* masm, Register scratch1,
                                  Register scratch2) {
  Register params_size = scratch1;
  // Get the size of the formal parameters + receiver (in bytes).
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(params_size.C(),
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(params_size,
#endif // defined(__CHERI_PURE_CAPABILITY__)
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ Ldr(params_size.W(),
#if defined(__CHERI_PURE_CAPABILITY__)
         FieldMemOperand(params_size.C(), BytecodeArray::kParameterSizeOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
         FieldMemOperand(params_size, BytecodeArray::kParameterSizeOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  Register actual_params_size = scratch2;
  // Compute the size of the actual parameters + receiver (in bytes).
  __ Ldr(actual_params_size,
         MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ lsl(actual_params_size, actual_params_size, kSystemPointerSizeLog2);

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ Cmp(params_size, actual_params_size);
  __ B(ge, &corrected_args_count);
  __ Mov(params_size, actual_params_size);
  __ Bind(&corrected_args_count);

  // Leave the frame (also dropping the register file).
  __ LeaveFrame(StackFrame::INTERPRETED);

  // Drop receiver + arguments.
  if (FLAG_debug_code) {
    __ Tst(params_size, kSystemPointerSize - 1);
    __ Check(eq, AbortReason::kUnexpectedValue);
  }
  __ Lsr(params_size, params_size, kSystemPointerSizeLog2);
  __ DropArguments(params_size);
}

// Tail-call |function_id| if |actual_state| == |expected_state|
static void TailCallRuntimeIfStateEquals(MacroAssembler* masm,
                                         Register actual_state,
                                         TieringState expected_state,
                                         Runtime::FunctionId function_id) {
  ASM_CODE_COMMENT(masm);
  Label no_match;
  __ CompareAndBranch(actual_state, Operand(static_cast<int>(expected_state)),
                      ne, &no_match);
  GenerateTailCallToReturnedCode(masm, function_id);
  __ bind(&no_match);
}

static void TailCallOptimizedCodeSlot(MacroAssembler* masm,
                                      Register optimized_code_entry,
                                      Register scratch) {
  // ----------- S t a t e -------------
  //  -- x0 : actual argument count
  //  -- x3 : new target (preserved for callee if needed, and caller)
  //  -- x1 : target function (preserved for callee if needed, and caller)
  // -----------------------------------
  ASM_CODE_COMMENT(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(!AreAliased(c1, c3, optimized_code_entry, scratch));
#else // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(!AreAliased(x1, x3, optimized_code_entry, scratch));
#endif // defined(__CHERI_PURE_CAPABILITY__)

#if defined(__CHERI_PURE_CAPABILITY__)
  Register closure = c1;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register closure = x1;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Label heal_optimized_code_slot;

  // If the optimized code is cleared, go to runtime to update the optimization
  // marker field.
  __ LoadWeakValue(optimized_code_entry, optimized_code_entry,
                   &heal_optimized_code_slot);

  // Check if the optimized code is marked for deopt. If it is, call the
  // runtime to clear it.
  __ AssertCodeT(optimized_code_entry);
  __ JumpIfCodeTIsMarkedForDeoptimization(optimized_code_entry, scratch,
                                          &heal_optimized_code_slot);

  // Optimized code is good, get it into the closure and link the closure into
  // the optimized functions list, then tail call the optimized code.
  ReplaceClosureCodeWithOptimizedCode(masm, optimized_code_entry, closure);
#if defined(__CHERI_PURE_CAPABILITY__)
  static_assert(kJavaScriptCallCodeStartRegister == c2, "ABI mismatch");
  __ Move(c2, optimized_code_entry);
  __ JumpCodeTObject(c2);
#else // defined(__CHERI_PURE_CAPABILITY__)
  static_assert(kJavaScriptCallCodeStartRegister == x2, "ABI mismatch");
  __ Move(x2, optimized_code_entry);
  __ JumpCodeTObject(x2);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Optimized code slot contains deoptimized code or code is cleared and
  // optimized code marker isn't updated. Evict the code, update the marker
  // and re-enter the closure's code.
  __ bind(&heal_optimized_code_slot);
  GenerateTailCallToReturnedCode(masm, Runtime::kHealOptimizedCodeSlot);
}

static void MaybeOptimizeCode(MacroAssembler* masm, Register feedback_vector,
                              Register tiering_state) {
  // ----------- S t a t e -------------
  //  -- x0 : actual argument count
  //  -- x3 : new target (preserved for callee if needed, and caller)
  //  -- x1 : target function (preserved for callee if needed, and caller)
  //  -- feedback vector (preserved for caller if needed)
  //  -- tiering_state : int32 containing non-zero tiering state.
  // -----------------------------------
  ASM_CODE_COMMENT(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(!AreAliased(feedback_vector, c1, c3, tiering_state));
#else // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(!AreAliased(feedback_vector, x1, x3, tiering_state));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  TailCallRuntimeIfStateEquals(masm, tiering_state,
                               TieringState::kRequestTurbofan_Synchronous,
                               Runtime::kCompileTurbofan_Synchronous);
  TailCallRuntimeIfStateEquals(masm, tiering_state,
                               TieringState::kRequestTurbofan_Concurrent,
                               Runtime::kCompileTurbofan_Concurrent);

  __ Unreachable();
}

// Advance the current bytecode offset. This simulates what all bytecode
// handlers do upon completion of the underlying operation. Will bail out to a
// label if the bytecode (without prefix) is a return bytecode. Will not advance
// the bytecode offset if the current bytecode is a JumpLoop, instead just
// re-executing the JumpLoop to jump to the correct bytecode.
static void AdvanceBytecodeOffsetOrReturn(MacroAssembler* masm,
                                          Register bytecode_array,
                                          Register bytecode_offset,
                                          Register bytecode, Register scratch1,
                                          Register scratch2, Label* if_return) {
  ASM_CODE_COMMENT(masm);
  Register bytecode_size_table = scratch1;

  // The bytecode offset value will be increased by one in wide and extra wide
  // cases. In the case of having a wide or extra wide JumpLoop bytecode, we
  // will restore the original bytecode. In order to simplify the code, we have
  // a backup of it.
  Register original_bytecode_offset = scratch2;
  DCHECK(!AreAliased(bytecode_array, bytecode_offset, bytecode_size_table,
                     bytecode, original_bytecode_offset));

  __ Mov(bytecode_size_table, ExternalReference::bytecode_size_table_address());
  __ Mov(original_bytecode_offset, bytecode_offset);

  // Check if the bytecode is a Wide or ExtraWide prefix bytecode.
  Label process_bytecode, extra_wide;
  static_assert(0 == static_cast<int>(interpreter::Bytecode::kWide));
  static_assert(1 == static_cast<int>(interpreter::Bytecode::kExtraWide));
  static_assert(2 == static_cast<int>(interpreter::Bytecode::kDebugBreakWide));
  static_assert(3 ==
                static_cast<int>(interpreter::Bytecode::kDebugBreakExtraWide));
  __ Cmp(bytecode, Operand(0x3));
  __ B(hi, &process_bytecode);
  __ Tst(bytecode, Operand(0x1));
  // The code to load the next bytecode is common to both wide and extra wide.
  // We can hoist them up here since they do not modify the flags after Tst.
  __ Add(bytecode_offset, bytecode_offset, Operand(1));
  __ Ldrb(bytecode, MemOperand(bytecode_array, bytecode_offset));
  __ B(ne, &extra_wide);

  // Update table to the wide scaled table.
  __ Add(bytecode_size_table, bytecode_size_table,
         Operand(kByteSize * interpreter::Bytecodes::kBytecodeCount));
  __ B(&process_bytecode);

  __ Bind(&extra_wide);
  // Update table to the extra wide scaled table.
  __ Add(bytecode_size_table, bytecode_size_table,
         Operand(2 * kByteSize * interpreter::Bytecodes::kBytecodeCount));

  __ Bind(&process_bytecode);

// Bailout to the return label if this is a return bytecode.
#define JUMP_IF_EQUAL(NAME)                                              \
  __ Cmp(x1, Operand(static_cast<int>(interpreter::Bytecode::k##NAME))); \
  __ B(if_return, eq);
  RETURN_BYTECODE_LIST(JUMP_IF_EQUAL)
#undef JUMP_IF_EQUAL

  // If this is a JumpLoop, re-execute it to perform the jump to the beginning
  // of the loop.
  Label end, not_jump_loop;
  __ Cmp(bytecode, Operand(static_cast<int>(interpreter::Bytecode::kJumpLoop)));
  __ B(ne, &not_jump_loop);
  // We need to restore the original bytecode_offset since we might have
  // increased it to skip the wide / extra-wide prefix bytecode.
  __ Mov(bytecode_offset, original_bytecode_offset);
  __ B(&end);

  __ bind(&not_jump_loop);
  // Otherwise, load the size of the current bytecode and advance the offset.
  __ Ldrb(scratch1.W(), MemOperand(bytecode_size_table, bytecode));
  __ Add(bytecode_offset, bytecode_offset, scratch1);

  __ Bind(&end);
}

// Read off the optimization state in the feedback vector and check if there
// is optimized code or a tiering state that needs to be processed.
static void LoadTieringStateAndJumpIfNeedsProcessing(
    MacroAssembler* masm, Register optimization_state, Register feedback_vector,
    Label* has_optimized_code_or_state) {
  ASM_CODE_COMMENT(masm);
  DCHECK(!AreAliased(optimization_state, feedback_vector));
  __ Ldrh(optimization_state,
          FieldMemOperand(feedback_vector, FeedbackVector::kFlagsOffset));
  __ TestAndBranchIfAnySet(
      optimization_state,
      FeedbackVector::kHasOptimizedCodeOrTieringStateIsAnyRequestMask,
      has_optimized_code_or_state);
}

static void MaybeOptimizeCodeOrTailCallOptimizedCodeSlot(
    MacroAssembler* masm, Register optimization_state,
    Register feedback_vector) {
  ASM_CODE_COMMENT(masm);
  DCHECK(!AreAliased(optimization_state, feedback_vector));
  Label maybe_has_optimized_code;
  // Check if optimized code is available
  __ TestAndBranchIfAllClear(optimization_state,
                             FeedbackVector::kTieringStateIsAnyRequestMask,
                             &maybe_has_optimized_code);

  Register tiering_state = optimization_state;
  __ DecodeField<FeedbackVector::TieringStateBits>(tiering_state);
  MaybeOptimizeCode(masm, feedback_vector, tiering_state);

  __ bind(&maybe_has_optimized_code);
#if defined(__CHERI_PURE_CAPABILITY__)
  Register optimized_code_entry = c7;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register optimized_code_entry = x7;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadAnyTaggedField(
      optimized_code_entry,
      FieldMemOperand(feedback_vector,
                      FeedbackVector::kMaybeOptimizedCodeOffset));
  TailCallOptimizedCodeSlot(masm, optimized_code_entry, x4);
}

namespace {

void ResetBytecodeAge(MacroAssembler* masm, Register bytecode_array) {
  static_assert(BytecodeArray::kNoAgeBytecodeAge == 0);
  __ Strh(wzr,
          FieldMemOperand(bytecode_array, BytecodeArray::kBytecodeAgeOffset));
}

void ResetFeedbackVectorOsrUrgency(MacroAssembler* masm,
                                   Register feedback_vector, Register scratch) {
  DCHECK(!AreAliased(feedback_vector, scratch));
  __ Ldrb(scratch,
          FieldMemOperand(feedback_vector, FeedbackVector::kOsrStateOffset));
  __ And(scratch, scratch,
         Operand(FeedbackVector::MaybeHasOptimizedOsrCodeBit::kMask));
  __ Strb(scratch,
          FieldMemOperand(feedback_vector, FeedbackVector::kOsrStateOffset));
}

}  // namespace

// static
void Builtins::Generate_BaselineOutOfLinePrologue(MacroAssembler* masm) {
  UseScratchRegisterScope temps(masm);
  // Need a few extra registers
#if defined(__CHERI_PURE_CAPABILITY__)
  temps.Include(c14, c15);
#else // defined(__CHERI_PURE_CAPABILITY__)
  temps.Include(x14, x15);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  auto descriptor =
      Builtins::CallInterfaceDescriptorFor(Builtin::kBaselineOutOfLinePrologue);
  Register closure = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kClosure);
  // Load the feedback vector from the closure.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register feedback_vector = temps.AcquireC();
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register feedback_vector = temps.AcquireX();
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      feedback_vector,
      FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedPointerField(
      feedback_vector, FieldMemOperand(feedback_vector, Cell::kValueOffset));
  if (FLAG_debug_code) {
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CompareObjectType(feedback_vector, c4, x4, FEEDBACK_VECTOR_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CompareObjectType(feedback_vector, x4, x4, FEEDBACK_VECTOR_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Assert(eq, AbortReason::kExpectedFeedbackVector);
  }

  // Check the tiering state.
  Label has_optimized_code_or_state;
  Register optimization_state = temps.AcquireW();
  LoadTieringStateAndJumpIfNeedsProcessing(
      masm, optimization_state, feedback_vector, &has_optimized_code_or_state);

  {
    UseScratchRegisterScope temps(masm);
    ResetFeedbackVectorOsrUrgency(masm, feedback_vector, temps.AcquireW());
  }

  // Increment invocation count for the function.
  {
    UseScratchRegisterScope temps(masm);
    Register invocation_count = temps.AcquireW();
    __ Ldr(invocation_count,
           FieldMemOperand(feedback_vector,
                           FeedbackVector::kInvocationCountOffset));
    __ Add(invocation_count, invocation_count, Operand(1));
    __ Str(invocation_count,
           FieldMemOperand(feedback_vector,
                           FeedbackVector::kInvocationCountOffset));
  }

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  {
    ASM_CODE_COMMENT_STRING(masm, "Frame Setup");
    // Normally the first thing we'd do here is Push(lr, fp), but we already
    // entered the frame in BaselineCompiler::Prologue, as we had to use the
    // value lr before the call to this BaselineOutOfLinePrologue builtin.

    Register callee_context = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kCalleeContext);
    Register callee_js_function = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kClosure);
    __ Push(callee_context, callee_js_function);
    DCHECK_EQ(callee_js_function, kJavaScriptCallTargetRegister);
    DCHECK_EQ(callee_js_function, kJSFunctionRegister);

    Register argc = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kJavaScriptCallArgCount);
    // We'll use the bytecode for both code age/OSR resetting, and pushing onto
    // the frame, so load it into a register.
    Register bytecode_array = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kInterpreterBytecodeArray);
    ResetBytecodeAge(masm, bytecode_array);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Push(argc.C(), bytecode_array);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Push(argc, bytecode_array);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Baseline code frames store the feedback vector where interpreter would
    // store the bytecode offset.
    if (FLAG_debug_code) {
#if defined(__CHERI_PURE_CAPABILITY__)
      __ CompareObjectType(feedback_vector, c4, x4, FEEDBACK_VECTOR_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ CompareObjectType(feedback_vector, x4, x4, FEEDBACK_VECTOR_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ Assert(eq, AbortReason::kExpectedFeedbackVector);
    }
    // Our stack is currently aligned. We have have to push something along with
    // the feedback vector to keep it that way -- we may as well start
    // initialising the register frame.
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    __ Push(feedback_vector, kInterpreterAccumulatorRegister);
  }

  Label call_stack_guard;
  Register frame_size = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kStackFrameSize);
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt check");
    // Stack check. This folds the checks for both the interrupt stack limit
    // check and the real stack limit into one by just checking for the
    // interrupt limit. The interrupt limit is either equal to the real stack
    // limit or tighter. By ensuring we have space until that limit after
    // building the frame we can quickly precheck both at once.
    UseScratchRegisterScope temps(masm);

#if defined(__CHERI_PURE_CAPABILITY__)
    Register sp_minus_frame_size = temps.AcquireC();
    __ Sub(sp_minus_frame_size, csp, frame_size);
    Register interrupt_limit = temps.AcquireC();
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register sp_minus_frame_size = temps.AcquireX();
    __ Sub(sp_minus_frame_size, sp, frame_size);
    Register interrupt_limit = temps.AcquireX();
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadStackLimit(interrupt_limit, StackLimitKind::kInterruptStackLimit);
    __ Cmp(sp_minus_frame_size, interrupt_limit);
    __ B(lo, &call_stack_guard);
  }

  // Do "fast" return to the caller pc in lr.
  if (FLAG_debug_code) {
    // The accumulator should already be "undefined", we don't have to load it.
    __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    __ Assert(eq, AbortReason::kUnexpectedValue);
  }
  __ Ret();

  __ bind(&has_optimized_code_or_state);
  {
    ASM_CODE_COMMENT_STRING(masm, "Optimized marker check");
    // Drop the frame created by the baseline call.
    __ Pop<TurboAssembler::kAuthLR>(fp, lr);
    MaybeOptimizeCodeOrTailCallOptimizedCodeSlot(masm, optimization_state,
                                                 feedback_vector);
    __ Trap();
  }

  __ bind(&call_stack_guard);
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt call");
    Register new_target = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kJavaScriptCallNewTarget);

    FrameScope frame_scope(masm, StackFrame::INTERNAL);
    // Save incoming new target or generator
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Push(padregc, new_target);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Push(padreg, new_target);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ SmiTag(frame_size);
    __ PushArgument(frame_size);
    __ CallRuntime(Runtime::kStackGuardWithGap);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(new_target, padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(new_target, padreg);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ Ret();
}

// Generate code for entering a JS function with the interpreter.
// On entry to the function the receiver and arguments have been pushed on the
// stack left to right.
//
// The live registers are:
//   - x0: actual argument count
//   - x1: the JS function object being called.
//   - x3: the incoming new target or generator object
//   - cp: our context.
//   - fp: our caller's frame pointer.
//   - lr: return address.
//
// The function builds an interpreter frame. See InterpreterFrameConstants in
// frame-constants.h for its layout.
void Builtins::Generate_InterpreterEntryTrampoline(MacroAssembler* masm) {
#if defined(__CHERI_PURE_CAPABILITY__)
  Register closure = c1;
  Register feedback_vector = c2;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register closure = x1;
  Register feedback_vector = x2;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Get the bytecode array from the function object and load it into
  // kInterpreterBytecodeArrayRegister.
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c4, FieldMemOperand(closure, JSFunction::kSharedFunctionInfoOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      x4, FieldMemOperand(closure, JSFunction::kSharedFunctionInfoOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      kInterpreterBytecodeArrayRegister,
#if defined(__CHERI_PURE_CAPABILITY__)
      FieldMemOperand(c4, SharedFunctionInfo::kFunctionDataOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      FieldMemOperand(x4, SharedFunctionInfo::kFunctionDataOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  Label is_baseline;
  GetSharedFunctionInfoBytecodeOrBaseline(
#if defined(__CHERI_PURE_CAPABILITY__)
      masm, kInterpreterBytecodeArrayRegister, c11, &is_baseline);
#else
      masm, kInterpreterBytecodeArrayRegister, x11, &is_baseline);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // The bytecode array could have been flushed from the shared function info,
  // if so, call into CompileLazy.
  Label compile_lazy;
#if defined(__CHERI_PURE_CAPABILITY__)
  __ CompareObjectType(kInterpreterBytecodeArrayRegister, c4, x4,
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ CompareObjectType(kInterpreterBytecodeArrayRegister, x4, x4,
#endif // defined(__CHERI_PURE_CAPABILITY__)
                       BYTECODE_ARRAY_TYPE);
  __ B(ne, &compile_lazy);

  // Load the feedback vector from the closure.
  __ LoadTaggedPointerField(
      feedback_vector,
      FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedPointerField(
      feedback_vector, FieldMemOperand(feedback_vector, Cell::kValueOffset));

  Label push_stack_frame;
  // Check if feedback vector is valid. If valid, check for optimized code
  // and update invocation count. Otherwise, setup the stack frame.
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c7, FieldMemOperand(feedback_vector, HeapObject::kMapOffset));
  __ Ldrh(x7, FieldMemOperand(c7, Map::kInstanceTypeOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      x7, FieldMemOperand(feedback_vector, HeapObject::kMapOffset));
  __ Ldrh(x7, FieldMemOperand(x7, Map::kInstanceTypeOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Cmp(x7, FEEDBACK_VECTOR_TYPE);
  __ B(ne, &push_stack_frame);

  // Check the tiering state.
  Label has_optimized_code_or_state;
  Register optimization_state = w7;
  LoadTieringStateAndJumpIfNeedsProcessing(
      masm, optimization_state, feedback_vector, &has_optimized_code_or_state);

  {
    UseScratchRegisterScope temps(masm);
    ResetFeedbackVectorOsrUrgency(masm, feedback_vector, temps.AcquireW());
  }

  Label not_optimized;
  __ bind(&not_optimized);

  // Increment invocation count for the function.
  __ Ldr(w10, FieldMemOperand(feedback_vector,
                              FeedbackVector::kInvocationCountOffset));
  __ Add(w10, w10, Operand(1));
  __ Str(w10, FieldMemOperand(feedback_vector,
                              FeedbackVector::kInvocationCountOffset));

  // Open a frame scope to indicate that there is a frame on the stack.  The
  // MANUAL indicates that the scope shouldn't actually generate code to set up
  // the frame (that is done below).
  __ Bind(&push_stack_frame);
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ Push<TurboAssembler::kSignLR>(lr, fp);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(fp, csp);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ mov(fp, sp);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(cp, closure);

  ResetBytecodeAge(masm, kInterpreterBytecodeArrayRegister);

  // Load the initial bytecode offset.
  __ Mov(kInterpreterBytecodeOffsetRegister,
         Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));

  // Push actual argument count, bytecode array, Smi tagged bytecode array
  // offset and an undefined (to properly align the stack pointer).
  static_assert(TurboAssembler::kExtraSlotClaimedByPrologue == 1);
  __ SmiTag(x6, kInterpreterBytecodeOffsetRegister);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(kJavaScriptCallArgCountRegister.C(), kInterpreterBytecodeArrayRegister);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(kJavaScriptCallArgCountRegister, kInterpreterBytecodeArrayRegister);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(c6, kInterpreterAccumulatorRegister);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(x6, kInterpreterAccumulatorRegister);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Allocate the local and temporary register file on the stack.
  Label stack_overflow;
  {
    // Load frame size from the BytecodeArray object.
    __ Ldr(w11, FieldMemOperand(kInterpreterBytecodeArrayRegister,
                                BytecodeArray::kFrameSizeOffset));

    // Do a stack check to ensure we don't go over the limit.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Sub(c10, csp, Operand(x11));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Sub(x10, sp, Operand(x11));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    {
      UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
      Register scratch = temps.AcquireC();
#else // defined(__CHERI_PURE_CAPABILITY__)
      Register scratch = temps.AcquireX();
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ LoadStackLimit(scratch, StackLimitKind::kRealStackLimit);
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Cmp(c10, scratch);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Cmp(x10, scratch);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    }
    __ B(lo, &stack_overflow);

    // If ok, push undefined as the initial value for all register file entries.
    // Note: there should always be at least one stack slot for the return
    // register in the register file.
    Label loop_header;
    __ Lsr(x11, x11, kSystemPointerSizeLog2);
    // Round down (since we already have an undefined in the stack) the number
    // of registers to a multiple of 2, to align the stack to 16 bytes.
    __ Bic(x11, x11, 1);
    __ PushMultipleTimes(kInterpreterAccumulatorRegister, x11);
    __ Bind(&loop_header);
  }

  // If the bytecode array has a valid incoming new target or generator object
  // register, initialize it with incoming value which was passed in x3.
  Label no_incoming_new_target_or_generator_register;
  __ Ldrsw(x10,
           FieldMemOperand(
               kInterpreterBytecodeArrayRegister,
               BytecodeArray::kIncomingNewTargetOrGeneratorRegisterOffset));
  __ Cbz(x10, &no_incoming_new_target_or_generator_register);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(c3, MemOperand(fp, x10, LSL, kSystemPointerSizeLog2));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(x3, MemOperand(fp, x10, LSL, kSystemPointerSizeLog2));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Bind(&no_incoming_new_target_or_generator_register);

  // Perform interrupt stack check.
  // TODO(solanes): Merge with the real stack limit check above.
  Label stack_check_interrupt, after_stack_check_interrupt;
#if defined(__CHERI_PURE_CAPABILITY__)
  __ LoadStackLimit(c10, StackLimitKind::kInterruptStackLimit);
  __ Cmp(csp, c10);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadStackLimit(x10, StackLimitKind::kInterruptStackLimit);
  __ Cmp(sp, x10);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(lo, &stack_check_interrupt);
  __ Bind(&after_stack_check_interrupt);

  // The accumulator is already loaded with undefined.

  // Load the dispatch table into a register and dispatch to the bytecode
  // handler at the current bytecode offset.
  Label do_dispatch;
  __ bind(&do_dispatch);
  __ Mov(
      kInterpreterDispatchTableRegister,
      ExternalReference::interpreter_dispatch_table_address(masm->isolate()));
  __ Ldrb(x23, MemOperand(kInterpreterBytecodeArrayRegister,
                          kInterpreterBytecodeOffsetRegister));
  __ Mov(x1, Operand(x23, LSL, kSystemPointerSizeLog2));
  __ Ldr(kJavaScriptCallCodeStartRegister,
         MemOperand(kInterpreterDispatchTableRegister, x1));
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Orr(kJavaScriptCallCodeStartRegister, kJavaScriptCallCodeStartRegister, 0x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Call(kJavaScriptCallCodeStartRegister);

  // Any returns to the entry trampoline are either due to the return bytecode
  // or the interpreter tail calling a builtin and then a dispatch.
  masm->isolate()->heap()->SetInterpreterEntryReturnPCOffset(masm->pc_offset());
  __ JumpTarget();

  // Get bytecode array and bytecode offset from the stack frame.
  __ Ldr(kInterpreterBytecodeArrayRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  // Either return, or advance to the next bytecode and dispatch.
  Label do_return;
  __ Ldrb(x1, MemOperand(kInterpreterBytecodeArrayRegister,
                         kInterpreterBytecodeOffsetRegister));
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, x1, x2, x3,
                                &do_return);
  __ B(&do_dispatch);

  __ bind(&do_return);
  // The return value is in x0.
  LeaveInterpreterFrame(masm, x2, x4);
  __ Ret();

  __ bind(&stack_check_interrupt);
  // Modify the bytecode offset in the stack to be kFunctionEntryBytecodeOffset
  // for the call to the StackGuard.
  __ Mov(kInterpreterBytecodeOffsetRegister,
         Operand(Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag +
                              kFunctionEntryBytecodeOffset)));
  __ Str(kInterpreterBytecodeOffsetRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ CallRuntime(Runtime::kStackGuard);

  // After the call, restore the bytecode array, bytecode offset and accumulator
  // registers again. Also, restore the bytecode offset in the stack to its
  // previous value.
  __ Ldr(kInterpreterBytecodeArrayRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ Mov(kInterpreterBytecodeOffsetRegister,
         Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);

  __ SmiTag(x10, kInterpreterBytecodeOffsetRegister);
  __ Str(x10, MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  __ jmp(&after_stack_check_interrupt);

  __ bind(&has_optimized_code_or_state);
  MaybeOptimizeCodeOrTailCallOptimizedCodeSlot(masm, optimization_state,
                                               feedback_vector);

  __ bind(&is_baseline);
  {
    // Load the feedback vector from the closure.
    __ LoadTaggedPointerField(
        feedback_vector,
        FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
    __ LoadTaggedPointerField(
        feedback_vector, FieldMemOperand(feedback_vector, Cell::kValueOffset));

    Label install_baseline_code;
    // Check if feedback vector is valid. If not, call prepare for baseline to
    // allocate it.
    __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
        c7, FieldMemOperand(feedback_vector, HeapObject::kMapOffset));
    __ Ldrh(x7, FieldMemOperand(c7, Map::kInstanceTypeOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
        x7, FieldMemOperand(feedback_vector, HeapObject::kMapOffset));
    __ Ldrh(x7, FieldMemOperand(x7, Map::kInstanceTypeOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Cmp(x7, FEEDBACK_VECTOR_TYPE);
    __ B(ne, &install_baseline_code);

    // Check the tiering state.
    LoadTieringStateAndJumpIfNeedsProcessing(masm, optimization_state,
                                             feedback_vector,
                                             &has_optimized_code_or_state);

    // Load the baseline code into the closure.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Move(c2, kInterpreterBytecodeArrayRegister);
    static_assert(kJavaScriptCallCodeStartRegister == c2, "ABI mismatch");
    ReplaceClosureCodeWithOptimizedCode(masm, c2, closure);
    __ JumpCodeTObject(c2);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Move(x2, kInterpreterBytecodeArrayRegister);
    static_assert(kJavaScriptCallCodeStartRegister == x2, "ABI mismatch");
    ReplaceClosureCodeWithOptimizedCode(masm, x2, closure);
    __ JumpCodeTObject(x2);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    __ bind(&install_baseline_code);
    GenerateTailCallToReturnedCode(masm, Runtime::kInstallBaselineCode);
  }

  __ bind(&compile_lazy);
  GenerateTailCallToReturnedCode(masm, Runtime::kCompileLazy);
  __ Unreachable();  // Should not return.

  __ bind(&stack_overflow);
  __ CallRuntime(Runtime::kThrowStackOverflow);
  __ Unreachable();  // Should not return.
}

static void GenerateInterpreterPushArgs(MacroAssembler* masm, Register num_args,
                                        Register first_arg_index,
                                        Register spread_arg_out,
                                        ConvertReceiverMode receiver_mode,
                                        InterpreterPushArgsMode mode) {
  ASM_CODE_COMMENT(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
  Register last_arg_addr = c10;
  Register stack_addr = c11;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register last_arg_addr = x10;
  Register stack_addr = x11;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register slots_to_claim = x12;
  Register slots_to_copy = x13;

  DCHECK(!AreAliased(num_args, first_arg_index, last_arg_addr, stack_addr,
                     slots_to_claim, slots_to_copy));
  // spread_arg_out may alias with the first_arg_index input.
  DCHECK(!AreAliased(spread_arg_out, last_arg_addr, stack_addr, slots_to_claim,
                     slots_to_copy));

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Exclude final spread from slots to claim and the number of arguments.
    __ Sub(num_args, num_args, 1);
  }

  // Round up to an even number of slots.
  __ Add(slots_to_claim, num_args, 1);
  __ Bic(slots_to_claim, slots_to_claim, 1);

  // Add a stack check before pushing arguments.
  Label stack_overflow, done;
  __ StackOverflowCheck(slots_to_claim, &stack_overflow);
  __ B(&done);
  __ Bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
  __ Unreachable();
  __ Bind(&done);

  __ Claim(slots_to_claim);

  {
    // Store padding, which may be overwritten.
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Sub(scratch, slots_to_claim, 1);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Poke(padregc, Operand(scratch, LSL, kSystemPointerSizeLog2));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Poke(padreg, Operand(scratch, LSL, kSystemPointerSizeLog2));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  const bool skip_receiver =
      receiver_mode == ConvertReceiverMode::kNullOrUndefined;
  if (skip_receiver) {
    __ Sub(slots_to_copy, num_args, kJSArgcReceiverSlots);
  } else {
    __ Mov(slots_to_copy, num_args);
  }
  __ SlotAddress(stack_addr, skip_receiver ? 1 : 0);
  __ Sub(last_arg_addr, first_arg_index,
         Operand(slots_to_copy, LSL, kSystemPointerSizeLog2));
  __ Add(last_arg_addr, last_arg_addr, kSystemPointerSize);

  // Load the final spread argument into spread_arg_out, if necessary.
  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ Ldr(spread_arg_out, MemOperand(last_arg_addr, -kSystemPointerSize));
  }

#if defined(__CHERI_PURE_CAPABILITY__)
  __ CopyCapabilities(stack_addr, last_arg_addr, slots_to_copy,
                     TurboAssembler::kCapDstLessThanSrcAndReverse);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ CopyDoubleWords(stack_addr, last_arg_addr, slots_to_copy,
                     TurboAssembler::kDstLessThanSrcAndReverse);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    // Store "undefined" as the receiver arg if we need to.
#if defined(__CHERI_PURE_CAPABILITY__)
    Register receiver = c14;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register receiver = x14;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadRoot(receiver, RootIndex::kUndefinedValue);
    __ Poke(receiver, 0);
  }
}

// static
void Builtins::Generate_InterpreterPushArgsThenCallImpl(
    MacroAssembler* masm, ConvertReceiverMode receiver_mode,
    InterpreterPushArgsMode mode) {
  DCHECK(mode != InterpreterPushArgsMode::kArrayFunction);
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x2 : the address of the first argument to be pushed. Subsequent
  //          arguments should be consecutive above this, in the same order as
  //          they are to be pushed onto the stack.
  //  -- x1 : the target to call (can be any Object).
  // -----------------------------------

  // Push the arguments. num_args may be updated according to mode.
  // spread_arg_out will be updated to contain the last spread argument, when
  // mode == InterpreterPushArgsMode::kWithFinalSpread.
  Register num_args = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register first_arg_index = c2;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register first_arg_index = x2;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register spread_arg_out =
      (mode == InterpreterPushArgsMode::kWithFinalSpread) ? x2 : no_reg;
  GenerateInterpreterPushArgs(masm, num_args, first_arg_index, spread_arg_out,
                              receiver_mode, mode);

  // Call the target.
  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ Jump(BUILTIN_CODE(masm->isolate(), CallWithSpread),
            RelocInfo::CODE_TARGET);
  } else {
    __ Jump(masm->isolate()->builtins()->Call(ConvertReceiverMode::kAny),
            RelocInfo::CODE_TARGET);
  }
}

// static
void Builtins::Generate_InterpreterPushArgsThenConstructImpl(
    MacroAssembler* masm, InterpreterPushArgsMode mode) {
  // ----------- S t a t e -------------
  // -- x0 : argument count
  // -- x3 : new target
  // -- x1 : constructor to call
  // -- x2 : allocation site feedback if available, undefined otherwise
  // -- x4 : address of the first argument
  // -----------------------------------
#if defined(__CHERI_PURE_CAPABILITY__)
  __ AssertUndefinedOrAllocationSite(c2);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ AssertUndefinedOrAllocationSite(x2);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Push the arguments. num_args may be updated according to mode.
  // spread_arg_out will be updated to contain the last spread argument, when
  // mode == InterpreterPushArgsMode::kWithFinalSpread.
  Register num_args = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register first_arg_index = c4;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register first_arg_index = x4;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register spread_arg_out =
      (mode == InterpreterPushArgsMode::kWithFinalSpread) ? x2 : no_reg;
  GenerateInterpreterPushArgs(masm, num_args, first_arg_index, spread_arg_out,
                              ConvertReceiverMode::kNullOrUndefined, mode);

  if (mode == InterpreterPushArgsMode::kArrayFunction) {
#if defined(__CHERI_PURE_CAPABILITY__)
    __ AssertFunction(c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ AssertFunction(x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)

    // Tail call to the array construct stub (still in the caller
    // context at this point).
    __ Jump(BUILTIN_CODE(masm->isolate(), ArrayConstructorImpl),
            RelocInfo::CODE_TARGET);
  } else if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Call the constructor with x0, x1, and x3 unmodified.
    __ Jump(BUILTIN_CODE(masm->isolate(), ConstructWithSpread),
            RelocInfo::CODE_TARGET);
  } else {
    DCHECK_EQ(InterpreterPushArgsMode::kOther, mode);
    // Call the constructor with x0, x1, and x3 unmodified.
    __ Jump(BUILTIN_CODE(masm->isolate(), Construct), RelocInfo::CODE_TARGET);
  }
}

static void Generate_InterpreterEnterBytecode(MacroAssembler* masm) {
  // Initialize the dispatch table register.
  __ Mov(
      kInterpreterDispatchTableRegister,
      ExternalReference::interpreter_dispatch_table_address(masm->isolate()));

  // Get the bytecode array pointer from the frame.
  __ Ldr(kInterpreterBytecodeArrayRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));

  if (FLAG_debug_code) {
    // Check function data field is actually a BytecodeArray object.
    __ AssertNotSmi(
        kInterpreterBytecodeArrayRegister,
        AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CompareObjectType(kInterpreterBytecodeArrayRegister, c1, x1,
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CompareObjectType(kInterpreterBytecodeArrayRegister, x1, x1,
#endif // defined(__CHERI_PURE_CAPABILITY__)
                         BYTECODE_ARRAY_TYPE);
    __ Assert(
        eq, AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry);
  }

  // Get the target bytecode offset from the frame.
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  if (FLAG_debug_code) {
    Label okay;
    __ cmp(kInterpreterBytecodeOffsetRegister,
           Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
    __ B(ge, &okay);
    __ Unreachable();
    __ bind(&okay);
  }

  // Set up LR to point to code below, so we return there after we're done
  // executing the function.
  Label return_from_bytecode_dispatch;
  __ Adr(lr, &return_from_bytecode_dispatch);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Orr(lr, lr, 0x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Dispatch to the target bytecode.
  __ Ldrb(x23, MemOperand(kInterpreterBytecodeArrayRegister,
                          kInterpreterBytecodeOffsetRegister));
  __ Mov(x1, Operand(x23, LSL, kSystemPointerSizeLog2));
  __ Ldr(kJavaScriptCallCodeStartRegister,
         MemOperand(kInterpreterDispatchTableRegister, x1));

  {
    UseScratchRegisterScope temps(masm);
    temps.Exclude(x17);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c17, kJavaScriptCallCodeStartRegister);
    __ Jump(c17);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x17, kJavaScriptCallCodeStartRegister);
    __ Jump(x17);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  __ Bind(&return_from_bytecode_dispatch);

  // We return here after having executed the function in the interpreter.
  // Now jump to the correct point in the interpreter entry trampoline.
  Label builtin_trampoline, trampoline_loaded;
  Smi interpreter_entry_return_pc_offset(
      masm->isolate()->heap()->interpreter_entry_return_pc_offset());
  DCHECK_NE(interpreter_entry_return_pc_offset, Smi::zero());

  // If the SFI function_data is an InterpreterData, the function will have a
  // custom copy of the interpreter entry trampoline for profiling. If so,
  // get the custom trampoline, otherwise grab the entry address of the global
  // trampoline.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c1, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ LoadTaggedPointerField(
      c1, FieldMemOperand(c1, JSFunction::kSharedFunctionInfoOffset));
  __ LoadTaggedPointerField(
      c1, FieldMemOperand(c1, SharedFunctionInfo::kFunctionDataOffset));
  __ CompareObjectType(c1, kInterpreterDispatchTableRegister,
                       kInterpreterDispatchTableRegister.X(),
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x1, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ LoadTaggedPointerField(
      x1, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));
  __ LoadTaggedPointerField(
      x1, FieldMemOperand(x1, SharedFunctionInfo::kFunctionDataOffset));
  __ CompareObjectType(x1, kInterpreterDispatchTableRegister,
                       kInterpreterDispatchTableRegister,
#endif // defined(__CHERI_PURE_CAPABILITY__)
                       INTERPRETER_DATA_TYPE);
  __ B(ne, &builtin_trampoline);

  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c1, FieldMemOperand(c1, InterpreterData::kInterpreterTrampolineOffset));
  __ LoadCodeTEntry(c1, c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
      x1, FieldMemOperand(x1, InterpreterData::kInterpreterTrampolineOffset));
  __ LoadCodeTEntry(x1, x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(&trampoline_loaded);

  __ Bind(&builtin_trampoline);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c1, ExternalReference::
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x1, ExternalReference::
#endif // defined(__CHERI_PURE_CAPABILITY__)
                 address_of_interpreter_entry_trampoline_instruction_start(
                     masm->isolate()));
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c1, MemOperand(c1));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x1, MemOperand(x1));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ Bind(&trampoline_loaded);

  {
    UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
    temps.Exclude(c17);
    __ Add(c17, c1, Operand(interpreter_entry_return_pc_offset.value()));
    __ Orr(c17, c17, 0x1);
    __ Br(c17);
#else // defined(__CHERI_PURE_CAPABILITY__)
    temps.Exclude(x17);
    __ Add(x17, x1, Operand(interpreter_entry_return_pc_offset.value()));
    __ Br(x17);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
}

void Builtins::Generate_InterpreterEnterAtNextBytecode(MacroAssembler* masm) {
  // Get bytecode array and bytecode offset from the stack frame.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(kInterpreterBytecodeArrayRegister,
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ ldr(kInterpreterBytecodeArrayRegister,
#endif // defined(__CHERI_PURE_CAPABILITY__)
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  Label enter_bytecode, function_entry_bytecode;
  __ cmp(kInterpreterBytecodeOffsetRegister,
         Operand(BytecodeArray::kHeaderSize - kHeapObjectTag +
                 kFunctionEntryBytecodeOffset));
  __ B(eq, &function_entry_bytecode);

  // Load the current bytecode.
  __ Ldrb(x1, MemOperand(kInterpreterBytecodeArrayRegister,
                         kInterpreterBytecodeOffsetRegister));

  // Advance to the next bytecode.
  Label if_return;
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, x1, x2, x3,
                                &if_return);
  __ bind(&enter_bytecode);
  // Convert new bytecode offset to a Smi and save in the stackframe.
  __ SmiTag(x2, kInterpreterBytecodeOffsetRegister);
  __ Str(x2, MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  Generate_InterpreterEnterBytecode(masm);

  __ bind(&function_entry_bytecode);
  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset. Detect this case and advance to the first
  // actual bytecode.
  __ Mov(kInterpreterBytecodeOffsetRegister,
         Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
  __ B(&enter_bytecode);

  // We should never take the if_return path.
  __ bind(&if_return);
  __ Abort(AbortReason::kInvalidBytecodeAdvance);
}

void Builtins::Generate_InterpreterEnterAtBytecode(MacroAssembler* masm) {
  Generate_InterpreterEnterBytecode(masm);
}

namespace {
void Generate_ContinueToBuiltinHelper(MacroAssembler* masm,
                                      bool java_script_builtin,
                                      bool with_result) {
  const RegisterConfiguration* config(RegisterConfiguration::Default());
  int allocatable_register_count = config->num_allocatable_general_registers();
  int frame_size = BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp +
                   (allocatable_register_count +
                    BuiltinContinuationFrameConstants::PaddingSlotCount(
                        allocatable_register_count)) *
                       kSystemPointerSize;

  UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
  Register scratch = temps.AcquireC();  // Temp register is not allocatable.
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register scratch = temps.AcquireX();  // Temp register is not allocatable.
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Set up frame pointer.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Add(fp, csp, frame_size);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Add(fp, sp, frame_size);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  if (with_result) {
    if (java_script_builtin) {
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Mov(scratch, c0);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ mov(scratch, x0);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    } else {
      // Overwrite the hole inserted by the deoptimizer with the return value
      // from the LAZY deopt point.
      __ Str(x0, MemOperand(
                     fp, BuiltinContinuationFrameConstants::kCallerSPOffset));
    }
  }

  // Restore registers in pairs.
  int offset = -BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp -
               allocatable_register_count * kSystemPointerSize;
  for (int i = allocatable_register_count - 1; i > 0; i -= 2) {
    int code1 = config->GetAllocatableGeneralCode(i);
    int code2 = config->GetAllocatableGeneralCode(i - 1);
    Register reg1 = Register::from_code(code1);
    Register reg2 = Register::from_code(code2);
    __ Ldp(reg1, reg2, MemOperand(fp, offset));
    offset += 2 * kSystemPointerSize;
  }

  // Restore first register separately, if number of registers is odd.
  if (allocatable_register_count % 2 != 0) {
    int code = config->GetAllocatableGeneralCode(0);
    __ Ldr(Register::from_code(code), MemOperand(fp, offset));
  }

  if (java_script_builtin) __ SmiUntag(kJavaScriptCallArgCountRegister);

  if (java_script_builtin && with_result) {
    // Overwrite the hole inserted by the deoptimizer with the return value from
    // the LAZY deopt point. x0 contains the arguments count, the return value
    // from LAZY is always the last argument.
    constexpr int return_offset =
        BuiltinContinuationFrameConstants::kCallerSPOffset /
            kSystemPointerSize -
        kJSArgcReceiverSlots;
    __ add(x0, x0, return_offset);
    __ Str(scratch, MemOperand(fp, x0, LSL, kSystemPointerSizeLog2));
    // Recover argument count.
    __ sub(x0, x0, return_offset);
  }

  // Load builtin index (stored as a Smi) and use it to get the builtin start
  // address from the builtins table.
  Register builtin = scratch;
  __ Ldr(
      builtin,
      MemOperand(fp, BuiltinContinuationFrameConstants::kBuiltinIndexOffset));

  // Restore fp, lr.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(csp, fp);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(sp, fp);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Pop<TurboAssembler::kAuthLR>(fp, lr);

  __ LoadEntryFromBuiltinIndex(builtin);
  __ Jump(builtin);
}
}  // namespace

void Builtins::Generate_ContinueToCodeStubBuiltin(MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, false, false);
}

void Builtins::Generate_ContinueToCodeStubBuiltinWithResult(
    MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, false, true);
}

void Builtins::Generate_ContinueToJavaScriptBuiltin(MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, true, false);
}

void Builtins::Generate_ContinueToJavaScriptBuiltinWithResult(
    MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, true, true);
}

void Builtins::Generate_NotifyDeoptimized(MacroAssembler* masm) {
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kNotifyDeoptimized);
  }

  // Pop TOS register and padding.
#if defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(kInterpreterAccumulatorRegister.code(), c0.code());
  __ Pop(c0, padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(kInterpreterAccumulatorRegister.code(), x0.code());
  __ Pop(x0, padreg);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Ret();
}

namespace {

void Generate_OSREntry(MacroAssembler* masm, Register entry_address,
                       Operand offset = Operand(0)) {
  // Pop the return address to this function's caller from the return stack
  // buffer, since we'll never return to it.
  Label jump;
  __ Adr(lr, &jump);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Orr(lr, lr, 0x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Ret();

  __ Bind(&jump);

  UseScratchRegisterScope temps(masm);
  temps.Exclude(x17);
  if (offset.IsZero()) {
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c17, entry_address);
  } else {
    __ Add(c17, entry_address, offset);
  }
  __ Orr(c17, c17, 0x1);
  __ Br(c17);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x17, entry_address);
  } else {
    __ Add(x17, entry_address, offset);
  }
  __ Br(x17);
#endif // defined(__CHERI_PURE_CAPABILITY__)
}

enum class OsrSourceTier {
  kInterpreter,
  kBaseline,
};

void OnStackReplacement(MacroAssembler* masm, OsrSourceTier source,
                        Register maybe_target_code) {
  Label jump_to_optimized_code;
  {
    // If maybe_target_code is not null, no need to call into runtime. A
    // precondition here is: if maybe_target_code is a Code object, it must NOT
    // be marked_for_deoptimization (callers must ensure this).
    __ CompareTaggedAndBranch(x0, Smi::zero(), ne, &jump_to_optimized_code);
  }

  ASM_CODE_COMMENT(masm);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kCompileOptimizedOSR);
  }

  // If the code object is null, just return to the caller.
  __ CompareTaggedAndBranch(x0, Smi::zero(), ne, &jump_to_optimized_code);
  __ Ret();

  __ Bind(&jump_to_optimized_code);
#if defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(maybe_target_code, c0);  // Already in the right spot.
#else // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(maybe_target_code, x0);  // Already in the right spot.
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // OSR entry tracing.
  {
    Label next;
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c1, ExternalReference::address_of_FLAG_trace_osr());
    __ Ldrsb(x1, MemOperand(c1));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x1, ExternalReference::address_of_FLAG_trace_osr());
    __ Ldrsb(x1, MemOperand(x1));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Tst(x1, 0xFF);  // Mask to the LSB.
    __ B(eq, &next);

    {
      FrameScope scope(masm, StackFrame::INTERNAL);
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Push(c0, padregc);  // Preserve the code object.
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Push(x0, padreg);  // Preserve the code object.
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ CallRuntime(Runtime::kTraceOptimizedOSREntry, 0);
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Pop(c0, padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Pop(padreg, x0);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    }

    __ Bind(&next);
  }

  if (source == OsrSourceTier::kInterpreter) {
    // Drop the handler frame that is be sitting on top of the actual
    // JavaScript frame. This is the case then OSR is triggered from bytecode.
    __ LeaveFrame(StackFrame::STUB);
  }

  if (V8_EXTERNAL_CODE_SPACE_BOOL) {
#if defined(__CHERI_PURE_CAPABILITY__)
    __ LoadCodeDataContainerCodeNonBuiltin(c0, c0);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ LoadCodeDataContainerCodeNonBuiltin(x0, x0);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  // Load deoptimization data from the code object.
  // <deopt_data> = <code>[#deoptimization_data_offset]
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c1,
      FieldMemOperand(c0, Code::kDeoptimizationDataOrInterpreterDataOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      x1,
      FieldMemOperand(x0, Code::kDeoptimizationDataOrInterpreterDataOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Load the OSR entrypoint offset from the deoptimization data.
  // <osr_offset> = <deopt_data>[#header_size + #osr_pc_offset]
  __ SmiUntagField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c1, FieldMemOperand(x1, FixedArray::OffsetOfElementAt(
#else // defined(__CHERI_PURE_CAPABILITY__)
      x1, FieldMemOperand(x1, FixedArray::OffsetOfElementAt(
#endif // defined(__CHERI_PURE_CAPABILITY__)
                                  DeoptimizationData::kOsrPcOffsetIndex)));

  // Compute the target address = code_obj + header_size + osr_offset
  // <entry_addr> = <code_obj> + #header_size + <osr_offset>
#if defined(__CHERI_PURE_CAPABILITY__)
  {
    // TODO(gcjenkinson): Support addition of register values
    // to capability registers in the macro assembler.
    UseScratchRegisterScope temps(masm);
    Register temp = temps.AcquireX();
    __ Gcvalue(c0, temp);
    __ Add(temp, temp, x1);
    __ Scvalue(c0, c0, temp);
  }
  Generate_OSREntry(masm, c0, Code::kHeaderSize - kHeapObjectTag);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Add(x0, x0, x1);
  Generate_OSREntry(masm, x0, Code::kHeaderSize - kHeapObjectTag);
#endif // defined(__CHERI_PURE_CAPABILITY__)
}

}  // namespace

void Builtins::Generate_InterpreterOnStackReplacement(MacroAssembler* masm) {
  using D = InterpreterOnStackReplacementDescriptor;
  static_assert(D::kParameterCount == 1);
  OnStackReplacement(masm, OsrSourceTier::kInterpreter,
                     D::MaybeTargetCodeRegister());
}

void Builtins::Generate_BaselineOnStackReplacement(MacroAssembler* masm) {
  using D = BaselineOnStackReplacementDescriptor;
  static_assert(D::kParameterCount == 1);

  __ ldr(kContextRegister,
         MemOperand(fp, BaselineFrameConstants::kContextOffset));
  OnStackReplacement(masm, OsrSourceTier::kBaseline,
                     D::MaybeTargetCodeRegister());
}

// static
void Builtins::Generate_FunctionPrototypeApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0       : argc
  //  -- sp[0]    : receiver
  //  -- sp[8]    : thisArg  (if argc >= 1)
  //  -- sp[16]   : argArray (if argc == 2)
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_FunctionPrototypeApply");

  Register argc = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register receiver = c1;
  Register arg_array = c2;
  Register this_arg = c3;
  Register undefined_value = c4;
  Register null_value = c5;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register receiver = x1;
  Register arg_array = x2;
  Register this_arg = x3;
  Register undefined_value = x4;
  Register null_value = x5;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);
  __ LoadRoot(null_value, RootIndex::kNullValue);

  // 1. Load receiver into x1, argArray into x2 (if present), remove all
  // arguments from the stack (including the receiver), and push thisArg (if
  // present) instead.
  {
    Label done;
    __ Mov(this_arg, undefined_value);
    __ Mov(arg_array, undefined_value);
    __ Peek(receiver, 0);
    __ Cmp(argc, Immediate(JSParameterCount(1)));
    __ B(lt, &done);
    __ Peek(this_arg, kSystemPointerSize);
    __ B(eq, &done);
    __ Peek(arg_array, 2 * kSystemPointerSize);
    __ bind(&done);
  }
  __ DropArguments(argc, TurboAssembler::kCountIncludesReceiver);
  __ PushArgument(this_arg);

  // ----------- S t a t e -------------
  //  -- x2      : argArray
  //  -- x1      : receiver
  //  -- sp[0]   : thisArg
  // -----------------------------------

  // 2. We don't need to check explicitly for callable receiver here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Tail call with no arguments if argArray is null or undefined.
  Label no_arguments;
  __ CmpTagged(arg_array, null_value);
  __ CcmpTagged(arg_array, undefined_value, ZFlag, ne);
  __ B(eq, &no_arguments);

  // 4a. Apply the receiver to the given argArray.
  __ Jump(BUILTIN_CODE(masm->isolate(), CallWithArrayLike),
          RelocInfo::CODE_TARGET);

  // 4b. The argArray is either null or undefined, so we tail call without any
  // arguments to the receiver.
  __ Bind(&no_arguments);
  {
    __ Mov(x0, JSParameterCount(0));
#if defined(__CHERI_PURE_CAPABILITY__)
    DCHECK_EQ(receiver, c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
    DCHECK_EQ(receiver, x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Jump(masm->isolate()->builtins()->Call(), RelocInfo::CODE_TARGET);
  }
}

// static
void Builtins::Generate_FunctionPrototypeCall(MacroAssembler* masm) {
  Register argc = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register function = c1;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register function = x1;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  ASM_LOCATION("Builtins::Generate_FunctionPrototypeCall");

  // 1. Get the callable to call (passed as receiver) from the stack.
  __ Peek(function, __ ReceiverOperand(argc));

  // 2. Handle case with no arguments.
  {
    Label non_zero;
#if defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = c10;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = x10;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Cmp(argc, JSParameterCount(0));
    __ B(gt, &non_zero);
    __ LoadRoot(scratch, RootIndex::kUndefinedValue);
    // Overwrite receiver with undefined, which will be the new receiver.
    // We do not need to overwrite the padding slot above it with anything.
    __ Poke(scratch, 0);
    // Call function. The argument count is already zero.
    __ Jump(masm->isolate()->builtins()->Call(), RelocInfo::CODE_TARGET);
    __ Bind(&non_zero);
  }

  Label arguments_ready;
  // 3. Shift arguments. It depends if the arguments is even or odd.
  // That is if padding exists or not.
  {
    Label even;
#if defined(__CHERI_PURE_CAPABILITY__)
    Register copy_from = c10;
    Register copy_to = c11;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register copy_from = x10;
    Register copy_to = x11;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    Register count = x12;
    UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
    temps.Include(c14, c15);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    Register argc_without_receiver = temps.AcquireX();
    __ Sub(argc_without_receiver, argc, kJSArgcReceiverSlots);

    // CopyDoubleWords changes the count argument.
    __ Mov(count, argc_without_receiver);
    __ Tbz(argc_without_receiver, 0, &even);

    // Shift arguments one slot down on the stack (overwriting the original
    // receiver).
    __ SlotAddress(copy_from, 1);
    __ Sub(copy_to, copy_from, kSystemPointerSize);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CopyCapabilities(copy_to, copy_from, count);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CopyDoubleWords(copy_to, copy_from, count);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    // Overwrite the duplicated remaining last argument.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Poke(padregc, Operand(argc_without_receiver, LSL, kCRegSizeLog2));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Poke(padreg, Operand(argc_without_receiver, LSL, kXRegSizeLog2));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ B(&arguments_ready);

    // Copy arguments one slot higher in memory, overwriting the original
    // receiver and padding.
    __ Bind(&even);
    __ SlotAddress(copy_from, count);
    __ Add(copy_to, copy_from, kSystemPointerSize);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CopyCapabilities(copy_to, copy_from, count,
                       TurboAssembler::kCapSrcLessThanDst);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CopyDoubleWords(copy_to, copy_from, count,
                       TurboAssembler::kSrcLessThanDst);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Drop(2);
  }

  // 5. Adjust argument count to make the original first argument the new
  //    receiver and call the callable.
  __ Bind(&arguments_ready);
  __ Sub(argc, argc, 1);
  __ Jump(masm->isolate()->builtins()->Call(), RelocInfo::CODE_TARGET);
}

void Builtins::Generate_ReflectApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0     : argc
  //  -- sp[0]  : receiver
  //  -- sp[8]  : target         (if argc >= 1)
  //  -- sp[16] : thisArgument   (if argc >= 2)
  //  -- sp[24] : argumentsList  (if argc == 3)
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_ReflectApply");

  Register argc = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register arguments_list = c2;
  Register target = c1;
  Register this_argument = c4;
  Register undefined_value = c3;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register arguments_list = x2;
  Register target = x1;
  Register this_argument = x4;
  Register undefined_value = x3;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);

  // 1. Load target into x1 (if present), argumentsList into x2 (if present),
  // remove all arguments from the stack (including the receiver), and push
  // thisArgument (if present) instead.
  {
    Label done;
    __ Mov(target, undefined_value);
    __ Mov(this_argument, undefined_value);
    __ Mov(arguments_list, undefined_value);
    __ Cmp(argc, Immediate(JSParameterCount(1)));
    __ B(lt, &done);
    __ Peek(target, kSystemPointerSize);
    __ B(eq, &done);
    __ Peek(this_argument, 2 * kSystemPointerSize);
    __ Cmp(argc, Immediate(JSParameterCount(3)));
    __ B(lt, &done);
    __ Peek(arguments_list, 3 * kSystemPointerSize);
    __ bind(&done);
  }
  __ DropArguments(argc, TurboAssembler::kCountIncludesReceiver);
  __ PushArgument(this_argument);

  // ----------- S t a t e -------------
  //  -- x2      : argumentsList
  //  -- x1      : target
  //  -- sp[0]   : thisArgument
  // -----------------------------------

  // 2. We don't need to check explicitly for callable target here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Apply the target to the given argumentsList.
  __ Jump(BUILTIN_CODE(masm->isolate(), CallWithArrayLike),
          RelocInfo::CODE_TARGET);
}

void Builtins::Generate_ReflectConstruct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0       : argc
  //  -- sp[0]   : receiver
  //  -- sp[8]   : target
  //  -- sp[16]  : argumentsList
  //  -- sp[24]  : new.target (optional)
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_ReflectConstruct");

  Register argc = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register arguments_list = c2;
  Register target = c1;
  Register new_target = c3;
  Register undefined_value = c4;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register arguments_list = x2;
  Register target = x1;
  Register new_target = x3;
  Register undefined_value = x4;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);

  // 1. Load target into x1 (if present), argumentsList into x2 (if present),
  // new.target into x3 (if present, otherwise use target), remove all
  // arguments from the stack (including the receiver), and push thisArgument
  // (if present) instead.
  {
    Label done;
    __ Mov(target, undefined_value);
    __ Mov(arguments_list, undefined_value);
    __ Mov(new_target, undefined_value);
    __ Cmp(argc, Immediate(JSParameterCount(1)));
    __ B(lt, &done);
    __ Peek(target, kSystemPointerSize);
    __ B(eq, &done);
    __ Peek(arguments_list, 2 * kSystemPointerSize);
    __ Mov(new_target, target);  // new.target defaults to target
    __ Cmp(argc, Immediate(JSParameterCount(3)));
    __ B(lt, &done);
    __ Peek(new_target, 3 * kSystemPointerSize);
    __ bind(&done);
  }

  __ DropArguments(argc, TurboAssembler::kCountIncludesReceiver);

  // Push receiver (undefined).
  __ PushArgument(undefined_value);

  // ----------- S t a t e -------------
  //  -- x2      : argumentsList
  //  -- x1      : target
  //  -- x3      : new.target
  //  -- sp[0]   : receiver (undefined)
  // -----------------------------------

  // 2. We don't need to check explicitly for constructor target here,
  // since that's the first thing the Construct/ConstructWithArrayLike
  // builtins will do.

  // 3. We don't need to check explicitly for constructor new.target here,
  // since that's the second thing the Construct/ConstructWithArrayLike
  // builtins will do.

  // 4. Construct the target with the given new.target and argumentsList.
  __ Jump(BUILTIN_CODE(masm->isolate(), ConstructWithArrayLike),
          RelocInfo::CODE_TARGET);
}

namespace {

// Prepares the stack for copying the varargs. First we claim the necessary
// slots, taking care of potential padding. Then we copy the existing arguments
// one slot up or one slot down, as needed.
void Generate_PrepareForCopyingVarargs(MacroAssembler* masm, Register argc,
                                       Register len) {
  Label exit, even;
  Register slots_to_copy = x10;
  Register slots_to_claim = x12;

  __ Mov(slots_to_copy, argc);
  __ Mov(slots_to_claim, len);
  __ Tbz(slots_to_claim, 0, &even);

  // Claim space we need. If argc (without receiver) is even, slots_to_claim =
  // len + 1, as we need one extra padding slot. If argc (without receiver) is
  // odd, we know that the original arguments will have a padding slot we can
  // reuse (since len is odd), so slots_to_claim = len - 1.
  {
    Register scratch = x11;
    __ Add(slots_to_claim, len, 1);
    __ And(scratch, argc, 1);
    __ Sub(slots_to_claim, slots_to_claim, Operand(scratch, LSL, 1));
  }

  __ Bind(&even);
  __ Cbz(slots_to_claim, &exit);
  __ Claim(slots_to_claim);

  // Move the arguments already in the stack including the receiver.
  {
#if defined(__CHERI_PURE_CAPABILITY__)
    Register src = c11;
    Register dst = c12;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register src = x11;
    Register dst = x12;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ SlotAddress(src, slots_to_claim);
    __ SlotAddress(dst, 0);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CopyCapabilities(dst, src, slots_to_copy);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CopyDoubleWords(dst, src, slots_to_copy);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ Bind(&exit);
}

}  // namespace

// static
// TODO(v8:11615): Observe Code::kMaxArguments in CallOrConstructVarargs
void Builtins::Generate_CallOrConstructVarargs(MacroAssembler* masm,
                                               Handle<CodeT> code) {
  // ----------- S t a t e -------------
  //  -- x1 : target
  //  -- x0 : number of parameters on the stack
  //  -- x2 : arguments list (a FixedArray)
  //  -- x4 : len (number of elements to push from args)
  //  -- x3 : new.target (for [[Construct]])
  // -----------------------------------
  if (FLAG_debug_code) {
    // Allow x2 to be a FixedArray, or a FixedDoubleArray if x4 == 0.
    Label ok, fail;
#if defined(__CHERI_PURE_CAPABILITY__)
    __ AssertNotSmi(c2, AbortReason::kOperandIsNotAFixedArray);
    __ LoadTaggedPointerField(c10, FieldMemOperand(c2, HeapObject::kMapOffset));
    __ Ldrh(x13, FieldMemOperand(c10, Map::kInstanceTypeOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ AssertNotSmi(x2, AbortReason::kOperandIsNotAFixedArray);
    __ LoadTaggedPointerField(x10, FieldMemOperand(x2, HeapObject::kMapOffset));
    __ Ldrh(x13, FieldMemOperand(x10, Map::kInstanceTypeOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Cmp(x13, FIXED_ARRAY_TYPE);
    __ B(eq, &ok);
    __ Cmp(x13, FIXED_DOUBLE_ARRAY_TYPE);
    __ B(ne, &fail);
    __ Cmp(x4, 0);
    __ B(eq, &ok);
    // Fall through.
    __ bind(&fail);
    __ Abort(AbortReason::kOperandIsNotAFixedArray);

    __ bind(&ok);
  }

#if defined(__CHERI_PURE_CAPABILITY__)
  Register arguments_list = c2;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register arguments_list = x2;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register argc = x0;
  Register len = x4;

  Label stack_overflow;
  __ StackOverflowCheck(len, &stack_overflow);

  // Skip argument setup if we don't need to push any varargs.
  Label done;
  __ Cbz(len, &done);

  Generate_PrepareForCopyingVarargs(masm, argc, len);

  // Push varargs.
  {
    Label loop;
#if defined(__CHERI_PURE_CAPABILITY__)
    Register src = c10;
    Register the_hole_value = c11;
    Register undefined_value = c12;
    Register scratch = c13;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register src = x10;
    Register the_hole_value = x11;
    Register undefined_value = x12;
    Register scratch = x13;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Add(src, arguments_list, FixedArray::kHeaderSize - kHeapObjectTag);
    __ LoadRoot(the_hole_value, RootIndex::kTheHoleValue);
    __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);
    // We do not use the CompareRoot macro as it would do a LoadRoot behind the
    // scenes and we want to avoid that in a loop.
    // TODO(all): Consider using Ldp and Stp.
#if defined(__CHERI_PURE_CAPABILITY__)
    Register dst = c16;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register dst = x16;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ SlotAddress(dst, argc);
    __ Add(argc, argc, len);  // Update new argc.
    __ Bind(&loop);
    __ Sub(len, len, 1);
    __ LoadAnyTaggedField(scratch, MemOperand(src, kTaggedSize, PostIndex));
    __ CmpTagged(scratch, the_hole_value);
    __ Csel(scratch, scratch, undefined_value, ne);
    __ Str(scratch, MemOperand(dst, kSystemPointerSize, PostIndex));
    __ Cbnz(len, &loop);
  }
  __ Bind(&done);
  // Tail-call to the actual Call or Construct builtin.
  __ Jump(code, RelocInfo::CODE_TARGET);

  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallOrConstructForwardVarargs(MacroAssembler* masm,
                                                      CallOrConstructMode mode,
                                                      Handle<CodeT> code) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x3 : the new.target (for [[Construct]] calls)
  //  -- x1 : the target to call (can be any Object)
  //  -- x2 : start index (to support rest parameters)
  // -----------------------------------

  Register argc = x0;
  Register start_index = x2;

  // Check if new.target has a [[Construct]] internal method.
  if (mode == CallOrConstructMode::kConstruct) {
    Label new_target_constructor, new_target_not_constructor;
#if defined(__CHERI_PURE_CAPABILITY__)
    __ JumpIfSmi(c3, &new_target_not_constructor);
    __ LoadTaggedPointerField(c5, FieldMemOperand(c3, HeapObject::kMapOffset));
    __ Ldrb(x5, FieldMemOperand(c5, Map::kBitFieldOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ JumpIfSmi(x3, &new_target_not_constructor);
    __ LoadTaggedPointerField(x5, FieldMemOperand(x3, HeapObject::kMapOffset));
    __ Ldrb(x5, FieldMemOperand(x5, Map::kBitFieldOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ TestAndBranchIfAnySet(x5, Map::Bits1::IsConstructorBit::kMask,
                             &new_target_constructor);
    __ Bind(&new_target_not_constructor);
    {
      FrameScope scope(masm, StackFrame::MANUAL);
      __ EnterFrame(StackFrame::INTERNAL);
#if defined(__CHERI_PURE_CAPABILITY__)
      __ PushArgument(c3);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ PushArgument(x3);
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ CallRuntime(Runtime::kThrowNotConstructor);
      __ Unreachable();
    }
    __ Bind(&new_target_constructor);
  }

  Register len = x6;
  Label stack_done, stack_overflow;
  __ Ldr(len, MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ Subs(len, len, kJSArgcReceiverSlots);
  __ Subs(len, len, start_index);
  __ B(le, &stack_done);
  // Check for stack overflow.
  __ StackOverflowCheck(len, &stack_overflow);

  Generate_PrepareForCopyingVarargs(masm, argc, len);

  // Push varargs.
  {
#if defined(__CHERI_PURE_CAPABILITY__)
    Register args_fp = c5;
    Register dst = c13;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register args_fp = x5;
    Register dst = x13;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    // Point to the fist argument to copy from (skipping receiver).
    __ Add(args_fp, fp,
           CommonFrameConstants::kFixedFrameSizeAboveFp + kSystemPointerSize);
    __ lsl(start_index, start_index, kSystemPointerSizeLog2);
    __ Add(args_fp, args_fp, start_index);
    // Point to the position to copy to.
    __ SlotAddress(dst, argc);
    // Update total number of arguments.
    __ Add(argc, argc, len);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CopyCapabilities(dst, args_fp, len);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CopyDoubleWords(dst, args_fp, len);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ B(&stack_done);

  __ Bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
  __ Bind(&stack_done);

  __ Jump(code, RelocInfo::CODE_TARGET);
}

// static
void Builtins::Generate_CallFunction(MacroAssembler* masm,
                                     ConvertReceiverMode mode) {
  ASM_LOCATION("Builtins::Generate_CallFunction");
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the function to call (checked to be a JSFunction)
  // -----------------------------------
  Register arg_count = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register js_func = c1;
  Register shared_func_info = c2;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register js_func = x1;
  Register shared_func_info = x2;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ AssertCallableFunction(js_func);
  __ LoadTaggedPointerField(
      shared_func_info, FieldMemOperand(js_func, JSFunction::kSharedFunctionInfoOffset));

  // Enter the context of the function; ToObject has to run in the function
  // context, and we also need to take the global proxy from the function
  // context in case of conversion.
  __ LoadTaggedPointerField(cp,
                            FieldMemOperand(js_func, JSFunction::kContextOffset));
  // We need to convert the receiver for non-native sloppy mode functions.
  Label done_convert;
  __ Ldr(w3, FieldMemOperand(shared_func_info, SharedFunctionInfo::kFlagsOffset));
  __ TestAndBranchIfAnySet(w3,
                           SharedFunctionInfo::IsNativeBit::kMask |
                               SharedFunctionInfo::IsStrictBit::kMask,
                           &done_convert);
  {
    // ----------- S t a t e -------------
    //  -- x0 : the number of arguments
    //  -- x1 : the function to call (checked to be a JSFunction)
    //  -- x2 : the shared function info.
    //  -- cp : the function context.
    // -----------------------------------

    if (mode == ConvertReceiverMode::kNullOrUndefined) {
      // Patch receiver to global proxy.
#if defined(__CHERI_PURE_CAPABILITY__)
      __ LoadGlobalProxy(c3);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ LoadGlobalProxy(x3);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    } else {
      Label convert_to_object, convert_receiver;
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Peek(c3, __ ReceiverOperand(arg_count));
      __ JumpIfSmi(c3, &convert_to_object);
      static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
      __ CompareObjectType(c3, c4, x4, FIRST_JS_RECEIVER_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Peek(x3, __ ReceiverOperand(arg_count));
      __ JumpIfSmi(x3, &convert_to_object);
      static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
      __ CompareObjectType(x3, x4, x4, FIRST_JS_RECEIVER_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ B(hs, &done_convert);
      if (mode != ConvertReceiverMode::kNotNullOrUndefined) {
        Label convert_global_proxy;
#if defined(__CHERI_PURE_CAPABILITY__)
        __ JumpIfRoot(c3, RootIndex::kUndefinedValue, &convert_global_proxy);
        __ JumpIfNotRoot(c3, RootIndex::kNullValue, &convert_to_object);
#else // defined(__CHERI_PURE_CAPABILITY__)
        __ JumpIfRoot(x3, RootIndex::kUndefinedValue, &convert_global_proxy);
        __ JumpIfNotRoot(x3, RootIndex::kNullValue, &convert_to_object);
#endif // defined(__CHERI_PURE_CAPABILITY__)
        __ Bind(&convert_global_proxy);
        {
          // Patch receiver to global proxy.
#if defined(__CHERI_PURE_CAPABILITY__)
          __ LoadGlobalProxy(c3);
#else // defined(__CHERI_PURE_CAPABILITY__)
          __ LoadGlobalProxy(x3);
#endif // defined(__CHERI_PURE_CAPABILITY__)
        }
        __ B(&convert_receiver);
      }
      __ Bind(&convert_to_object);
      {
        // Convert receiver using ToObject.
        // TODO(bmeurer): Inline the allocation here to avoid building the frame
        // in the fast case? (fall back to AllocateInNewSpace?)
        FrameScope scope(masm, StackFrame::INTERNAL);
        __ SmiTag(arg_count);
#if defined(__CHERI_PURE_CAPABILITY__)
        __ Push(padreg, arg_count);
        __ Push(js_func, cp);
#else // defined(__CHERI_PURE_CAPABILITY__)
        __ Push(padreg, arg_count, js_func, cp);
#endif // defined(__CHERI_PURE_CAPABILITY__)
        __ Mov(arg_count, x3);
        __ Call(BUILTIN_CODE(masm->isolate(), ToObject),
                RelocInfo::CODE_TARGET);
        __ Mov(x3, arg_count);
#if defined(__CHERI_PURE_CAPABILITY__)
        __ Pop(cp, js_func, arg_count.C(), padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
        __ Pop(cp, js_func, arg_count, padreg);
#endif // defined(__CHERI_PURE_CAPABILITY__)
        __ SmiUntag(arg_count);
      }
      __ LoadTaggedPointerField(
          shared_func_info, FieldMemOperand(js_func, JSFunction::kSharedFunctionInfoOffset));
      __ Bind(&convert_receiver);
    }
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Poke(c3, __ ReceiverOperand(arg_count));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Poke(x3, __ ReceiverOperand(arg_count));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ Bind(&done_convert);

  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the function to call (checked to be a JSFunction)
  //  -- x2 : the shared function info.
  //  -- cp : the function context.
  // -----------------------------------

#if defined(__CHERI_PURE_CAPABILITY__)
  Register shared_func_info_value = shared_func_info.X();
  __ Ldrh(shared_func_info_value,
          FieldMemOperand(shared_func_info, SharedFunctionInfo::kFormalParameterCountOffset));
  __ InvokeFunctionCode(js_func, no_reg, shared_func_info_value, arg_count, InvokeType::kJump);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldrh(shared_func_info,
          FieldMemOperand(shared_func_info, SharedFunctionInfo::kFormalParameterCountOffset));
  __ InvokeFunctionCode(js_func, no_reg, shared_func_info, arg_count, InvokeType::kJump);
#endif // defined(__CHERI_PURE_CAPABILITY__)
}

namespace {

void Generate_PushBoundArguments(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : target (checked to be a JSBoundFunction)
  //  -- x3 : new.target (only in case of [[Construct]])
  // -----------------------------------

  Register bound_argc = x4;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register bound_argv = c2;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register bound_argv = x2;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Load [[BoundArguments]] into x2 and length of that into x4.
  Label no_bound_arguments;
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      bound_argv, FieldMemOperand(c1, JSBoundFunction::kBoundArgumentsOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      bound_argv, FieldMemOperand(x1, JSBoundFunction::kBoundArgumentsOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ SmiUntagField(bound_argc,
                   FieldMemOperand(bound_argv, FixedArray::kLengthOffset));
  __ Cbz(bound_argc, &no_bound_arguments);
  {
    // ----------- S t a t e -------------
    //  -- x0 : the number of arguments
    //  -- x1 : target (checked to be a JSBoundFunction)
    //  -- x2 : the [[BoundArguments]] (implemented as FixedArray)
    //  -- x3 : new.target (only in case of [[Construct]])
    //  -- x4 : the number of [[BoundArguments]]
    // -----------------------------------

    Register argc = x0;

    // Check for stack overflow.
    {
      // Check the stack for overflow. We are not trying to catch interruptions
      // (i.e. debug break and preemption) here, so check the "real stack
      // limit".
      Label done;
      __ LoadStackLimit(x10, StackLimitKind::kRealStackLimit);
      // Make x10 the space we have left. The stack might already be overflowed
      // here which will cause x10 to become negative.
#if defined(__CHERI_PURE_CAPABILITY__)
      {
        UseScratchRegisterScope temps(masm);
        Register temp = temps.AcquireX();
        __ Gcvalue(csp, temp);
        __ Sub(x10, temp, x10);
      }
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Sub(x10, sp, x10);
#endif // defined(__CHERI_PURE_CAPABILITY__)
      // Check if the arguments will overflow the stack.
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Cmp(x10, Operand(bound_argc, LSL, kSystemPointerSizeLog2));
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Cmp(x10, Operand(bound_argc, LSL, kSystemPointerSizeLog2));
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ B(gt, &done);
      __ TailCallRuntime(Runtime::kThrowStackOverflow);
      __ Bind(&done);
    }

    Label copy_bound_args;
    Register total_argc = x15;
    Register slots_to_claim = x12;
#if defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = c10;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = x10;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    Register receiver = x14;

    __ Sub(argc, argc, kJSArgcReceiverSlots);
    __ Add(total_argc, argc, bound_argc);
    __ Peek(receiver, 0);

    // Round up slots_to_claim to an even number if it is odd.
    __ Add(slots_to_claim, bound_argc, 1);
    __ Bic(slots_to_claim, slots_to_claim, 1);
    __ Claim(slots_to_claim, kSystemPointerSize);

    __ Tbz(bound_argc, 0, &copy_bound_args);
    {
      Label argc_even;
      __ Tbz(argc, 0, &argc_even);
      // Arguments count is odd (with the receiver it's even), so there's no
      // alignment padding above the arguments and we have to "add" it. We
      // claimed bound_argc + 1, since it is odd and it was rounded up. +1 here
      // is for stack alignment padding.
      // 1. Shift args one slot down.
      {
#if defined(__CHERI_PURE_CAPABILITY__)
        Register copy_from = c11;
        Register copy_to = c12;
#else // defined(__CHERI_PURE_CAPABILITY__)
        Register copy_from = x11;
        Register copy_to = x12;
#endif // defined(__CHERI_PURE_CAPABILITY__)
        __ SlotAddress(copy_to, slots_to_claim);
        __ Add(copy_from, copy_to, kSystemPointerSize);
#if defined(__CHERI_PURE_CAPABILITY__)
        __ CopyCapabilities(copy_to, copy_from, argc);
#else // defined(__CHERI_PURE_CAPABILITY__)
        __ CopyDoubleWords(copy_to, copy_from, argc);
#endif // defined(__CHERI_PURE_CAPABILITY__)
      }
      // 2. Write a padding in the last slot.
#if defined(__CHERI_PURE_CAPABILITY__)
      __ Add(scratch.X(), total_argc, 1);
      __ Str(padregc, MemOperand(csp, scratch.X(), LSL, kSystemPointerSizeLog2));
#else // defined(__CHERI_PURE_CAPABILITY__)
      __ Add(scratch, total_argc, 1);
      __ Str(padreg, MemOperand(sp, scratch, LSL, kSystemPointerSizeLog2));
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ B(&copy_bound_args);

      __ Bind(&argc_even);
      // Arguments count is even (with the receiver it's odd), so there's an
      // alignment padding above the arguments and we can reuse it. We need to
      // claim bound_argc - 1, but we claimed bound_argc + 1, since it is odd
      // and it was rounded up.
      // 1. Drop 2.
      __ Drop(2);
      // 2. Shift args one slot up.
      {
#if defined(__CHERI_PURE_CAPABILITY__)
        Register copy_from = c11;
        Register copy_to = c12;
#else // defined(__CHERI_PURE_CAPABILITY__)
        Register copy_from = x11;
        Register copy_to = x12;
#endif // defined(__CHERI_PURE_CAPABILITY__)
        __ SlotAddress(copy_to, total_argc);
        __ Sub(copy_from, copy_to, kSystemPointerSize);
#if defined(__CHERI_PURE_CAPABILITY__)
        __ CopyCapabilities(copy_to, copy_from, argc,
                           TurboAssembler::kCapSrcLessThanDst);
#else // defined(__CHERI_PURE_CAPABILITY__)
        __ CopyDoubleWords(copy_to, copy_from, argc,
                           TurboAssembler::kSrcLessThanDst);
#endif // defined(__CHERI_PURE_CAPABILITY__)
      }
    }

    // If bound_argc is even, there is no alignment massage to do, and we have
    // already claimed the correct number of slots (bound_argc).
    __ Bind(&copy_bound_args);

    // Copy the receiver back.
    __ Poke(receiver, 0);
    // Copy [[BoundArguments]] to the stack (below the receiver).
    {
      Label loop;
      Register counter = bound_argc;
#if defined(__CHERI_PURE_CAPABILITY__)
      Register copy_to = c12;
#else // defined(__CHERI_PURE_CAPABILITY__)
      Register copy_to = x12;
#endif // defined(__CHERI_PURE_CAPABILITY__)
      __ Add(bound_argv, bound_argv, FixedArray::kHeaderSize - kHeapObjectTag);
      __ SlotAddress(copy_to, 1);
      __ Bind(&loop);
      __ Sub(counter, counter, 1);
      __ LoadAnyTaggedField(scratch,
                            MemOperand(bound_argv, kTaggedSize, PostIndex));
      __ Str(scratch, MemOperand(copy_to, kSystemPointerSize, PostIndex));
      __ Cbnz(counter, &loop);
    }
    // Update argc.
    __ Add(argc, total_argc, kJSArgcReceiverSlots);
  }
  __ Bind(&no_bound_arguments);
}

}  // namespace

// static
void Builtins::Generate_CallBoundFunctionImpl(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the function to call (checked to be a JSBoundFunction)
  // -----------------------------------
#if defined(__CHERI_PURE_CAPABILITY__)
  __ AssertBoundFunction(c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ AssertBoundFunction(x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Patch the receiver to [[BoundThis]].
#if defined(__CHERI_PURE_CAPABILITY__)
  __ LoadAnyTaggedField(c10,
                        FieldMemOperand(c1, JSBoundFunction::kBoundThisOffset));
  __ Poke(c10, __ ReceiverOperand(x0));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadAnyTaggedField(x10,
                        FieldMemOperand(x1, JSBoundFunction::kBoundThisOffset));
  __ Poke(x10, __ ReceiverOperand(x0));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Push the [[BoundArguments]] onto the stack.
  Generate_PushBoundArguments(masm);

  // Call the [[BoundTargetFunction]] via the Call builtin.
  __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
      c1, FieldMemOperand(c1, JSBoundFunction::kBoundTargetFunctionOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
      x1, FieldMemOperand(x1, JSBoundFunction::kBoundTargetFunctionOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Jump(BUILTIN_CODE(masm->isolate(), Call_ReceiverIsAny),
          RelocInfo::CODE_TARGET);
}

// static
void Builtins::Generate_Call(MacroAssembler* masm, ConvertReceiverMode mode) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the target to call (can be any Object).
  // -----------------------------------
  Register argc = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register target = c1;
  Register map = c4;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register target = x1;
  Register map = x4;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register instance_type = x5;
  DCHECK(!AreAliased(argc, target, map, instance_type));

  Label non_callable, class_constructor;
  __ JumpIfSmi(target, &non_callable);
  __ LoadMap(map, target);
  __ CompareInstanceTypeRange(map, instance_type,
                              FIRST_CALLABLE_JS_FUNCTION_TYPE,
                              LAST_CALLABLE_JS_FUNCTION_TYPE);
  __ Jump(masm->isolate()->builtins()->CallFunction(mode),
          RelocInfo::CODE_TARGET, ls);
  __ Cmp(instance_type, JS_BOUND_FUNCTION_TYPE);
  __ Jump(BUILTIN_CODE(masm->isolate(), CallBoundFunction),
          RelocInfo::CODE_TARGET, eq);

  // Check if target has a [[Call]] internal method.
  {
    Register flags = x4;
    __ Ldrb(flags, FieldMemOperand(map, Map::kBitFieldOffset));
    map = no_reg;
    __ TestAndBranchIfAllClear(flags, Map::Bits1::IsCallableBit::kMask,
                               &non_callable);
  }

  // Check if target is a proxy and call CallProxy external builtin
  __ Cmp(instance_type, JS_PROXY_TYPE);
  __ Jump(BUILTIN_CODE(masm->isolate(), CallProxy), RelocInfo::CODE_TARGET, eq);

  // Check if target is a wrapped function and call CallWrappedFunction external
  // builtin
  __ Cmp(instance_type, JS_WRAPPED_FUNCTION_TYPE);
  __ Jump(BUILTIN_CODE(masm->isolate(), CallWrappedFunction),
          RelocInfo::CODE_TARGET, eq);

  // ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList)
  // Check that the function is not a "classConstructor".
  __ Cmp(instance_type, JS_CLASS_CONSTRUCTOR_TYPE);
  __ B(eq, &class_constructor);

  // 2. Call to something else, which might have a [[Call]] internal method (if
  // not we raise an exception).
  // Overwrite the original receiver with the (original) target.
  __ Poke(target, __ ReceiverOperand(argc));

  // Let the "call_as_function_delegate" take care of the rest.
  __ LoadNativeContextSlot(target, Context::CALL_AS_FUNCTION_DELEGATE_INDEX);
  __ Jump(masm->isolate()->builtins()->CallFunction(
              ConvertReceiverMode::kNotNullOrUndefined),
          RelocInfo::CODE_TARGET);

  // 3. Call to something that is not callable.
  __ bind(&non_callable);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ PushArgument(target);
    __ CallRuntime(Runtime::kThrowCalledNonCallable);
    __ Unreachable();
  }

  // 4. The function is a "classConstructor", need to raise an exception.
  __ bind(&class_constructor);
  {
    FrameScope frame(masm, StackFrame::INTERNAL);
    __ PushArgument(target);
    __ CallRuntime(Runtime::kThrowConstructorNonCallableError);
    __ Unreachable();
  }
}

// static
void Builtins::Generate_ConstructFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the constructor to call (checked to be a JSFunction)
  //  -- x3 : the new target (checked to be a constructor)
  // -----------------------------------
#if defined(__CHERI_PURE_CAPABILITY__)
  __ AssertConstructor(c1);
  __ AssertFunction(c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ AssertConstructor(x1);
  __ AssertFunction(x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Calling convention for function specific ConstructStubs require
  // x2 to contain either an AllocationSite or undefined.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ LoadRoot(c2, RootIndex::kUndefinedValue);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadRoot(x2, RootIndex::kUndefinedValue);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  Label call_generic_stub;

  // Jump to JSBuiltinsConstructStub or JSConstructStubGeneric.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      c4, FieldMemOperand(c1, JSFunction::kSharedFunctionInfoOffset));
  __ Ldr(w4, FieldMemOperand(c4, SharedFunctionInfo::kFlagsOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      x4, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));
  __ Ldr(w4, FieldMemOperand(x4, SharedFunctionInfo::kFlagsOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ TestAndBranchIfAllClear(
      w4, SharedFunctionInfo::ConstructAsBuiltinBit::kMask, &call_generic_stub);

  __ Jump(BUILTIN_CODE(masm->isolate(), JSBuiltinsConstructStub),
          RelocInfo::CODE_TARGET);

  __ bind(&call_generic_stub);
  __ Jump(BUILTIN_CODE(masm->isolate(), JSConstructStubGeneric),
          RelocInfo::CODE_TARGET);
}

// static
void Builtins::Generate_ConstructBoundFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the function to call (checked to be a JSBoundFunction)
  //  -- x3 : the new target (checked to be a constructor)
  // -----------------------------------
#if defined(__CHERI_PURE_CAPABILITY__)
  __ AssertConstructor(c1);
  __ AssertBoundFunction(c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ AssertConstructor(x1);
  __ AssertBoundFunction(x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Push the [[BoundArguments]] onto the stack.
  Generate_PushBoundArguments(masm);

  // Patch new.target to [[BoundTargetFunction]] if new.target equals target.
  {
    Label done;
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CmpTagged(c1, c3);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CmpTagged(x1, x3);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ B(ne, &done);
    __ LoadTaggedPointerField(
#if defined(__CHERI_PURE_CAPABILITY__)
        c3, FieldMemOperand(c1, JSBoundFunction::kBoundTargetFunctionOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
        x3, FieldMemOperand(x1, JSBoundFunction::kBoundTargetFunctionOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Bind(&done);
  }

  // Construct the [[BoundTargetFunction]] via the Construct builtin.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      c1, FieldMemOperand(c1, JSBoundFunction::kBoundTargetFunctionOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      x1, FieldMemOperand(x1, JSBoundFunction::kBoundTargetFunctionOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Jump(BUILTIN_CODE(masm->isolate(), Construct), RelocInfo::CODE_TARGET);
}

// static
void Builtins::Generate_Construct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the constructor to call (can be any Object)
  //  -- x3 : the new target (either the same as the constructor or
  //          the JSFunction on which new was invoked initially)
  // -----------------------------------
  Register argc = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register target = c1;
  Register map = c4;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register target = x1;
  Register map = x4;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register instance_type = x5;
  DCHECK(!AreAliased(argc, target, map, instance_type));

  // Check if target is a Smi.
  Label non_constructor, non_proxy;
  __ JumpIfSmi(target, &non_constructor);

  // Check if target has a [[Construct]] internal method.
  __ LoadTaggedPointerField(map,
                            FieldMemOperand(target, HeapObject::kMapOffset));
  {
    Register flags = x2;
    DCHECK(!AreAliased(argc, target, map, instance_type, flags));
    __ Ldrb(flags, FieldMemOperand(map, Map::kBitFieldOffset));
    __ TestAndBranchIfAllClear(flags, Map::Bits1::IsConstructorBit::kMask,
                               &non_constructor);
  }

  // Dispatch based on instance type.
  __ CompareInstanceTypeRange(map, instance_type, FIRST_JS_FUNCTION_TYPE,
                              LAST_JS_FUNCTION_TYPE);
  __ Jump(BUILTIN_CODE(masm->isolate(), ConstructFunction),
          RelocInfo::CODE_TARGET, ls);

  // Only dispatch to bound functions after checking whether they are
  // constructors.
  __ Cmp(instance_type, JS_BOUND_FUNCTION_TYPE);
  __ Jump(BUILTIN_CODE(masm->isolate(), ConstructBoundFunction),
          RelocInfo::CODE_TARGET, eq);

  // Only dispatch to proxies after checking whether they are constructors.
  __ Cmp(instance_type, JS_PROXY_TYPE);
  __ B(ne, &non_proxy);
  __ Jump(BUILTIN_CODE(masm->isolate(), ConstructProxy),
          RelocInfo::CODE_TARGET);

  // Called Construct on an exotic Object with a [[Construct]] internal method.
  __ bind(&non_proxy);
  {
    // Overwrite the original receiver with the (original) target.
    __ Poke(target, __ ReceiverOperand(argc));

    // Let the "call_as_constructor_delegate" take care of the rest.
    __ LoadNativeContextSlot(target,
                             Context::CALL_AS_CONSTRUCTOR_DELEGATE_INDEX);
    __ Jump(masm->isolate()->builtins()->CallFunction(),
            RelocInfo::CODE_TARGET);
  }

  // Called Construct on an Object that doesn't have a [[Construct]] internal
  // method.
  __ bind(&non_constructor);
  __ Jump(BUILTIN_CODE(masm->isolate(), ConstructedNonConstructable),
          RelocInfo::CODE_TARGET);
}

#if V8_ENABLE_WEBASSEMBLY
void Builtins::Generate_WasmCompileLazy(MacroAssembler* masm) {
  // The function index was put in w8 by the jump table trampoline.
  // Sign extend and convert to Smi for the runtime call.
  __ sxtw(kWasmCompileLazyFuncIndexRegister,
          kWasmCompileLazyFuncIndexRegister.W());
  __ SmiTag(kWasmCompileLazyFuncIndexRegister);

  // Compute register lists for parameters to be saved. We save all parameter
  // registers (see wasm-linkage.h). They might be overwritten in the runtime
  // call below. We don't have any callee-saved registers in wasm, so no need to
  // store anything else.
  constexpr RegList kSavedGpRegs = ([]() constexpr {
    RegList saved_gp_regs;
    for (Register gp_param_reg : wasm::kGpParamRegisters) {
      saved_gp_regs.set(gp_param_reg);
    }
    // Also push x1, because we must push multiples of 16 bytes (see
    // {TurboAssembler::PushCPURegList}.
#if defined(__CHERI_PURE_CAPABILITY__)
    saved_gp_regs.set(c1);
#else // defined(__CHERI_PURE_CAPABILITY__)
    saved_gp_regs.set(x1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    // All set registers were unique.
    CHECK_EQ(saved_gp_regs.Count(), arraysize(wasm::kGpParamRegisters) + 1);
    // We push a multiple of 16 bytes.
    CHECK_EQ(0, saved_gp_regs.Count() % 2);
    // The Wasm instance must be part of the saved registers.
    CHECK(saved_gp_regs.has(kWasmInstanceRegister));
    CHECK_EQ(WasmCompileLazyFrameConstants::kNumberOfSavedGpParamRegs,
             saved_gp_regs.Count());
    return saved_gp_regs;
  })();

  constexpr DoubleRegList kSavedFpRegs = ([]() constexpr {
    DoubleRegList saved_fp_regs;
    for (DoubleRegister fp_param_reg : wasm::kFpParamRegisters) {
      saved_fp_regs.set(fp_param_reg);
    }

    CHECK_EQ(saved_fp_regs.Count(), arraysize(wasm::kFpParamRegisters));
    CHECK_EQ(WasmCompileLazyFrameConstants::kNumberOfSavedFpParamRegs,
             saved_fp_regs.Count());
    return saved_fp_regs;
  })();

  {
    HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
    FrameScope scope(masm, StackFrame::WASM_COMPILE_LAZY);

    // Save registers that we need to keep alive across the runtime call.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ PushCRegList(kSavedGpRegs);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ PushXRegList(kSavedGpRegs);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ PushQRegList(kSavedFpRegs);

    // Pass instance and function index as explicit arguments to the runtime
    // function.
    __ Push(kWasmInstanceRegister, kWasmCompileLazyFuncIndexRegister);
    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Mov(cp, Smi::zero());
    __ CallRuntime(Runtime::kWasmCompileLazy, 2);

    // Untag the returned Smi into into x17, for later use.
    static_assert(!kSavedGpRegs.has(x17));
    __ SmiUntag(x17, kReturnRegister0);

    // Restore registers.
    __ PopQRegList(kSavedFpRegs);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ PopCRegList(kSavedGpRegs);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ PopXRegList(kSavedGpRegs);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  // The runtime function returned the jump table slot offset as a Smi (now in
  // x17). Use that to compute the jump target.
  static_assert(!kSavedGpRegs.has(x18));
#if defined(__CHERI_PURE_CAPABILITY__)
  __ ldr(c18, MemOperand(
#else
  __ ldr(x18, MemOperand(
#endif // defined(__CHERI_PURE_CAPABILITY__)
                  kWasmInstanceRegister,
                  WasmInstanceObject::kJumpTableStartOffset - kHeapObjectTag));
#if defined(__CHERI_PURE_CAPABILITY__)
  __ add(c17, c18, Operand(c17));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ add(x17, x18, Operand(x17));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  // Finally, jump to the jump table slot for the function.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Jump(c17);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Jump(x17);
#endif // defined(__CHERI_PURE_CAPABILITY__)
}

void Builtins::Generate_WasmDebugBreak(MacroAssembler* masm) {
  HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
  {
    FrameScope scope(masm, StackFrame::WASM_DEBUG_BREAK);

    // Save all parameter registers. They might hold live values, we restore
    // them after the runtime call.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ PushCRegList(WasmDebugBreakFrameConstants::kPushedGpRegs);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ PushXRegList(WasmDebugBreakFrameConstants::kPushedGpRegs);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ PushQRegList(WasmDebugBreakFrameConstants::kPushedFpRegs);

    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Move(cp, Smi::zero());
    __ CallRuntime(Runtime::kWasmDebugBreak, 0);

    // Restore registers.
    __ PopQRegList(WasmDebugBreakFrameConstants::kPushedFpRegs);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ PopCRegList(WasmDebugBreakFrameConstants::kPushedGpRegs);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ PopXRegList(WasmDebugBreakFrameConstants::kPushedGpRegs);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ Ret();
}

void Builtins::Generate_GenericJSToWasmWrapper(MacroAssembler* masm) {
  // TODO(v8:10701): Implement for this platform.
  __ Trap();
}

void Builtins::Generate_WasmReturnPromiseOnSuspend(MacroAssembler* masm) {
  // TODO(v8:12191): Implement for this platform.
  __ Trap();
}

void Builtins::Generate_WasmSuspend(MacroAssembler* masm) {
  // TODO(v8:12191): Implement for this platform.
  __ Trap();
}

void Builtins::Generate_WasmResume(MacroAssembler* masm) {
  // TODO(v8:12191): Implement for this platform.
  __ Trap();
}

void Builtins::Generate_WasmOnStackReplace(MacroAssembler* masm) {
  // Only needed on x64.
  __ Trap();
}
#endif  // V8_ENABLE_WEBASSEMBLY

void Builtins::Generate_CEntry(MacroAssembler* masm, int result_size,
                               SaveFPRegsMode save_doubles, ArgvMode argv_mode,
                               bool builtin_exit_frame) {
  // The Abort mechanism relies on CallRuntime, which in turn relies on
  // CEntry, so until this stub has been generated, we have to use a
  // fall-back Abort mechanism.
  //
  // Note that this stub must be generated before any use of Abort.
  HardAbortScope hard_aborts(masm);

  ASM_LOCATION("CEntry::Generate entry");

  // Register parameters:
  //    x0: argc (including receiver, untagged)
  //    x1: target
  // If argv_mode == ArgvMode::kRegister:
  //    x11: argv (pointer to first argument)
  //
  // The stack on entry holds the arguments and the receiver, with the receiver
  // at the highest address:
  //
  //    sp[argc-1]: receiver
  //    sp[argc-2]: arg[argc-2]
  //    ...           ...
  //    sp[1]:      arg[1]
  //    sp[0]:      arg[0]
  //
  // The arguments are in reverse order, so that arg[argc-2] is actually the
  // first argument to the target function and arg[0] is the last.
  const Register& argc_input = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  const Register& target_input = c1;
#else // defined(__CHERI_PURE_CAPABILITY__)
  const Register& target_input = x1;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Calculate argv, argc and the target address, and store them in
  // callee-saved registers so we can retry the call without having to reload
  // these arguments.
  // TODO(jbramley): If the first call attempt succeeds in the common case (as
  // it should), then we might be better off putting these parameters directly
  // into their argument registers, rather than using callee-saved registers and
  // preserving them on the stack.
#if defined(__CHERI_PURE_CAPABILITY__)
  const Register& argv = c21;
#else // defined(__CHERI_PURE_CAPABILITY__)
  const Register& argv = x21;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  const Register& argc = x22;
#if defined(__CHERI_PURE_CAPABILITY__)
  const Register& target = c23;
#else // defined(__CHERI_PURE_CAPABILITY__)
  const Register& target = x23;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Derive argv from the stack pointer so that it points to the first argument
  // (arg[argc-2]), or just below the receiver in case there are no arguments.
  //  - Adjust for the arg[] array.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register temp_argv = c11;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register temp_argv = x11;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  if (argv_mode == ArgvMode::kStack) {
    __ SlotAddress(temp_argv, x0);
    //  - Adjust for the receiver.
    __ Sub(temp_argv, temp_argv, 1 * kSystemPointerSize);
  }

  // Reserve three slots to preserve x21-x23 callee-saved registers.
  int extra_stack_space = 3;
  // Enter the exit frame.
  FrameScope scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(
#if defined(__CHERI_PURE_CAPABILITY__)
      save_doubles == SaveFPRegsMode::kSave, c10, extra_stack_space,
#else // defined(__CHERI_PURE_CAPABILITY__)
      save_doubles == SaveFPRegsMode::kSave, x10, extra_stack_space,
#endif // defined(__CHERI_PURE_CAPABILITY__)
      builtin_exit_frame ? StackFrame::BUILTIN_EXIT : StackFrame::EXIT);

  // Poke callee-saved registers into reserved space.
  __ Poke(argv, 1 * kSystemPointerSize);
  __ Poke(argc, 2 * kSystemPointerSize);
  __ Poke(target, 3 * kSystemPointerSize);

  // We normally only keep tagged values in callee-saved registers, as they
  // could be pushed onto the stack by called stubs and functions, and on the
  // stack they can confuse the GC. However, we're only calling C functions
  // which can push arbitrary data onto the stack anyway, and so the GC won't
  // examine that part of the stack.
  __ Mov(argc, argc_input);
  __ Mov(target, target_input);
  __ Mov(argv, temp_argv);

  // x21 : argv
  // x22 : argc
  // x23 : call target
  //
  // The stack (on entry) holds the arguments and the receiver, with the
  // receiver at the highest address:
  //
  //         argv[8]:     receiver
  // argv -> argv[0]:     arg[argc-2]
  //         ...          ...
  //         argv[...]:   arg[1]
  //         argv[...]:   arg[0]
  //
  // Immediately below (after) this is the exit frame, as constructed by
  // EnterExitFrame:
  //         fp[8]:    CallerPC (lr)
  //   fp -> fp[0]:    CallerFP (old fp)
  //         fp[-8]:   Space reserved for SPOffset.
  //         fp[-16]:  CodeObject()
  //         sp[...]:  Saved doubles, if saved_doubles is true.
  //         sp[32]:   Alignment padding, if necessary.
  //         sp[24]:   Preserved x23 (used for target).
  //         sp[16]:   Preserved x22 (used for argc).
  //         sp[8]:    Preserved x21 (used for argv).
  //   sp -> sp[0]:    Space reserved for the return address.
  //
  // After a successful call, the exit frame, preserved registers (x21-x23) and
  // the arguments (including the receiver) are dropped or popped as
  // appropriate. The stub then returns.
  //
  // After an unsuccessful call, the exit frame and suchlike are left
  // untouched, and the stub either throws an exception by jumping to one of
  // the exception_returned label.

  // Prepare AAPCS64 arguments to pass to the builtin.
  __ Mov(x0, argc);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c1, argv);
  __ Mov(c2, ExternalReference::isolate_address(masm->isolate()));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x1, argv);
  __ Mov(x2, ExternalReference::isolate_address(masm->isolate()));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ StoreReturnAddressAndCall(target);

  // Result returned in x0 or x1:x0 - do not destroy these registers!

  //  x0    result0      The return code from the call.
  //  x1    result1      For calls which return ObjectPair.
  //  x21   argv
  //  x22   argc
  //  x23   target
#if defined(__CHERI_PURE_CAPABILITY__)
  const Register& result = c0;
#else // defined(__CHERI_PURE_CAPABILITY__)
  const Register& result = x0;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Check result for exception sentinel.
  Label exception_returned;
  __ CompareRoot(result, RootIndex::kException);
  __ B(eq, &exception_returned);

  // The call succeeded, so unwind the stack and return.

  // Restore callee-saved registers x21-x23.
  __ Mov(x11, argc);

  __ Peek(argv, 1 * kSystemPointerSize);
  __ Peek(argc, 2 * kSystemPointerSize);
  __ Peek(target, 3 * kSystemPointerSize);

  __ LeaveExitFrame(save_doubles == SaveFPRegsMode::kSave, x10, x9);
  if (argv_mode == ArgvMode::kStack) {
    // Drop the remaining stack slots and return from the stub.
    __ DropArguments(x11);
  }
  __ AssertFPCRState();
  __ Ret();

  // Handling of exception.
  __ Bind(&exception_returned);

  ExternalReference pending_handler_context_address = ExternalReference::Create(
      IsolateAddressId::kPendingHandlerContextAddress, masm->isolate());
  ExternalReference pending_handler_entrypoint_address =
      ExternalReference::Create(
          IsolateAddressId::kPendingHandlerEntrypointAddress, masm->isolate());
  ExternalReference pending_handler_fp_address = ExternalReference::Create(
      IsolateAddressId::kPendingHandlerFPAddress, masm->isolate());
  ExternalReference pending_handler_sp_address = ExternalReference::Create(
      IsolateAddressId::kPendingHandlerSPAddress, masm->isolate());

  // Ask the runtime for help to determine the handler. This will set x0 to
  // contain the current pending exception, don't clobber it.
  ExternalReference find_handler =
      ExternalReference::Create(Runtime::kUnwindAndFindExceptionHandler);
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ Mov(x0, 0);  // argc.
    __ Mov(x1, 0);  // argv.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c2, ExternalReference::isolate_address(masm->isolate()));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x2, ExternalReference::isolate_address(masm->isolate()));
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ CallCFunction(find_handler, 3);
  }

  // Retrieve the handler context, SP and FP.
  __ Mov(cp, pending_handler_context_address);
  __ Ldr(cp, MemOperand(cp));
  {
    UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = temps.AcquireC();
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = temps.AcquireX();
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(scratch, pending_handler_sp_address);
    __ Ldr(scratch, MemOperand(scratch));
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(csp, scratch);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(sp, scratch);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ Mov(fp, pending_handler_fp_address);
  __ Ldr(fp, MemOperand(fp));

  // If the handler is a JS frame, restore the context to the frame. Note that
  // the context will be set to (cp == 0) for non-JS frames.
  Label not_js_frame;
  __ Cbz(cp, &not_js_frame);
  __ Str(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ Bind(&not_js_frame);

  {
    // Clear c_entry_fp, like we do in `LeaveExitFrame`.
    UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = temps.AcquireC();
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = temps.AcquireX();
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(scratch, ExternalReference::Create(
                        IsolateAddressId::kCEntryFPAddress, masm->isolate()));
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Str(czr, MemOperand(scratch));
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Str(xzr, MemOperand(scratch));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  // Compute the handler entry address and jump to it. We use x17 here for the
  // jump target, as this jump can occasionally end up at the start of
  // InterpreterEnterAtBytecode, which when CFI is enabled starts with
  // a "BTI c".
  UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
  temps.Exclude(c17);
  __ Mov(c17, pending_handler_entrypoint_address);
  __ Ldr(c17, MemOperand(c17));
  __ Orr(c17, c17, 0x1);
  __ Br(c17);
#else // defined(__CHERI_PURE_CAPABILITY__)
  temps.Exclude(x17);
  __ Mov(x17, pending_handler_entrypoint_address);
  __ Ldr(x17, MemOperand(x17));
  __ Br(x17);
#endif // defined(__CHERI_PURE_CAPABILITY__)
}

void Builtins::Generate_DoubleToI(MacroAssembler* masm) {
  Label done;
  Register result = x7;

  DCHECK(result.Is64Bits());

  HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
  UseScratchRegisterScope temps(masm);
  Register scratch1 = temps.AcquireX();
  Register scratch2 = temps.AcquireX();
  DoubleRegister double_scratch = temps.AcquireD();

  // Account for saved regs.
  const int kArgumentOffset = 2 * kSystemPointerSize;

  __ Push(result, scratch1);  // scratch1 is also pushed to preserve alignment.
  __ Peek(double_scratch, kArgumentOffset);

  // Try to convert with a FPU convert instruction.  This handles all
  // non-saturating cases.
  __ TryConvertDoubleToInt64(result, double_scratch, &done);
  __ Fmov(result, double_scratch);

  // If we reach here we need to manually convert the input to an int32.

  // Extract the exponent.
  Register exponent = scratch1;
  __ Ubfx(exponent, result, HeapNumber::kMantissaBits,
          HeapNumber::kExponentBits);

  // It the exponent is >= 84 (kMantissaBits + 32), the result is always 0 since
  // the mantissa gets shifted completely out of the int32_t result.
  __ Cmp(exponent, HeapNumber::kExponentBias + HeapNumber::kMantissaBits + 32);
  __ CzeroX(result, ge);
  __ B(ge, &done);

  // The Fcvtzs sequence handles all cases except where the conversion causes
  // signed overflow in the int64_t target. Since we've already handled
  // exponents >= 84, we can guarantee that 63 <= exponent < 84.

  if (FLAG_debug_code) {
    __ Cmp(exponent, HeapNumber::kExponentBias + 63);
    // Exponents less than this should have been handled by the Fcvt case.
    __ Check(ge, AbortReason::kUnexpectedValue);
  }

  // Isolate the mantissa bits, and set the implicit '1'.
  Register mantissa = scratch2;
  __ Ubfx(mantissa, result, 0, HeapNumber::kMantissaBits);
  __ Orr(mantissa, mantissa, 1ULL << HeapNumber::kMantissaBits);

  // Negate the mantissa if necessary.
  __ Tst(result, kXSignMask);
  __ Cneg(mantissa, mantissa, ne);

  // Shift the mantissa bits in the correct place. We know that we have to shift
  // it left here, because exponent >= 63 >= kMantissaBits.
  __ Sub(exponent, exponent,
         HeapNumber::kExponentBias + HeapNumber::kMantissaBits);
  __ Lsl(result, mantissa, exponent);

  __ Bind(&done);
  __ Poke(result, kArgumentOffset);
  __ Pop(scratch1, result);
  __ Ret();
}

namespace {

// The number of register that CallApiFunctionAndReturn will need to save on
// the stack. The space for these registers need to be allocated in the
// ExitFrame before calling CallApiFunctionAndReturn.
constexpr int kCallApiFunctionSpillSpace = 4;

int AddressOffset(ExternalReference ref0, ExternalReference ref1) {
  return static_cast<int>(ref0.address() - ref1.address());
}

// Calls an API function. Allocates HandleScope, extracts returned value
// from handle and propagates exceptions.
// 'stack_space' is the space to be unwound on exit (includes the call JS
// arguments space and the additional space allocated for the fast call).
// 'spill_offset' is the offset from the stack pointer where
// CallApiFunctionAndReturn can spill registers.
void CallApiFunctionAndReturn(MacroAssembler* masm, Register function_address,
                              ExternalReference thunk_ref, int stack_space,
                              MemOperand* stack_space_operand, int spill_offset,
                              MemOperand return_value_operand) {
  ASM_CODE_COMMENT(masm);
  ASM_LOCATION("CallApiFunctionAndReturn");
  Isolate* isolate = masm->isolate();
  ExternalReference next_address =
      ExternalReference::handle_scope_next_address(isolate);
  const int kNextOffset = 0;
  const int kLimitOffset = AddressOffset(
      ExternalReference::handle_scope_limit_address(isolate), next_address);
  const int kLevelOffset = AddressOffset(
      ExternalReference::handle_scope_level_address(isolate), next_address);

#if defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(function_address == c1 || function_address == c2);
#else // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(function_address == x1 || function_address == x2);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  Label profiler_enabled, end_profiler_check;
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c10, ExternalReference::is_profiling_address(isolate));
  __ Ldrb(w10, MemOperand(c10));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x10, ExternalReference::is_profiling_address(isolate));
  __ Ldrb(w10, MemOperand(x10));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Cbnz(w10, &profiler_enabled);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c10, ExternalReference::address_of_runtime_stats_flag());
  __ Ldrsw(w10, MemOperand(c10));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x10, ExternalReference::address_of_runtime_stats_flag());
  __ Ldrsw(w10, MemOperand(x10));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Cbnz(w10, &profiler_enabled);
  {
    // Call the api function directly.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c3, function_address);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x3, function_address);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ B(&end_profiler_check);
  }
  __ Bind(&profiler_enabled);
  {
    // Additional parameter is the address of the actual callback.
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(c3, thunk_ref);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(x3, thunk_ref);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  __ Bind(&end_profiler_check);

  // Save the callee-save registers we are going to use.
  // TODO(all): Is this necessary? ARM doesn't do it.
  static_assert(kCallApiFunctionSpillSpace == 4);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Poke(c19, (spill_offset + 0) * kCRegSize);
  __ Poke(c20, (spill_offset + 1) * kCRegSize);
  __ Poke(c21, (spill_offset + 2) * kCRegSize);
  __ Poke(c22, (spill_offset + 3) * kCRegSize);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Poke(x19, (spill_offset + 0) * kXRegSize);
  __ Poke(x20, (spill_offset + 1) * kXRegSize);
  __ Poke(x21, (spill_offset + 2) * kXRegSize);
  __ Poke(x22, (spill_offset + 3) * kXRegSize);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Allocate HandleScope in callee-save registers.
  // We will need to restore the HandleScope after the call to the API function,
  // by allocating it in callee-save registers they will be preserved by C code.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register handle_scope_base = c22;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register handle_scope_base = x22;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register next_address_reg = x19;
  Register limit_reg = x20;
  Register level_reg = w21;

  __ Mov(handle_scope_base, next_address);
  __ Ldr(next_address_reg, MemOperand(handle_scope_base, kNextOffset));
  __ Ldr(limit_reg, MemOperand(handle_scope_base, kLimitOffset));
  __ Ldr(level_reg, MemOperand(handle_scope_base, kLevelOffset));
  __ Add(level_reg, level_reg, 1);
  __ Str(level_reg, MemOperand(handle_scope_base, kLevelOffset));

#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c10, c3);  // TODO(arm64): Load target into c10 directly.
  __ StoreReturnAddressAndCall(c10);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x10, x3);  // TODO(arm64): Load target into x10 directly.
  __ StoreReturnAddressAndCall(x10);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  Label promote_scheduled_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;
  Label return_value_loaded;

  // Load value from ReturnValue.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c0, return_value_operand);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x0, return_value_operand);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Bind(&return_value_loaded);
  // No more valid handles (the result handle was the last one). Restore
  // previous handle scope.
  __ Str(next_address_reg, MemOperand(handle_scope_base, kNextOffset));
  if (FLAG_debug_code) {
    __ Ldr(w1, MemOperand(handle_scope_base, kLevelOffset));
    __ Cmp(w1, level_reg);
    __ Check(eq, AbortReason::kUnexpectedLevelAfterReturnFromApiCall);
  }
  __ Sub(level_reg, level_reg, 1);
  __ Str(level_reg, MemOperand(handle_scope_base, kLevelOffset));
  __ Ldr(x1, MemOperand(handle_scope_base, kLimitOffset));
  __ Cmp(limit_reg, x1);
  __ B(ne, &delete_allocated_handles);

  // Leave the API exit frame.
  __ Bind(&leave_exit_frame);
  // Restore callee-saved registers.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Peek(c19, (spill_offset + 0) * kCRegSize);
  __ Peek(c20, (spill_offset + 1) * kCRegSize);
  __ Peek(c21, (spill_offset + 2) * kCRegSize);
  __ Peek(c22, (spill_offset + 3) * kCRegSize);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Peek(x19, (spill_offset + 0) * kXRegSize);
  __ Peek(x20, (spill_offset + 1) * kXRegSize);
  __ Peek(x21, (spill_offset + 2) * kXRegSize);
  __ Peek(x22, (spill_offset + 3) * kXRegSize);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  if (stack_space_operand != nullptr) {
    DCHECK_EQ(stack_space, 0);
    // Load the number of stack slots to drop before LeaveExitFrame modifies sp.
    __ Ldr(x19, *stack_space_operand);
  }

#if defined(__CHERI_PURE_CAPABILITY__)
  __ LeaveExitFrame(false, c1, x5);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ LeaveExitFrame(false, x1, x5);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Check if the function scheduled an exception.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c5, ExternalReference::scheduled_exception_address(isolate));
  __ Ldr(c5, MemOperand(c5));
  __ JumpIfNotRoot(c5, RootIndex::kTheHoleValue, &promote_scheduled_exception);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x5, ExternalReference::scheduled_exception_address(isolate));
  __ Ldr(x5, MemOperand(x5));
  __ JumpIfNotRoot(x5, RootIndex::kTheHoleValue, &promote_scheduled_exception);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  if (stack_space_operand == nullptr) {
    DCHECK_NE(stack_space, 0);
    __ DropSlots(stack_space);
  } else {
    DCHECK_EQ(stack_space, 0);
    __ DropArguments(x19);
  }

  __ Ret();

  // Re-throw by promoting a scheduled exception.
  __ Bind(&promote_scheduled_exception);
  __ TailCallRuntime(Runtime::kPromoteScheduledException);

  // HandleScope limit has changed. Delete allocated extensions.
  __ Bind(&delete_allocated_handles);
  __ Str(limit_reg, MemOperand(handle_scope_base, kLimitOffset));
  // Save the return value in a callee-save register.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register saved_result = c19;
  __ Mov(saved_result, c0);
  __ Mov(c0, ExternalReference::isolate_address(isolate));
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register saved_result = x19;
  __ Mov(saved_result, x0);
  __ Mov(x0, ExternalReference::isolate_address(isolate));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ CallCFunction(ExternalReference::delete_handle_scope_extensions(), 1);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c0, saved_result);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x0, saved_result);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(&leave_exit_frame);
}

}  // namespace

void Builtins::Generate_CallApiCallback(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- cp                  : context
  //  -- x1                  : api function address
  //  -- x2                  : arguments count (not including the receiver)
  //  -- x3                  : call data
  //  -- x0                  : holder
  //  -- sp[0]               : receiver
  //  -- sp[8]               : first argument
  //  -- ...
  //  -- sp[(argc) * 8]      : last argument
  // -----------------------------------

#if defined(__CHERI_PURE_CAPABILITY__)
  Register api_function_address = c1;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register api_function_address = x1;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register argc = x2;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register call_data = c3;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register call_data = x3;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register holder = x0;
#if defined(__CHERI_PURE_CAPABILITY__)
  Register scratch = c4;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register scratch = x4;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  DCHECK(!AreAliased(api_function_address, argc, call_data, holder, scratch));

  using FCA = FunctionCallbackArguments;

  static_assert(FCA::kArgsLength == 6);
  static_assert(FCA::kNewTargetIndex == 5);
  static_assert(FCA::kDataIndex == 4);
  static_assert(FCA::kReturnValueOffset == 3);
  static_assert(FCA::kReturnValueDefaultValueIndex == 2);
  static_assert(FCA::kIsolateIndex == 1);
  static_assert(FCA::kHolderIndex == 0);

  // Set up FunctionCallbackInfo's implicit_args on the stack as follows:
  //
  // Target state:
  //   sp[0 * kSystemPointerSize]: kHolder
  //   sp[1 * kSystemPointerSize]: kIsolate
  //   sp[2 * kSystemPointerSize]: undefined (kReturnValueDefaultValue)
  //   sp[3 * kSystemPointerSize]: undefined (kReturnValue)
  //   sp[4 * kSystemPointerSize]: kData
  //   sp[5 * kSystemPointerSize]: undefined (kNewTarget)

  // Reserve space on the stack.
  __ Claim(FCA::kArgsLength, kSystemPointerSize);

  // kHolder.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(holder, MemOperand(csp, 0 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(holder, MemOperand(sp, 0 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // kIsolate.
  __ Mov(scratch, ExternalReference::isolate_address(masm->isolate()));
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(csp, 1 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(sp, 1 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // kReturnValueDefaultValue and kReturnValue.
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(csp, 2 * kSystemPointerSize));
  __ Str(scratch, MemOperand(csp, 3 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(sp, 2 * kSystemPointerSize));
  __ Str(scratch, MemOperand(sp, 3 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // kData.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(call_data, MemOperand(csp, 4 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(call_data, MemOperand(sp, 4 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // kNewTarget.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(csp, 5 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(sp, 5 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Keep a pointer to kHolder (= implicit_args) in a scratch register.
  // We use it below to set up the FunctionCallbackInfo object.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(scratch, csp);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(scratch, sp);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Allocate the v8::Arguments structure in the arguments' space, since it's
  // not controlled by GC.
  static constexpr int kApiStackSpace = 4;
  static constexpr bool kDontSaveDoubles = false;

  FrameScope frame_scope(masm, StackFrame::MANUAL);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ EnterExitFrame(kDontSaveDoubles, c10,
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ EnterExitFrame(kDontSaveDoubles, x10,
#endif // defined(__CHERI_PURE_CAPABILITY__)
                    kApiStackSpace + kCallApiFunctionSpillSpace);

  // FunctionCallbackInfo::implicit_args_ (points at kHolder as set up above).
  // Arguments are after the return address (pushed by EnterExitFrame()).
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(csp, 1 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(sp, 1 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // FunctionCallbackInfo::values_ (points at the first varargs argument passed
  // on the stack).
  __ Add(scratch, scratch,
         Operand((FCA::kArgsLength + 1) * kSystemPointerSize));
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(csp, 2 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(scratch, MemOperand(sp, 2 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // FunctionCallbackInfo::length_.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Str(argc, MemOperand(csp, 3 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Str(argc, MemOperand(sp, 3 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // We also store the number of slots to drop from the stack after returning
  // from the API function here.
  // Note: Unlike on other architectures, this stores the number of slots to
  // drop, not the number of bytes. arm64 must always drop a slot count that is
  // a multiple of two, and related helper functions (DropArguments) expect a
  // register containing the slot count.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Add(scratch.X(), argc, Operand(FCA::kArgsLength + 1 /*receiver*/));
  __ Str(scratch, MemOperand(csp, 4 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Add(scratch, argc, Operand(FCA::kArgsLength + 1 /*receiver*/));
  __ Str(scratch, MemOperand(sp, 4 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // v8::InvocationCallback's argument.
#if defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(!AreAliased(c0, api_function_address));
  __ add(c0, csp, Operand(1 * kSystemPointerSize));
#else // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(!AreAliased(x0, api_function_address));
  __ add(x0, sp, Operand(1 * kSystemPointerSize));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  ExternalReference thunk_ref = ExternalReference::invoke_function_callback();

  // The current frame needs to be aligned.
  DCHECK_EQ(FCA::kArgsLength % 2, 0);

  // There are two stack slots above the arguments we constructed on the stack.
  // TODO(jgruber): Document what these arguments are.
  static constexpr int kStackSlotsAboveFCA = 2;
  MemOperand return_value_operand(
      fp, (kStackSlotsAboveFCA + FCA::kReturnValueOffset) * kSystemPointerSize);

  static constexpr int kSpillOffset = 1 + kApiStackSpace;
  static constexpr int kUseStackSpaceOperand = 0;
#if defined(__CHERI_PURE_CAPABILITY__)
  MemOperand stack_space_operand(csp, 4 * kSystemPointerSize);
#else // defined(__CHERI_PURE_CAPABILITY__)
  MemOperand stack_space_operand(sp, 4 * kSystemPointerSize);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  AllowExternalCallThatCantCauseGC scope(masm);
  CallApiFunctionAndReturn(masm, api_function_address, thunk_ref,
                           kUseStackSpaceOperand, &stack_space_operand,
                           kSpillOffset, return_value_operand);
}

void Builtins::Generate_CallApiGetter(MacroAssembler* masm) {
  static_assert(PropertyCallbackArguments::kShouldThrowOnErrorIndex == 0);
  static_assert(PropertyCallbackArguments::kHolderIndex == 1);
  static_assert(PropertyCallbackArguments::kIsolateIndex == 2);
  static_assert(PropertyCallbackArguments::kReturnValueDefaultValueIndex == 3);
  static_assert(PropertyCallbackArguments::kReturnValueOffset == 4);
  static_assert(PropertyCallbackArguments::kDataIndex == 5);
  static_assert(PropertyCallbackArguments::kThisIndex == 6);
  static_assert(PropertyCallbackArguments::kArgsLength == 7);

  Register receiver = ApiGetterDescriptor::ReceiverRegister();
  Register holder = ApiGetterDescriptor::HolderRegister();
  Register callback = ApiGetterDescriptor::CallbackRegister();
#if defined(__CHERI_PURE_CAPABILITY__)
  Register data = c4;
  Register undef = c5;
  Register isolate_address = c6;
  Register name = c7;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register data = x4;
  Register undef = x5;
  Register isolate_address = x6;
  Register name = x7;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK(!AreAliased(receiver, holder, callback, data, undef, isolate_address,
                     name));

  __ LoadAnyTaggedField(data,
                        FieldMemOperand(callback, AccessorInfo::kDataOffset));
  __ LoadRoot(undef, RootIndex::kUndefinedValue);
  __ Mov(isolate_address, ExternalReference::isolate_address(masm->isolate()));
  __ LoadTaggedPointerField(
      name, FieldMemOperand(callback, AccessorInfo::kNameOffset));

  // PropertyCallbackArguments:
  //   receiver, data, return value, return value default, isolate, holder,
  //   should_throw_on_error
  // These are followed by the property name, which is also pushed below the
  // exit frame to make the GC aware of it.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(receiver, data, undef, undef, isolate_address, holder, czr, name);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(receiver, data, undef, undef, isolate_address, holder, xzr, name);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // v8::PropertyCallbackInfo::args_ array and name handle.
  static const int kStackUnwindSpace =
      PropertyCallbackArguments::kArgsLength + 1;
  static_assert(kStackUnwindSpace % 2 == 0,
                "slots must be a multiple of 2 for stack pointer alignment");

  // Load address of v8::PropertyAccessorInfo::args_ array and name handle.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c0, csp);                          // x0 = Handle<Name>
  __ Add(c1, c0, 1 * kSystemPointerSize);  // x1 = v8::PCI::args_
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x0, sp);                          // x0 = Handle<Name>
  __ Add(x1, x0, 1 * kSystemPointerSize);  // x1 = v8::PCI::args_
#endif // defined(__CHERI_PURE_CAPABILITY__)

  const int kApiStackSpace = 1;

  FrameScope frame_scope(masm, StackFrame::MANUAL);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ EnterExitFrame(false, c10, kApiStackSpace + kCallApiFunctionSpillSpace);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ EnterExitFrame(false, x10, kApiStackSpace + kCallApiFunctionSpillSpace);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Create v8::PropertyCallbackInfo object on the stack and initialize
  // it's args_ field.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Poke(c1, 1 * kSystemPointerSize);
  __ SlotAddress(c1, 1);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Poke(x1, 1 * kSystemPointerSize);
  __ SlotAddress(x1, 1);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  // x1 = v8::PropertyCallbackInfo&

  ExternalReference thunk_ref =
      ExternalReference::invoke_accessor_getter_callback();

#if defined(__CHERI_PURE_CAPABILITY__)
  Register api_function_address = c2;
  Register js_getter = c4;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register api_function_address = x2;
  Register js_getter = x4;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      js_getter, FieldMemOperand(callback, AccessorInfo::kJsGetterOffset));

  __ LoadExternalPointerField(
      api_function_address,
      FieldMemOperand(js_getter, Foreign::kForeignAddressOffset),
      kForeignForeignAddressTag);

  const int spill_offset = 1 + kApiStackSpace;
  // +3 is to skip prolog, return address and name handle.
  MemOperand return_value_operand(
      fp,
      (PropertyCallbackArguments::kReturnValueOffset + 3) * kSystemPointerSize);
  MemOperand* const kUseStackSpaceConstant = nullptr;
  CallApiFunctionAndReturn(masm, api_function_address, thunk_ref,
                           kStackUnwindSpace, kUseStackSpaceConstant,
                           spill_offset, return_value_operand);
}

void Builtins::Generate_DirectCEntry(MacroAssembler* masm) {
  // The sole purpose of DirectCEntry is for movable callers (e.g. any general
  // purpose Code object) to be able to call into C functions that may trigger
  // GC and thus move the caller.
  //
  // DirectCEntry places the return address on the stack (updated by the GC),
  // making the call GC safe. The irregexp backend relies on this.

  __ Poke<TurboAssembler::kSignLR>(lr, 0);  // Store the return address.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Blr(c10);                              // Call the C++ function.
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Blr(x10);                              // Call the C++ function.
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Peek<TurboAssembler::kAuthLR>(lr, 0);  // Return to calling code.
  __ AssertFPCRState();
  __ Ret();
}

namespace {

void CopyRegListToFrame(MacroAssembler* masm, const Register& dst,
                        int dst_offset, const CPURegList& reg_list,
                        const Register& temp0, const Register& temp1,
                        int src_offset = 0) {
  ASM_CODE_COMMENT(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(reg_list.Count() * reg_list.RegisterSizeInBits() % 16, 0);
#else // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(reg_list.Count() % 2, 0);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  UseScratchRegisterScope temps(masm);
  CPURegList copy_to_input = reg_list;
  int reg_size = reg_list.RegisterSizeInBytes();
  DCHECK_EQ(temp0.SizeInBytes(), reg_size);
  DCHECK_EQ(temp1.SizeInBytes(), reg_size);

  // Compute some temporary addresses to avoid having the macro assembler set
  // up a temp with an offset for accesses out of the range of the addressing
  // mode.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register src = temps.AcquireC();
  masm->Add(src, csp, src_offset);
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register src = temps.AcquireX();
  masm->Add(src, sp, src_offset);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  masm->Add(dst, dst, dst_offset);

  // Write reg_list into the frame pointed to by dst.
  for (int i = 0; i < reg_list.Count(); i += 2) {
    masm->Ldp(temp0, temp1, MemOperand(src, i * reg_size));

    CPURegister reg0 = copy_to_input.PopLowestIndex();
    CPURegister reg1 = copy_to_input.PopLowestIndex();
    int offset0 = reg0.code() * reg_size;
    int offset1 = reg1.code() * reg_size;

    // Pair up adjacent stores, otherwise write them separately.
    if (offset1 == offset0 + reg_size) {
      masm->Stp(temp0, temp1, MemOperand(dst, offset0));
    } else {
      masm->Str(temp0, MemOperand(dst, offset0));
      masm->Str(temp1, MemOperand(dst, offset1));
    }
  }
  masm->Sub(dst, dst, dst_offset);
}

void RestoreRegList(MacroAssembler* masm, const CPURegList& reg_list,
                    const Register& src_base, int src_offset) {
  ASM_CODE_COMMENT(masm);
#if !defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(reg_list.Count() % 2, 0);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  UseScratchRegisterScope temps(masm);
  CPURegList restore_list = reg_list;
  int reg_size = restore_list.RegisterSizeInBytes();

  // Compute a temporary addresses to avoid having the macro assembler set
  // up a temp with an offset for accesses out of the range of the addressing
  // mode.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register src = temps.AcquireC();
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register src = temps.AcquireX();
#endif // defined(__CHERI_PURE_CAPABILITY__)
  masm->Add(src, src_base, src_offset);

  // No need to restore padreg.
  restore_list.Remove(padreg);

  // Restore every register in restore_list from src.
  while (!restore_list.IsEmpty()) {
    CPURegister reg0 = restore_list.PopLowestIndex();
    CPURegister reg1 = restore_list.PopLowestIndex();
    int offset0 = reg0.code() * reg_size;

    if (reg1 == NoCPUReg) {
      masm->Ldr(reg0, MemOperand(src, offset0));
      break;
    }

    int offset1 = reg1.code() * reg_size;

    // Pair up adjacent loads, otherwise read them separately.
    if (offset1 == offset0 + reg_size) {
      masm->Ldp(reg0, reg1, MemOperand(src, offset0));
    } else {
      masm->Ldr(reg0, MemOperand(src, offset0));
      masm->Ldr(reg1, MemOperand(src, offset1));
    }
  }
}

void Generate_DeoptimizationEntry(MacroAssembler* masm,
                                  DeoptimizeKind deopt_kind) {
  Isolate* isolate = masm->isolate();

  // TODO(all): This code needs to be revisited. We probably only need to save
  // caller-saved registers here. Callee-saved registers can be stored directly
  // in the input frame.

  // Save all allocatable double registers.
  CPURegList saved_double_registers(
      kDRegSizeInBits,
      DoubleRegList::FromBits(
          RegisterConfiguration::Default()->allocatable_double_codes_mask()));
  DCHECK_EQ(saved_double_registers.Count() % 2, 0);
  __ PushCPURegList(saved_double_registers);

  // We save all the registers except sp, lr, platform register (x18) and the
  // masm scratches.
#if defined(__CHERI_PURE_CAPABILITY__)
  CPURegList saved_registers(CPURegister::kRegister, kCRegSizeInBits, 0, 28);
#else // defined(__CHERI_PURE_CAPABILITY__)
  CPURegList saved_registers(CPURegister::kRegister, kXRegSizeInBits, 0, 28);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  saved_registers.Remove(ip0);
  saved_registers.Remove(ip1);
#if defined(__CHERI_PURE_CAPABILITY__)
  saved_registers.Remove(c18);
#else // defined(__CHERI_PURE_CAPABILITY__)
  saved_registers.Remove(x18);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  saved_registers.Combine(fp);
  saved_registers.Align();
  DCHECK_EQ(saved_registers.Count() % 2, 0);
  __ PushCPURegList(saved_registers);

#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c3, Operand(ExternalReference::Create(
                 IsolateAddressId::kCEntryFPAddress, isolate)));
  __ Str(fp, MemOperand(c3));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x3, Operand(ExternalReference::Create(
                 IsolateAddressId::kCEntryFPAddress, isolate)));
  __ Str(fp, MemOperand(x3));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  const int kSavedRegistersAreaSize =
#if defined(__CHERI_PURE_CAPABILITY__)
      (saved_registers.Count() * kCRegSize) +
#else // defined(__CHERI_PURE_CAPABILITY__)
      (saved_registers.Count() * kXRegSize) +
#endif // defined(__CHERI_PURE_CAPABILITY__)
      (saved_double_registers.Count() * kDRegSize);

  // Floating point registers are saved on the stack above core registers.
  const int kDoubleRegistersOffset = saved_registers.Count() * kXRegSize;

#if defined(__CHERI_PURE_CAPABILITY__)
  Register code_object = c2;
  Register fp_to_sp = c3;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register code_object = x2;
  Register fp_to_sp = x3;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  // Get the address of the location in the code object. This is the return
  // address for lazy deoptimization.
  __ Mov(code_object, lr);
  // Compute the fp-to-sp delta.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Add(fp_to_sp, csp, kSavedRegistersAreaSize);
  __ Sub(fp_to_sp, fp, fp_to_sp);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Add(fp_to_sp, sp, kSavedRegistersAreaSize);
  __ Sub(fp_to_sp, fp, fp_to_sp);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Allocate a new deoptimizer object.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c1, MemOperand(fp, CommonFrameConstants::kContextOrFrameTypeOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x1, MemOperand(fp, CommonFrameConstants::kContextOrFrameTypeOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Ensure we can safely load from below fp.
  DCHECK_GT(kSavedRegistersAreaSize, -StandardFrameConstants::kFunctionOffset);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c0, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x0, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // If x1 is a smi, zero x0.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Tst(c1, kSmiTagMask);
  __ CzeroC(c0, eq);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Tst(x1, kSmiTagMask);
  __ CzeroX(x0, eq);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ Mov(x1, static_cast<int>(deopt_kind));
  // Following arguments are already loaded:
  //  - x2: code object address
  //  - x3: fp-to-sp delta
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(c4, ExternalReference::isolate_address(isolate));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(x4, ExternalReference::isolate_address(isolate));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  {
    // Call Deoptimizer::New().
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::new_deoptimizer_function(), 5);
  }

  // Preserve "deoptimizer" object in register x0.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register deoptimizer = c0;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register deoptimizer = x0;
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Get the input frame descriptor pointer.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c1, MemOperand(deoptimizer, Deoptimizer::input_offset()));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x1, MemOperand(deoptimizer, Deoptimizer::input_offset()));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Copy core registers into the input frame.
#if defined(__CHERI_PURE_CAPABILITY__)
  CopyRegListToFrame(masm, c1, FrameDescription::registers_offset(),
                     saved_registers, c2, c3);
#else // defined(__CHERI_PURE_CAPABILITY__)
  CopyRegListToFrame(masm, x1, FrameDescription::registers_offset(),
                     saved_registers, x2, x3);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Copy double registers to the input frame.
  CopyRegListToFrame(masm, x1, FrameDescription::double_registers_offset(),
                     saved_double_registers, x2, x3, kDoubleRegistersOffset);

  // Mark the stack as not iterable for the CPU profiler which won't be able to
  // walk the stack without the return address.
  {
    UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
    Register is_iterable = temps.AcquireC();
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register is_iterable = temps.AcquireX();
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(is_iterable, ExternalReference::stack_is_iterable_address(isolate));
    __ strb(xzr, MemOperand(is_iterable));
  }

  // Remove the saved registers from the stack.
#if defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(kSavedRegistersAreaSize % kCRegSize, 0);
  __ Drop(kSavedRegistersAreaSize / kCRegSize);
#else // defined(__CHERI_PURE_CAPABILITY__)
  DCHECK_EQ(kSavedRegistersAreaSize % kXRegSize, 0);
  __ Drop(kSavedRegistersAreaSize / kXRegSize);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Compute a pointer to the unwinding limit in register x2; that is
  // the first stack slot not part of the input frame.
  Register unwind_limit = x2;
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(unwind_limit, MemOperand(c1, FrameDescription::frame_size_offset()));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(unwind_limit, MemOperand(x1, FrameDescription::frame_size_offset()));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Add(c3, c1, FrameDescription::frame_content_offset());
  __ SlotAddress(c1, 0);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Add(x3, x1, FrameDescription::frame_content_offset());
  __ SlotAddress(x1, 0);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Lsr(unwind_limit, unwind_limit, kSystemPointerSizeLog2);
  __ Mov(x5, unwind_limit);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ CopyCapabilities(c3, c1, x5);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ CopyDoubleWords(x3, x1, x5);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  // Since {unwind_limit} is the frame size up to the parameter count, we might
  // end up with a unaligned stack pointer. This is later recovered when
  // setting the stack pointer to {caller_frame_top_offset}.
  __ Bic(unwind_limit, unwind_limit, 1);
  __ Drop(unwind_limit);

  // Compute the output frame in the deoptimizer.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(padregc, c0);  // Preserve deoptimizer object across call.
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(padreg, x0);  // Preserve deoptimizer object across call.
#endif // defined(__CHERI_PURE_CAPABILITY__)
  {
    // Call Deoptimizer::ComputeOutputFrames().
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::compute_output_frames_function(), 1);
  }
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Pop(c4, padregc);  // Restore deoptimizer object (class Deoptimizer).
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Pop(x4, padreg);  // Restore deoptimizer object (class Deoptimizer).
#endif // defined(__CHERI_PURE_CAPABILITY__)

  {
    UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = temps.AcquireC();
    __ Ldr(scratch, MemOperand(c4, Deoptimizer::caller_frame_top_offset()));
    __ Mov(csp, scratch);
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register scratch = temps.AcquireX();
    __ Ldr(scratch, MemOperand(x4, Deoptimizer::caller_frame_top_offset()));
    __ Mov(sp, scratch);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, outer_loop_header;
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldrsw(x1, MemOperand(c4, Deoptimizer::output_count_offset()));
  __ Ldr(x0, MemOperand(c4, Deoptimizer::output_offset()));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldrsw(x1, MemOperand(x4, Deoptimizer::output_count_offset()));
  __ Ldr(x0, MemOperand(x4, Deoptimizer::output_offset()));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Add(x1, x0, Operand(x1, LSL, kSystemPointerSizeLog2));
  __ B(&outer_loop_header);

  __ Bind(&outer_push_loop);
#if defined(__CHERI_PURE_CAPABILITY__)
  Register current_frame = c2;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register current_frame = x2;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  Register frame_size = x3;
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(current_frame, MemOperand(c0, kSystemPointerSize, PostIndex));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(current_frame, MemOperand(x0, kSystemPointerSize, PostIndex));
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x3, MemOperand(current_frame, FrameDescription::frame_size_offset()));
  __ Lsr(frame_size, x3, kSystemPointerSizeLog2);
  __ Claim(frame_size);

#if defined(__CHERI_PURE_CAPABILITY__)
  __ Add(c7, current_frame, FrameDescription::frame_content_offset());
  __ SlotAddress(c6, 0);
  __ CopyCapabilities(c6, c7, frame_size);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Add(x7, current_frame, FrameDescription::frame_content_offset());
  __ SlotAddress(x6, 0);
  __ CopyDoubleWords(x6, x7, frame_size);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ Bind(&outer_loop_header);
  __ Cmp(x0, x1);
  __ B(lt, &outer_push_loop);

#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c1, MemOperand(c4, Deoptimizer::input_offset()));
  RestoreRegList(masm, saved_double_registers, c1,
                 FrameDescription::double_registers_offset());
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x1, MemOperand(x4, Deoptimizer::input_offset()));
  RestoreRegList(masm, saved_double_registers, x1,
                 FrameDescription::double_registers_offset());
#endif // defined(__CHERI_PURE_CAPABILITY__)

  {
    UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
    Register is_iterable = temps.AcquireC();
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register is_iterable = temps.AcquireX();
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(is_iterable, ExternalReference::stack_is_iterable_address(isolate));
    Register one = x4;
    __ Mov(one, Operand(1));
    __ strb(one, MemOperand(is_iterable));
  }

  // TODO(all): ARM copies a lot (if not all) of the last output frame onto the
  // stack, then pops it all into registers. Here, we try to load it directly
  // into the relevant registers. Is this correct? If so, we should improve the
  // ARM code.

  // Restore registers from the last output frame.
  // Note that lr is not in the list of saved_registers and will be restored
  // later. We can use it to hold the address of last output frame while
  // reloading the other registers.
  DCHECK(!saved_registers.IncludesAliasOf(lr));
  Register last_output_frame = lr;
  __ Mov(last_output_frame, current_frame);

  RestoreRegList(masm, saved_registers, last_output_frame,
                 FrameDescription::registers_offset());

  UseScratchRegisterScope temps(masm);
#if defined(__CHERI_PURE_CAPABILITY__)
  temps.Exclude(c17);
  Register continuation = c17;
  __ Ldr(continuation, MemOperand(last_output_frame,
                                   FrameDescription::continuation_offset()));
  __ Orr(continuation, continuation, 0x1);
  __ Ldr(lr, MemOperand(last_output_frame, FrameDescription::pc_offset()));
#else // defined(__CHERI_PURE_CAPABILITY__)
  temps.Exclude(x17);
  Register continuation = x17;
  __ Ldr(continuation, MemOperand(last_output_frame,
                                  FrameDescription::continuation_offset()));
  __ Ldr(lr, MemOperand(last_output_frame, FrameDescription::pc_offset()));
#endif // defined(__CHERI_PURE_CAPABILITY__)
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  __ Autibsp();
#endif
  __ Br(continuation);
}

}  // namespace

void Builtins::Generate_DeoptimizationEntry_Eager(MacroAssembler* masm) {
  Generate_DeoptimizationEntry(masm, DeoptimizeKind::kEager);
}

void Builtins::Generate_DeoptimizationEntry_Lazy(MacroAssembler* masm) {
  Generate_DeoptimizationEntry(masm, DeoptimizeKind::kLazy);
}

namespace {

// Restarts execution either at the current or next (in execution order)
// bytecode. If there is baseline code on the shared function info, converts an
// interpreter frame into a baseline frame and continues execution in baseline
// code. Otherwise execution continues with bytecode.
void Generate_BaselineOrInterpreterEntry(MacroAssembler* masm,
                                         bool next_bytecode,
                                         bool is_osr = false) {
  Label start;
  __ bind(&start);

  // Get function from the frame.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register closure = c1;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register closure = x1;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(closure, MemOperand(fp, StandardFrameConstants::kFunctionOffset));

  // Get the Code object from the shared function info.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register code_obj = c22;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register code_obj = x22;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      code_obj,
      FieldMemOperand(closure, JSFunction::kSharedFunctionInfoOffset));
  __ LoadTaggedPointerField(
      code_obj,
      FieldMemOperand(code_obj, SharedFunctionInfo::kFunctionDataOffset));

  // Check if we have baseline code. For OSR entry it is safe to assume we
  // always have baseline code.
  if (!is_osr) {
    Label start_with_baseline;
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CompareObjectType(code_obj, c3, x3, CODET_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CompareObjectType(code_obj, x3, x3, CODET_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ B(eq, &start_with_baseline);

    // Start with bytecode as there is no baseline code.
    Builtin builtin_id = next_bytecode
                             ? Builtin::kInterpreterEnterAtNextBytecode
                             : Builtin::kInterpreterEnterAtBytecode;
    __ Jump(masm->isolate()->builtins()->code_handle(builtin_id),
            RelocInfo::CODE_TARGET);

    // Start with baseline code.
    __ bind(&start_with_baseline);
  } else if (FLAG_debug_code) {
#if defined(__CHERI_PURE_CAPABILITY__)
    __ CompareObjectType(code_obj, c3, x3, CODET_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ CompareObjectType(code_obj, x3, x3, CODET_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Assert(eq, AbortReason::kExpectedBaselineData);
  }

  if (FLAG_debug_code) {
    AssertCodeTIsBaseline(masm, code_obj, x3);
  }
  if (V8_EXTERNAL_CODE_SPACE_BOOL) {
    __ LoadCodeDataContainerCodeNonBuiltin(code_obj, code_obj);
  }

  // Load the feedback vector.
#if defined(__CHERI_PURE_CAPABILITY__)
  Register feedback_vector = c2;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register feedback_vector = x2;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ LoadTaggedPointerField(
      feedback_vector,
      FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedPointerField(
      feedback_vector, FieldMemOperand(feedback_vector, Cell::kValueOffset));

  Label install_baseline_code;
  // Check if feedback vector is valid. If not, call prepare for baseline to
  // allocate it.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ CompareObjectType(feedback_vector, c3, x3, FEEDBACK_VECTOR_TYPE);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ CompareObjectType(feedback_vector, x3, x3, FEEDBACK_VECTOR_TYPE);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ B(ne, &install_baseline_code);

  // Save BytecodeOffset from the stack frame.
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  // Replace BytecodeOffset with the feedback vector.
  __ Str(feedback_vector,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  feedback_vector = no_reg;

  // Compute baseline pc for bytecode offset.
  ExternalReference get_baseline_pc_extref;
  if (next_bytecode || is_osr) {
    get_baseline_pc_extref =
        ExternalReference::baseline_pc_for_next_executed_bytecode();
  } else {
    get_baseline_pc_extref =
        ExternalReference::baseline_pc_for_bytecode_offset();
  }
#if defined(__CHERI_PURE_CAPABILITY__)
  Register get_baseline_pc = c3;
#else // defined(__CHERI_PURE_CAPABILITY__)
  Register get_baseline_pc = x3;
#endif // defined(__CHERI_PURE_CAPABILITY__)
  __ Mov(get_baseline_pc, get_baseline_pc_extref);

  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset.
  // TODO(pthier): Investigate if it is feasible to handle this special case
  // in TurboFan instead of here.
  Label valid_bytecode_offset, function_entry_bytecode;
  if (!is_osr) {
    __ cmp(kInterpreterBytecodeOffsetRegister,
           Operand(BytecodeArray::kHeaderSize - kHeapObjectTag +
                   kFunctionEntryBytecodeOffset));
    __ B(eq, &function_entry_bytecode);
  }

  __ Sub(kInterpreterBytecodeOffsetRegister, kInterpreterBytecodeOffsetRegister,
         (BytecodeArray::kHeaderSize - kHeapObjectTag));

  __ bind(&valid_bytecode_offset);
  // Get bytecode array from the stack frame.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(kInterpreterBytecodeArrayRegister,
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ ldr(kInterpreterBytecodeArrayRegister,
#endif // defined(__CHERI_PURE_CAPABILITY__)
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  // Save the accumulator register, since it's clobbered by the below call.
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Push(padregc, kInterpreterAccumulatorRegister);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Push(padreg, kInterpreterAccumulatorRegister);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  {
#if defined(__CHERI_PURE_CAPABILITY__)
    Register arg_reg_1 = c0;
    Register arg_reg_2 = x1;
    Register arg_reg_3 = c2;
#else // defined(__CHERI_PURE_CAPABILITY__)
    Register arg_reg_1 = x0;
    Register arg_reg_2 = x1;
    Register arg_reg_3 = x2;
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ Mov(arg_reg_1, code_obj);
    __ Mov(arg_reg_2, kInterpreterBytecodeOffsetRegister);
    __ Mov(arg_reg_3, kInterpreterBytecodeArrayRegister);
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallCFunction(get_baseline_pc, 3, 0);
  }
  __ Add(code_obj, code_obj, kReturnRegister0);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ Pop(kInterpreterAccumulatorRegister, padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Pop(kInterpreterAccumulatorRegister, padreg);
#endif // defined(__CHERI_PURE_CAPABILITY__)

  if (is_osr) {
    ResetBytecodeAge(masm, kInterpreterBytecodeArrayRegister);
    Generate_OSREntry(masm, code_obj, Code::kHeaderSize - kHeapObjectTag);
  } else {
    __ Add(code_obj, code_obj, Code::kHeaderSize - kHeapObjectTag);
    __ Jump(code_obj);
  }
  __ Trap();  // Unreachable.

  if (!is_osr) {
    __ bind(&function_entry_bytecode);
    // If the bytecode offset is kFunctionEntryOffset, get the start address of
    // the first bytecode.
    __ Mov(kInterpreterBytecodeOffsetRegister, Operand(0));
    if (next_bytecode) {
      __ Mov(get_baseline_pc,
             ExternalReference::baseline_pc_for_bytecode_offset());
    }
    __ B(&valid_bytecode_offset);
  }

  __ bind(&install_baseline_code);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Push(padregc, kInterpreterAccumulatorRegister);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Push(padreg, kInterpreterAccumulatorRegister);
#endif // defined(__CHERI_PURE_CAPABILITY__)
    __ PushArgument(closure);
    __ CallRuntime(Runtime::kInstallBaselineCode, 1);
#if defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(kInterpreterAccumulatorRegister, padregc);
#else // defined(__CHERI_PURE_CAPABILITY__)
    __ Pop(kInterpreterAccumulatorRegister, padreg);
#endif // defined(__CHERI_PURE_CAPABILITY__)
  }
  // Retry from the start after installing baseline code.
  __ B(&start);
}

}  // namespace

void Builtins::Generate_BaselineOrInterpreterEnterAtBytecode(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, false);
}

void Builtins::Generate_BaselineOrInterpreterEnterAtNextBytecode(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, true);
}

void Builtins::Generate_InterpreterOnStackReplacement_ToBaseline(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, false, true);
}

void Builtins::Generate_RestartFrameTrampoline(MacroAssembler* masm) {
  // Frame is being dropped:
  // - Look up current function on the frame.
  // - Leave the frame.
  // - Restart the frame by calling the function.

#if defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(c1, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ ldr(c0, MemOperand(fp, StandardFrameConstants::kArgCOffset));
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ Ldr(x1, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ ldr(x0, MemOperand(fp, StandardFrameConstants::kArgCOffset));
#endif // defined(__CHERI_PURE_CAPABILITY__)

  __ LeaveFrame(StackFrame::INTERPRETED);

  // The arguments are already in the stack (including any necessary padding),
  // we should not try to massage the arguments again.
  __ Mov(x2, kDontAdaptArgumentsSentinel);
#if defined(__CHERI_PURE_CAPABILITY__)
  __ InvokeFunction(c1, x2, x0, InvokeType::kJump);
#else // defined(__CHERI_PURE_CAPABILITY__)
  __ InvokeFunction(x1, x2, x0, InvokeType::kJump);
#endif // defined(__CHERI_PURE_CAPABILITY__)
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM

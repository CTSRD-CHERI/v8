// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Push all callee-saved registers to get them on the stack for conservative
// stack scanning.
//
// See asm/x64/push_registers_clang.cc for why the function is not generated
// using clang.
//
// Do not depend on V8_TARGET_OS_* defines as some embedders may override the
// GN toolchain (e.g. ChromeOS) and not provide them.

// We maintain 16-byte alignment.
//
// Calling convention source:
// https://en.wikipedia.org/wiki/Calling_convention#ARM_(A64)

asm(
#if defined(__APPLE__)
    ".globl _PushAllRegistersAndIterateStack            \n"
    ".private_extern _PushAllRegistersAndIterateStack   \n"
    ".p2align 2                                         \n"
    "_PushAllRegistersAndIterateStack:                  \n"
#else  // !defined(__APPLE__)
    ".globl PushAllRegistersAndIterateStack             \n"
#if !defined(_WIN64)
    ".type PushAllRegistersAndIterateStack, %function   \n"
    ".hidden PushAllRegistersAndIterateStack            \n"
#endif  // !defined(_WIN64)
    ".p2align 2                                         \n"
    "PushAllRegistersAndIterateStack:                   \n"
#endif  // !defined(__APPLE__)
#if defined(__CHERI_PURE_CAPABILITY__)
    // c19-c29 are callee-saved.
    "  stp c19, c20, [csp, #-32]!                        \n"
    "  stp c21, c22, [csp, #-32]!                        \n"
    "  stp c23, c24, [csp, #-32]!                        \n"
    "  stp c25, c26, [csp, #-32]!                        \n"
    "  stp c27, c28, [csp, #-32]!                        \n"
    "  stp cfp, clr, [csp, #-32]!                        \n"
    // Maintain frame pointer.
    "  mov cfp, csp                                      \n"
    // Pass 1st parameter (x0) unchanged (Stack*).
    // Pass 2nd parameter (x1) unchanged (StackVisitor*).
    // Save 3rd parameter (x2; IterateStackCallback)
    "  mov c7, c2                                       \n"
    // Pass 3rd parameter as sp (stack pointer).
    "  mov c2, csp                                       \n"
    "  blr c7                                           \n"
    // Load return address and frame pointer.
    "  ldp cfp, clr, [csp], #32                        \n"
    // Drop all callee-saved registers.
    "  add csp, csp, #160                               \n"
#else    // !__CHERI_PURE_CAPABILITY__
    // x19-x29 are callee-saved.
    "  stp x19, x20, [sp, #-16]!                        \n"
    "  stp x21, x22, [sp, #-16]!                        \n"
    "  stp x23, x24, [sp, #-16]!                        \n"
    "  stp x25, x26, [sp, #-16]!                        \n"
    "  stp x27, x28, [sp, #-16]!                        \n"
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
    // Sign return address.
    "  paciasp                                          \n"
#endif
    "  stp fp, lr,   [sp, #-16]!                        \n"
    // Maintain frame pointer.
    "  mov fp, sp                                       \n"
    // Pass 1st parameter (x0) unchanged (Stack*).
    // Pass 2nd parameter (x1) unchanged (StackVisitor*).
    // Save 3rd parameter (x2; IterateStackCallback)
    "  mov x7, x2                                       \n"
    // Pass 3rd parameter as sp (stack pointer).
    "  mov x2, sp                                       \n"
    "  blr x7                                           \n"
    // Load return address and frame pointer.
    "  ldp fp, lr, [sp], #16                            \n"
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
    // Authenticate return address.
    "  autiasp                                          \n"
#endif
    // Drop all callee-saved registers.
    "  add sp, sp, #80                                  \n"
#endif    // !__CHERI_PURE_CAPABILITY__
    "  ret                                              \n");

#ifdef __CHERI_PURE_CAPABILITY__
asm(".globl cheritree_print                            \n"
    ".type cheritree_print, %function                  \n"
    ".hidden cheritree_print                           \n"
    ".p2align 2                                        \n"
    "cheritree_print:                                  \n"
    "msr cid_el0, c0                                   \n"
    "adrp c0, :got:cheritree_regs_ptr                  \n"
    "ldr c0, [c0, :got_lo12:cheritree_regs_ptr]        \n"
    "ldr c0, [c0]                                      \n"
    "str c1, [c0, #16]                                 \n"
    "mrs c1, cid_el0                                   \n"
    "msr cid_el0, czr                                  \n"
    "str c1, [c0, #0]                                  \n"
    "stp c2, c3, [c0, #32]                             \n"
    "stp c4, c5, [c0, #64]                             \n"
    "stp c6, c7, [c0, #96]                             \n"
    "stp c8, c9, [c0, #128]                            \n"
    "stp c10, c11, [c0, #160]                          \n"
    "stp c12, c13, [c0, #192]                          \n"
    "stp c14, c15, [c0, #224]                          \n"
    "stp c16, c17, [c0, #256]                          \n"
    "stp c18, c19, [c0, #288]                          \n"
    "stp c20, c21, [c0, #320]                          \n"
    "stp c22, c23, [c0, #352]                          \n"
    "stp c24, c25, [c0, #384]                          \n"
    "stp c26, c27, [c0, #416]                          \n"
    "stp c28, c29, [c0, #448]                          \n"
    "str c30, [c0, #480]                               \n"
    "adr c1, 0                                         \n"
    "str c1, [c0, #496]                                \n"
    "mov c1, csp                                       \n"
    "str c1, [c0, #512]                                \n"
    "mrs c1, ctpidr_el0                                \n"
    "str c1, [c0, #528]                                \n"
    "ldp c0, c1, [c0, #0]                              \n"
    "adrp c29, :got:_cheritree_print_fptr              \n"
    "ldr c29, [c29, :got_lo12:_cheritree_print_fptr]   \n"
    "ldr c29, [c29, #0]                                \n"
    "br c29                                            \n");
#endif

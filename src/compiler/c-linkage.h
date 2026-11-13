// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_C_LINKAGE_H_
#define V8_COMPILER_C_LINKAGE_H_

#include "src/codegen/machine-type.h"

namespace v8 {
namespace internal {
namespace compiler {

#if V8_TARGET_CHERI && V8_TARGET_ARCH_ARM64
// AAPCS64-cap memory arguments use byte offsets. C-call caller-frame slots are
// encoded in 8-byte units, with 16-byte alignment holes where required.
constexpr int kAbiSlotSize = 8;
constexpr int kCapabilitySize = 16;
constexpr int kScalarSize = 8;
constexpr int kMinArgSize = 8;

// BuildParameterLocations rejects unsupported argument representations.
constexpr bool AAPCS64CapArgumentIsCapabilitySized(MachineType type) {
  MachineRepresentation rep = type.representation();
  return IsCapability(rep) || rep == MachineRepresentation::kTaggedSigned;
}

constexpr int AAPCS64CapArgumentSizeInBytes(MachineType type) {
  if (AAPCS64CapArgumentIsCapabilitySized(type)) return kCapabilitySize;
  MachineRepresentation rep = type.representation();
  if (IsFloatingPoint(rep)) return kScalarSize;
  int size = ElementSizeInBytes(rep);
  return size < kMinArgSize ? kMinArgSize : size;
}

constexpr int AAPCS64CapArgumentAlignmentInBytes(MachineType type) {
  return AAPCS64CapArgumentIsCapabilitySized(type) ? kCapabilitySize
                                                   : kScalarSize;
}
#endif

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_C_LINKAGE_H_

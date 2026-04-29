// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_INL_H_
#define V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_INL_H_

#include "src/snapshot/embedded/embedded-data.h"

namespace v8 {
namespace internal {

Address EmbeddedData::InstructionStartOf(Builtin builtin) const {
  DCHECK(Builtins::IsBuiltinId(builtin));
  const struct LayoutDescription& desc = LayoutDescription(builtin);
  const uint8_t* raw_code = RawCode();
#if defined(__CHERI_PURE_CAPABILITY__) && defined(__aarch64__)
  const uint8_t* result = reinterpret_cast<const uint8_t*>(
      V8_CHERI_SEALED(code_)
          ? V8_CHERI_TO_SENTRY(
                reinterpret_cast<Address>(raw_code + desc.instruction_offset) |
                1)
          : reinterpret_cast<Address>(raw_code + desc.instruction_offset));
#else
  const uint8_t* result =
      V8_CHERI_SEALED(code_)
          ? V8_CHERI_TO_SENTRY(raw_code + desc.instruction_offset)
          : raw_code + desc.instruction_offset;
#endif
  DCHECK_IMPLIES(V8_CHERI_PURECAP_BOOL, V8_CHERI_TAG_GET(result));
  DCHECK_LT(result, code_ + code_size_);
  DCHECK_LT(result, code_nonsentry_ + code_size_);
  return reinterpret_cast<Address>(result);
}

Address EmbeddedData::InstructionEndOf(Builtin builtin) const {
  DCHECK(Builtins::IsBuiltinId(builtin));
  const struct LayoutDescription& desc = LayoutDescription(builtin);
  const uint8_t* result =
      RawCode() + desc.instruction_offset + desc.instruction_length;
  DCHECK_LT(result, code_ + code_size_);
  return reinterpret_cast<Address>(result);
}

uint32_t EmbeddedData::InstructionSizeOf(Builtin builtin) const {
  DCHECK(Builtins::IsBuiltinId(builtin));
  const struct LayoutDescription& desc = LayoutDescription(builtin);
  return desc.instruction_length;
}

Address EmbeddedData::MetadataStartOf(Builtin builtin) const {
  DCHECK(Builtins::IsBuiltinId(builtin));
  const struct LayoutDescription& desc = LayoutDescription(builtin);
  const uint8_t* result = RawMetadata() + desc.metadata_offset;
  DCHECK_LE(desc.metadata_offset, data_size_);
  return reinterpret_cast<Address>(result);
}

Address EmbeddedData::InstructionStartOfBytecodeHandlers() const {
  return InstructionStartOf(Builtin::kFirstBytecodeHandler);
}

Address EmbeddedData::InstructionEndOfBytecodeHandlers() const {
  static_assert(Builtins::kBytecodeHandlersAreSortedLast);
  // Note this also includes trailing padding, but that's fine for our purposes.
  return reinterpret_cast<Address>(code_ + code_size_);
}

uint32_t EmbeddedData::PaddedInstructionSizeOf(Builtin builtin) const {
  uint32_t size = InstructionSizeOf(builtin);
  CHECK_NE(size, 0);
  return PadAndAlignCode(size);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_INL_H_

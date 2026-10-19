// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/object-type.h"

#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/objects/string-inl.h"

namespace v8 {
namespace internal {

#if defined(DEBUG) && defined(__CHERI_PURE_CAPABILITY__)
// Print() follows the map word, which faults on an untagged value.
static void PrintUntaggedValue(Address value,
                               std::stringstream& value_description) {
  const ptraddr_t addr = V8_CHERI_ADDR_GET(value);
  const ptraddr_t base = V8_CHERI_BASE_GET(value);
  const uint64_t len = V8_CHERI_LENGTH_GET(value);
  const uint64_t perms = V8_CHERI_PERMS(value);
  uint64_t halves[2] = {0, 0};
  std::memcpy(halves, &value, sizeof(halves));
  value_description << "<untagged raw_value" << std::hex << " addr=0x" << addr
                    << " lo=0x" << halves[0] << " hi=0x" << halves[1]
                    << " base=0x" << base << " len=0x" << len << " perms=0x"
                    << perms << " sealed=" << V8_CHERI_SEALED(value) << std::dec
                    << ">";
}
#endif  // defined(DEBUG) && defined(__CHERI_PURE_CAPABILITY__)

Address CheckObjectType(Address raw_value, Address raw_type,
                        Address raw_location) {
#ifdef DEBUG
  ObjectType type = static_cast<ObjectType>(Tagged<Smi>(raw_type).value());
  Tagged<String> location = Cast<String>(Tagged<Object>(raw_location));
  const char* expected = "Undefined (SWITCH CASE MISSING)";

  if (HAS_WEAK_HEAP_OBJECT_TAG(raw_value)) {
    if (type == ObjectType::kHeapObjectReference) return Smi::FromInt(0).ptr();
    // Casts of weak references are not allowed, one should use
    // GetHeapObjectIfStrong / GetHeapObjectAssumeWeak first.
    switch (type) {
#define TYPE_CASE(Name)     \
  case ObjectType::k##Name: \
    expected = #Name;       \
    break;
#define TYPE_STRUCT_CASE(NAME, Name, name) \
  case ObjectType::k##Name:                \
    expected = #Name;                      \
    break;

      TYPE_CASE(Object)
      TYPE_CASE(Smi)
      TYPE_CASE(TaggedIndex)
      TYPE_CASE(HeapObject)
      TYPE_CASE(HeapObjectReference)
      OBJECT_TYPE_LIST(TYPE_CASE)
      HEAP_OBJECT_TYPE_LIST(TYPE_CASE)
      STRUCT_LIST(TYPE_STRUCT_CASE)
#undef TYPE_CASE
#undef TYPE_STRUCT_CASE
    }
  } else {
    Tagged<Object> value(raw_value);
    switch (type) {
      case ObjectType::kHeapObjectReference:
        if (!IsSmi(value)) return Smi::FromInt(0).ptr();
        expected = "HeapObjectReference";
        break;
      case ObjectType::kObject:
        return Smi::FromInt(0).ptr();
#define TYPE_CASE(Name)                                \
  case ObjectType::k##Name:                            \
    if (Is##Name(value)) return Smi::FromInt(0).ptr(); \
    expected = #Name;                                  \
    break;
#define TYPE_STRUCT_CASE(NAME, Name, name)             \
  case ObjectType::k##Name:                            \
    if (Is##Name(value)) return Smi::FromInt(0).ptr(); \
    expected = #Name;                                  \
    break;

          TYPE_CASE(Smi)
          TYPE_CASE(TaggedIndex)
          TYPE_CASE(HeapObject)
          OBJECT_TYPE_LIST(TYPE_CASE)
          HEAP_OBJECT_TYPE_LIST(TYPE_CASE)
          STRUCT_LIST(TYPE_STRUCT_CASE)
#undef TYPE_CASE
#undef TYPE_STRUCT_CASE
    }
  }
  Tagged<MaybeObject> maybe_value(raw_value);
  std::stringstream value_description;
#ifdef __CHERI_PURE_CAPABILITY__
  if (maybe_value.IsSmi() || V8_CHERI_TAG_GET(maybe_value.ptr())) {
    Print(maybe_value, value_description);
  } else {
    PrintUntaggedValue(maybe_value.ptr(), value_description);
  }
#else
  Print(maybe_value, value_description);
#endif  // __CHERI_PURE_CAPABILITY__
  FATAL(
      "Type cast failed in %s\n"
      "  Expected %s but found %s",
      location->ToAsciiArray(), expected, value_description.str().c_str());
#else
  UNREACHABLE();
#endif
}

}  // namespace internal
}  // namespace v8

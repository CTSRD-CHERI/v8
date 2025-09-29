#ifndef V8_COMMON_CHERI_H_
#define V8_COMMON_CHERI_H_

#include "src/common/globals.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {
extern thread_local bool t_cheri_madvise;
V8_EXPORT_PRIVATE bool CheriShouldMadvise();

struct V8_EXPORT_PRIVATE CheriMadviseScope {
  CheriMadviseScope(bool condition);
  ~CheriMadviseScope();

  CheriMadviseScope() = delete;
  CheriMadviseScope(const CheriMadviseScope& other) = delete;
  CheriMadviseScope& operator=(const CheriMadviseScope& other) = delete;
};
}  // namespace base
}  // namespace v8

#endif  // V8_COMMON_CHERI_H_

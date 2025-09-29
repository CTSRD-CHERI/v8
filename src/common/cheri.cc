#include "src/common/cheri.h"
#include <stdlib.h>

namespace v8 {
namespace base {
thread_local bool t_cheri_madvise = false;

bool CheriShouldMadvise() { return t_cheri_madvise; }

CheriMadviseScope::CheriMadviseScope(bool condition) {
  if (getenv("APPROXIMATELY_MADVISE_JS_HEAP")) t_cheri_madvise = condition;
}
CheriMadviseScope::~CheriMadviseScope() { t_cheri_madvise = false; }

}  // namespace base
}  // namespace v8

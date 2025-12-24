// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for FreeBSD goes here. For the POSIX-compatible
// parts, the implementation is in platform-posix.cc.

#include <pthread.h>
#include <pthread_np.h>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <sys/user.h>

#include <sys/fcntl.h>  // open
#include <sys/mman.h>   // mmap & munmap
#include <sys/stat.h>   // open
#include <sys/sysctl.h>
#include <unistd.h>     // getpagesize
// If you don't have execinfo.h then you need devel/libexecinfo from ports.
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <strings.h>    // index

#include <cmath>

#undef MAP_TYPE

#include <link.h>

#include "src/base/macros.h"
#include "src/base/platform/platform-posix-time.h"
#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace base {

// #ifdef __CHERI_PURE_CAPABILITY__
#if 0
struct OS::C18n::TrustedFrameState::TrustedFrameStateImpl {
  TrustedFrameStateImpl() = default;
  ~TrustedFrameStateImpl() = default;

  TrustedFrameStateImpl(const TrustedFrameStateImpl& other) = default;
  TrustedFrameStateImpl& operator=(const TrustedFrameStateImpl& other) =
      default;

  TrustedFrameStateImpl(TrustedFrameStateImpl&& other) noexcept = default;
  TrustedFrameStateImpl& operator=(TrustedFrameStateImpl&& other) noexcept =
      default;

  dl_c18n_compart_state state{};
};

OS::C18n::TrustedFrameState::TrustedFrameState()
    : impl_(new TrustedFrameStateImpl{}) {}

OS::C18n::TrustedFrameState::~TrustedFrameState() = default;

void* OS::C18n::GetTrustedStack(void* pc) {
  return dl_c18n_get_trusted_stack(reinterpret_cast<uintptr_t>(pc));
}

void* OS::C18n::GetNextTrustedFrame(
    OS::C18n::TrustedFrameState& trusted_frame_state, void* trusted_frame) {
  trusted_frame = dl_c18n_pop_trusted_stack(&trusted_frame_state.impl_->state,
                                            trusted_frame);
  return trusted_frame;
}

size_t OS::C18n::TrustedFrameState::NumRegisters() const {
  return nitems(impl_->state.regs);
}
const void* const* OS::C18n::TrustedFrameState::Registers() const {
  return reinterpret_cast<const void* const*>(impl_->state.regs);
}
const void* OS::C18n::TrustedFrameState::StackStart() const {
  return impl_->state.osp;
}
const void* OS::C18n::TrustedFrameState::StackTop() const {
  return impl_->state.sp;
}
bool OS::C18n::ShouldIterateStack(const void* top, const void* start) {
  DCHECK(V8_CHERI_TAG_GET(top));
  DCHECK(V8_CHERI_TAG_GET(start));
  ptrdiff_t stack_size =
      reinterpret_cast<ptraddr_t>(start) - reinterpret_cast<ptraddr_t>(top);
  DCHECK_GE(stack_size, 0);
  return stack_size >= sizeof(void*);
}
bool OS::C18n::Enabled() { return dl_c18n_get_trusted_stack(0) != nullptr; }
#else   // !__CHERI_PURE_CAPABILITY__
// Shims
struct OS::C18n::TrustedFrameState::TrustedFrameStateImpl {};
OS::C18n::TrustedFrameState::TrustedFrameState() {}
OS::C18n::TrustedFrameState::~TrustedFrameState() = default;
void* OS::C18n::GetTrustedStack(void* pc) { return nullptr; }
void* OS::C18n::GetNextTrustedFrame(
    OS::C18n::TrustedFrameState& trusted_frame_state, void* trusted_frame) {
  return nullptr;
}
size_t OS::C18n::TrustedFrameState::NumRegisters() const { return 0; }
const void* const* OS::C18n::TrustedFrameState::Registers() const {
  return nullptr;
}
const void* OS::C18n::TrustedFrameState::StackStart() const { return nullptr; }
const void* OS::C18n::TrustedFrameState::StackTop() const { return nullptr; }
bool OS::C18n::ShouldIterateStack(const void* top, const void* start) {
  return true;
}
bool OS::C18n::Enabled() { return false; }
#endif  // __CHERI_PURE_CAPABILITY__

TimezoneCache* OS::CreateTimezoneCache() {
  return new PosixDefaultTimezoneCache();
}

static unsigned StringToLong(char* buffer) {
  return static_cast<unsigned>(strtol(buffer, nullptr, 16));
}

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  std::vector<SharedLibraryAddress> result;
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, getpid()};
  unsigned int miblen = sizeof(mib) / sizeof(mib[0]);
  size_t buffer_size;
  if (sysctl(mib, miblen, nullptr, &buffer_size, nullptr, 0) == 0) {
    // Overallocate the buffer by 1/3 to account for concurrent
    // kinfo_vmentry change. 1/3 is an arbitrary constant that
    // works in practice.
    buffer_size = buffer_size * 4 / 3;
    std::vector<char> buffer(buffer_size);
    int ret = sysctl(mib, miblen, buffer.data(), &buffer_size, nullptr, 0);

    if (ret == 0 || (ret == -1 && errno == ENOMEM)) {
      char* start = buffer.data();
      char* end = start + buffer_size;

      while (start < end) {
        struct kinfo_vmentry* map =
            reinterpret_cast<struct kinfo_vmentry*>(start);
        const size_t ssize = map->kve_structsize;
        char* path = map->kve_path;

        CHECK_NE(0, ssize);

        if ((map->kve_protection & KVME_PROT_READ) != 0 &&
            (map->kve_protection & KVME_PROT_EXEC) != 0 && path[0] != '\0') {
          char* sep = strrchr(path, '/');
          std::string lib_name;
          if (sep != nullptr) {
            lib_name = std::string(++sep);
          } else {
            lib_name = std::string(path);
          }
          result.push_back(SharedLibraryAddress(
              lib_name, static_cast<uintptr_t>(map->kve_start),
              static_cast<uintptr_t>(map->kve_end)));
        }

        start += ssize;
      }
    }
  }
  return result;
}

void OS::SignalCodeMovingGC() {}

void OS::AdjustSchedulingParams() {}

std::vector<OS::MemoryRange> OS::GetFreeMemoryRangesWithin(
    OS::Address boundary_start, OS::Address boundary_end, size_t minimum_size,
    size_t alignment) {
  return {};
}

// static
Stack::StackSlot Stack::ObtainCurrentThreadStackStart() {
  pthread_attr_t attr;
  int error;
  pthread_attr_init(&attr);
  error = pthread_attr_get_np(pthread_self(), &attr);
  if (!error) {
    void* base;
    size_t size;
    error = pthread_attr_getstack(&attr, &base, &size);
    CHECK(!error);
    pthread_attr_destroy(&attr);
    return reinterpret_cast<uint8_t*>(base) + size;
  }
  pthread_attr_destroy(&attr);
  return nullptr;
}

}  // namespace base
}  // namespace v8

// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bounded-page-allocator.h"

#if defined(__CHERI_PURE_CAPABILITY__) && V8_OS_FREEBSD
#include <cheri/cherireg.h>  // OS-specific capability permission bits
#endif

namespace v8 {
namespace base {

BoundedPageAllocator::BoundedPageAllocator(
    v8::PageAllocator* page_allocator, Address start, size_t size,
    size_t allocate_page_size, PageInitializationMode page_initialization_mode,
    PageFreeingMode page_freeing_mode, bool restrict_pages)
    : allocate_page_size_(allocate_page_size),
      commit_page_size_(page_allocator->CommitPageSize()),
      page_allocator_(page_allocator),
      region_allocator_(start, size, allocate_page_size_),
      page_initialization_mode_(page_initialization_mode),
      page_freeing_mode_(page_freeing_mode),
      restrict_pages_(restrict_pages) {
  DCHECK(!restrict_pages_ || V8_CHERI_BOUND_PAGES_BOOL);
  DCHECK_NOT_NULL(page_allocator);
  DCHECK(IsAligned(allocate_page_size, page_allocator->AllocatePageSize()));
  DCHECK(IsAligned(allocate_page_size_, commit_page_size_));
}

BoundedPageAllocator::Address BoundedPageAllocator::begin() const {
  return region_allocator_.begin();
}

size_t BoundedPageAllocator::size() const { return region_allocator_.size(); }

void* BoundedPageAllocator::AllocatePages(
    void* hint, size_t size, size_t alignment, PageAllocator::Permission access,
    PageAllocator::Permission max_access) {
  return AllocatePages(size, alignment, access, max_access,
                       v8::PageAllocator::AllocationHint().WithAddress(hint));
}

void* BoundedPageAllocator::AllocatePages(size_t size, size_t alignment,
                                          PageAllocator::Permission access,
                                          PageAllocator::Permission max_access,
                                          PageAllocator::AllocationHint hint) {
  MutexGuard guard(&mutex_);
  DCHECK(IsAligned(alignment, region_allocator_.page_size()));
  DCHECK(IsAligned(alignment, allocate_page_size_));

  Address address = RegionAllocator::kAllocationFailure;

  Address hint_address = reinterpret_cast<Address>(hint.Address());
  if (hint_address && IsAligned(hint_address, alignment) &&
      region_allocator_.contains(hint_address, size)) {
    if (region_allocator_.AllocateRegionAt(hint_address, size)) {
      address = hint_address;
    }
  }

  const RegionAllocator::AllocationStrategy allocation_strategy =
      hint.MayGrow() ? RegionAllocator::AllocationStrategy::kLargestFit
                     : RegionAllocator::AllocationStrategy::kFirstFit;

  if (address == RegionAllocator::kAllocationFailure) {
    if (alignment <= allocate_page_size_) {
      // TODO(ishell): Consider using randomized version here.
      address = region_allocator_.AllocateRegion(size, allocation_strategy);
    } else {
      address = region_allocator_.AllocateAlignedRegion(size, alignment);
    }
  }

  if (address == RegionAllocator::kAllocationFailure) {
    allocation_status_ = AllocationStatus::kRanOutOfReservation;
    return nullptr;
  }

  address = WithReservationProvenance(address, size);
  void* ptr = reinterpret_cast<void*>(address);
  void* client_ptr = ptr;
#if defined(__CHERI_PURE_CAPABILITY__) && V8_OS_FREEBSD
  if (V8_CHERI_BOUND_PAGES_BOOL && restrict_pages_) {
    // The returned page pointer shouldn't have SW_VMEM
    client_ptr = __builtin_cheri_perms_and(
        ptr, ~static_cast<size_t>(CHERI_PERM_SW_VMEM));
  }
#endif
  // It's assumed that free regions are in kNoAccess/kNoAccessWillJitLater
  // state.
  if (access == PageAllocator::kNoAccess ||
      access == PageAllocator::kNoAccessWillJitLater) {
    allocation_status_ = AllocationStatus::kSuccess;
    return client_ptr;
  }

  if (page_initialization_mode_ == PageInitializationMode::kRecommitOnly) {
    if (page_allocator_->RecommitPages(ptr, size, access, max_access)) {
      allocation_status_ = AllocationStatus::kSuccess;
      return client_ptr;
    }
  } else {
    if (page_allocator_->SetPermissions(ptr, size, access, max_access)) {
      allocation_status_ = AllocationStatus::kSuccess;
      return client_ptr;
    }
  }

  // This most likely means that we ran out of memory.
  CHECK_EQ(region_allocator_.FreeRegion(address), size);
  allocation_status_ = AllocationStatus::kFailedToCommit;
  return nullptr;
}

bool BoundedPageAllocator::AllocatePagesAt(
    Address address, size_t size, PageAllocator::Permission access,
    PageAllocator::Permission max_access) {
  MutexGuard guard(&mutex_);

  DCHECK(IsAligned(address, allocate_page_size_));
  DCHECK(IsAligned(size, allocate_page_size_));

  DCHECK(region_allocator_.contains(address, size));

  if (!region_allocator_.AllocateRegionAt(address, size)) {
    allocation_status_ = AllocationStatus::kHintedAddressTakenOrNotFound;
    return false;
  }

  void* ptr =
      reinterpret_cast<void*>(WithReservationProvenance(address, size));
  if (!page_allocator_->SetPermissions(ptr, size, access, max_access)) {
    // This most likely means that we ran out of memory.
    CHECK_EQ(region_allocator_.FreeRegion(address), size);
    allocation_status_ = AllocationStatus::kFailedToCommit;
    return false;
  }

  allocation_status_ = AllocationStatus::kSuccess;
  return true;
}

bool BoundedPageAllocator::ResizeAllocationAt(
    void* address, size_t old_size, size_t new_size,
    PageAllocator::Permission access, PageAllocator::Permission max_access) {
  MutexGuard guard(&mutex_);

  const Address address_at = reinterpret_cast<Address>(address);
  DCHECK(IsAligned(old_size, commit_page_size_));
  DCHECK(IsAligned(new_size, commit_page_size_));

  if (new_size < old_size) {
    // Shrinking is not supported at the moment.
    return false;
  } else if (new_size == old_size) {
    // Nothing to do in this case.
    return true;
  }

  DCHECK_LT(old_size, new_size);

  const Address allocated_old_size = RoundUp(old_size, allocate_page_size_);
  const Address allocated_new_size = RoundUp(new_size, allocate_page_size_);

  if (allocated_old_size < allocated_new_size) {
    if (!region_allocator_.TryGrowRegion(address_at, allocated_new_size)) {
      allocation_status_ = AllocationStatus::kHintedAddressTakenOrNotFound;
      return false;
    }
  }

  if (!page_allocator_->SetPermissions(
          reinterpret_cast<void*>(WithReservationProvenance(
              address_at + old_size, new_size - old_size)),
          new_size - old_size, access, max_access)) {
    if (allocated_old_size < allocated_new_size) {
      // This most likely means that we ran out of memory.
      CHECK_EQ(region_allocator_.TrimRegion(address_at, allocated_old_size),
               allocated_new_size - allocated_old_size);
    }

    allocation_status_ = AllocationStatus::kFailedToCommit;
    return false;
  }

  allocation_status_ = AllocationStatus::kSuccess;
  return true;
}

bool BoundedPageAllocator::ReserveForSharedMemoryMapping(void* ptr,
                                                         size_t size) {
  MutexGuard guard(&mutex_);

  Address address = reinterpret_cast<Address>(ptr);
  DCHECK(IsAligned(address, allocate_page_size_));
  DCHECK(IsAligned(size, commit_page_size_));

  DCHECK(region_allocator_.contains(address, size));

  // Region allocator requires page size rather than commit size so just over-
  // allocate there since any extra space couldn't be used anyway.
  size_t region_size = RoundUp(size, allocate_page_size_);
  if (!region_allocator_.AllocateRegionAt(
          address, region_size, RegionAllocator::RegionState::kExcluded)) {
    allocation_status_ = AllocationStatus::kHintedAddressTakenOrNotFound;
    return false;
  }

  const bool success = page_allocator_->SetPermissions(
      reinterpret_cast<void*>(WithReservationProvenance(address, size)), size,
      PageAllocator::Permission::kNoAccess,
      PageAllocator::Permission::kReadWrite);
  if (success) {
    allocation_status_ = AllocationStatus::kSuccess;
  } else {
    allocation_status_ = AllocationStatus::kFailedToCommit;
  }
  return success;
}

bool BoundedPageAllocator::FreePages(void* raw_address, size_t size) {
  // Careful: we are not locked here, do not touch BoundedPageAllocator
  // metadata.
  bool success;
  Address address = reinterpret_cast<Address>(raw_address);
  raw_address =
      reinterpret_cast<void*>(WithReservationProvenance(address, size));

  // The operations below can be expensive, don't hold the lock while they
  // happen. There is still potentially contention in the kernel, but at least
  // we don't need to hold the V8-side lock.
  if (page_initialization_mode_ ==
      PageInitializationMode::kAllocatedPagesMustBeZeroInitialized) {
    DCHECK_NE(page_freeing_mode_, PageFreeingMode::kDiscard);
    // When we are required to return zero-initialized pages, we decommit the
    // pages here, which will cause any wired pages to be removed by the OS.
    success = page_allocator_->DecommitPages(raw_address, size);
  } else {
    switch (page_freeing_mode_) {
      case PageFreeingMode::kMakeInaccessible:
        DCHECK_EQ(page_initialization_mode_,
                  PageInitializationMode::kAllocatedPagesCanBeUninitialized);
        success = page_allocator_->SetPermissions(raw_address, size,
                                                  PageAllocator::kNoAccess,
                                                  PageAllocator::kReadWrite);
        break;

      case PageFreeingMode::kDiscard:
        success = page_allocator_->DiscardSystemPages(raw_address, size);
        break;
    }
  }

  MutexGuard guard(&mutex_);
  CHECK_EQ(size, region_allocator_.FreeRegion(address));

  return success;
}

bool BoundedPageAllocator::ReleasePages(void* raw_address, size_t size,
                                        size_t new_size) {
  Address address = reinterpret_cast<Address>(raw_address);
  DCHECK(IsAligned(address, allocate_page_size_));

  DCHECK_LT(new_size, size);
  DCHECK(IsAligned(size - new_size, commit_page_size_));

  // This must be held until the page permissions are updated.
  MutexGuard guard(&mutex_);

  // Check if we freed any allocatable pages by this release.
  size_t allocated_size = RoundUp(size, allocate_page_size_);
  size_t new_allocated_size = RoundUp(new_size, allocate_page_size_);

#ifdef DEBUG
  {
    // There must be an allocated region at given |address| of a size not
    // smaller than |size|.
    DCHECK_EQ(allocated_size, region_allocator_.CheckRegion(address));
  }
#endif

  if (new_allocated_size < allocated_size) {
    region_allocator_.TrimRegion(address, new_allocated_size);
  }

  // Keep the region in "used" state just uncommit some pages.
  size_t free_size = size - new_size;
  void* free_address = reinterpret_cast<void*>(
      WithReservationProvenance(address + new_size, free_size));
  if (page_initialization_mode_ ==
      PageInitializationMode::kAllocatedPagesMustBeZeroInitialized) {
    DCHECK_NE(page_freeing_mode_, PageFreeingMode::kDiscard);
    // See comment in FreePages().
    return (page_allocator_->DecommitPages(free_address, free_size));
  }
  if (page_freeing_mode_ == PageFreeingMode::kMakeInaccessible) {
    DCHECK_EQ(page_initialization_mode_,
              PageInitializationMode::kAllocatedPagesCanBeUninitialized);
    return page_allocator_->SetPermissions(free_address, free_size,
                                           PageAllocator::kNoAccess,
                                           PageAllocator::kReadWrite);
  }
  CHECK_EQ(page_freeing_mode_, PageFreeingMode::kDiscard);
  return page_allocator_->DiscardSystemPages(free_address, free_size);
}

bool BoundedPageAllocator::SetPermissions(
    void* address, size_t size, PageAllocator::Permission access,
    PageAllocator::Permission max_access) {
  DCHECK(IsAligned(reinterpret_cast<Address>(address), commit_page_size_));
  DCHECK(IsAligned(size, commit_page_size_));
  DCHECK(region_allocator_.contains(reinterpret_cast<Address>(address), size));
  const bool success = page_allocator_->SetPermissions(
      reinterpret_cast<void*>(WithReservationProvenance(
          reinterpret_cast<Address>(address), size)),
      size, access, max_access);
  if (!success) {
    allocation_status_ = AllocationStatus::kFailedToCommit;
  }
  return success;
}

bool BoundedPageAllocator::RecommitPages(void* address, size_t size,
                                         PageAllocator::Permission access,
                                         PageAllocator::Permission max_access) {
  DCHECK(IsAligned(reinterpret_cast<Address>(address), commit_page_size_));
  DCHECK(IsAligned(size, commit_page_size_));
  DCHECK(region_allocator_.contains(reinterpret_cast<Address>(address), size));
  const bool success = page_allocator_->RecommitPages(
      reinterpret_cast<void*>(WithReservationProvenance(
          reinterpret_cast<Address>(address), size)),
      size, access, max_access);
  if (!success) {
    allocation_status_ = AllocationStatus::kFailedToCommit;
  }
  return success;
}

bool BoundedPageAllocator::DiscardSystemPages(void* address, size_t size) {
  return page_allocator_->DiscardSystemPages(
      reinterpret_cast<void*>(WithReservationProvenance(
          reinterpret_cast<Address>(address), size)),
      size);
}

bool BoundedPageAllocator::DecommitPages(void* address, size_t size) {
  return page_allocator_->DecommitPages(
      reinterpret_cast<void*>(WithReservationProvenance(
          reinterpret_cast<Address>(address), size)),
      size);
}

bool BoundedPageAllocator::SealPages(void* address, size_t size) {
  return page_allocator_->SealPages(
      reinterpret_cast<void*>(WithReservationProvenance(
          reinterpret_cast<Address>(address), size)),
      size);
}

const char* BoundedPageAllocator::AllocationStatusToString(
    AllocationStatus allocation_status) {
  switch (allocation_status) {
    case AllocationStatus::kSuccess:
      return "Success";
    case AllocationStatus::kFailedToCommit:
      return "Failed to commit";
    case AllocationStatus::kRanOutOfReservation:
      return "Ran out of reservation";
    case AllocationStatus::kHintedAddressTakenOrNotFound:
      return "Hinted address was taken or not found";
  }
}

BoundedPageAllocator::Stats BoundedPageAllocator::RecordStats() {
  MutexGuard guard(&mutex_);
  return {.free_size = region_allocator_.free_size(),
          .largest_free_region = region_allocator_.GetLargestFreeRegionSize(),
          .allocation_status = allocation_status_};
}

}  // namespace base
}  // namespace v8

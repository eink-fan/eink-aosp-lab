#include "eink_snapshot_pool.h"

#include <utility>

namespace einklab {

SnapshotPool::SnapshotPool(std::vector<std::shared_ptr<ComposeBuffer>> buffers) {
  if (buffers.empty()) {
    return;
  }
  for (const auto& buffer : buffers) {
    if (!buffer) {
      return;
    }
  }
  slots_.reserve(buffers.size());
  for (auto& buffer : buffers) {
    slots_.push_back(Slot{.buffer = std::move(buffer)});
  }
}

std::optional<SnapshotLease> SnapshotPool::TryAcquire() {
  std::lock_guard lock(mutex_);
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    Slot& slot = slots_[index];
    if (slot.state != State::kFree) {
      continue;
    }
    slot.state = State::kCopying;
    ++slot.generation;
    return SnapshotLease{
            .slot = index,
            .generation = slot.generation,
            .buffer = slot.buffer,
    };
  }
  ++drop_count_;
  return std::nullopt;
}

bool SnapshotPool::Matches(const SnapshotLease& lease, State expected) const {
  return lease.slot < slots_.size() && lease.buffer &&
          slots_[lease.slot].buffer == lease.buffer &&
          slots_[lease.slot].generation == lease.generation &&
          slots_[lease.slot].state == expected;
}

bool SnapshotPool::HandOffToConverter(const SnapshotLease& lease) {
  std::lock_guard lock(mutex_);
  if (!Matches(lease, State::kCopying)) {
    return false;
  }
  slots_[lease.slot].state = State::kConverting;
  return true;
}

bool SnapshotPool::AbortCopy(const SnapshotLease& lease) {
  std::lock_guard lock(mutex_);
  if (!Matches(lease, State::kCopying)) {
    return false;
  }
  slots_[lease.slot].state = State::kFree;
  return true;
}

bool SnapshotPool::ReleaseAfterConversion(const SnapshotLease& lease) {
  std::lock_guard lock(mutex_);
  if (!Matches(lease, State::kConverting)) {
    return false;
  }
  slots_[lease.slot].state = State::kFree;
  return true;
}

std::size_t SnapshotPool::Capacity() const {
  std::lock_guard lock(mutex_);
  return slots_.size();
}

std::size_t SnapshotPool::BusyCount() const {
  std::lock_guard lock(mutex_);
  std::size_t busy = 0;
  for (const auto& slot : slots_) {
    if (slot.state != State::kFree) {
      ++busy;
    }
  }
  return busy;
}

std::uint64_t SnapshotPool::DropCount() const {
  std::lock_guard lock(mutex_);
  return drop_count_;
}

}  // namespace einklab

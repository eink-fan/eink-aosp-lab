#pragma once

#include "eink_presentation_adapter.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace einklab {

// A lease identifies one GPU-copy destination buffer. The generation prevents
// a delayed worker from releasing a slot that has since been reused.
struct SnapshotLease {
  std::size_t slot = 0;
  std::uint64_t generation = 0;
  std::shared_ptr<ComposeBuffer> buffer;
};

// Fixed, non-blocking ownership state for separately copied composition
// frames. An platform backend associates a destination GraphicBuffer and its
// draw-completion fence with the lease externally; this core deliberately has
// no platform fence or allocator dependency.
class SnapshotPool {
 public:
  // Empty or null-buffer input creates a zero-capacity fail-closed pool. This
  // keeps the portable core usable in platform targets built without C++
  // exceptions; TryAcquire() then returns null without ever exposing a slot.
  explicit SnapshotPool(std::vector<std::shared_ptr<ComposeBuffer>> buffers);

  // Free -> copying. Returns null and increments DropCount when every slot is
  // busy; it never waits for a worker or GPU fence.
  [[nodiscard]] std::optional<SnapshotLease> TryAcquire();

  // Copying -> converting: call after the RenderEngine copy and its output
  // fence have been handed to the conversion worker.
  [[nodiscard]] bool HandOffToConverter(const SnapshotLease& lease);

  // Copying -> free: call if GPU-copy submission itself fails.
  [[nodiscard]] bool AbortCopy(const SnapshotLease& lease);

  // Converting -> free: call only after CPU mapping/conversion and any owned
  // grayscale handoff complete.
  [[nodiscard]] bool ReleaseAfterConversion(const SnapshotLease& lease);

  [[nodiscard]] std::size_t Capacity() const;
  [[nodiscard]] std::size_t BusyCount() const;
  [[nodiscard]] std::uint64_t DropCount() const;

 private:
  enum class State { kFree, kCopying, kConverting };

  struct Slot {
    std::shared_ptr<ComposeBuffer> buffer;
    State state = State::kFree;
    std::uint64_t generation = 0;
  };

  [[nodiscard]] bool Matches(const SnapshotLease& lease, State expected) const;

  mutable std::mutex mutex_;
  std::vector<Slot> slots_;
  std::uint64_t drop_count_ = 0;
};

}  // namespace einklab

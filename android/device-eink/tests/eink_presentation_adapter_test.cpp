#include "eink_presentation_adapter.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using neo2::eink::ComposeBuffer;
using neo2::eink::Engine;
using neo2::eink::Frame;
using neo2::eink::GrayscaleBuffer;
using neo2::eink::PresentationAdapter;
using neo2::eink::Rect;
using neo2::eink::RefreshMode;
using neo2::eink::ToEngineMode;

class Buffer final : public ComposeBuffer {};

class Pixels final : public GrayscaleBuffer {
 public:
  explicit Pixels(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

  const std::uint8_t* Data() const override { return bytes_.data(); }
  std::size_t ByteCount() const override { return bytes_.size(); }

 private:
  std::vector<std::uint8_t> bytes_;
};

class FakeEngine final : public Engine {
 public:
  bool Submit(const Frame& frame) override {
    {
      std::unique_lock lock(mutex_);
      submitted_.push_back(frame.sequence);
      refresh_ids_.push_back(frame.diagnostic_request);
      entered_first_submit_ = true;
      condition_.notify_all();
      if (block_first_submit_) {
        condition_.wait(lock, [this] { return release_first_submit_; });
      }
    }
    condition_.notify_all();
    return true;
  }

  bool WaitForFirstSubmit() {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, 1s, [this] {
      return entered_first_submit_;
    });
  }

  bool WaitForCount(std::size_t wanted) {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, 1s, [this, wanted] {
      return submitted_.size() >= wanted;
    });
  }

  void ReleaseFirstSubmit() {
    {
      std::lock_guard lock(mutex_);
      release_first_submit_ = true;
    }
    condition_.notify_all();
  }

  std::vector<std::uint64_t> RefreshIds() {
    std::lock_guard lock(mutex_);
    return refresh_ids_;
  }

  std::vector<std::uint64_t> Submitted() {
    std::lock_guard lock(mutex_);
    return submitted_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<std::uint64_t> submitted_;
  std::vector<std::uint64_t> refresh_ids_;
  bool block_first_submit_ = true;
  bool entered_first_submit_ = false;
  bool release_first_submit_ = false;
};

Frame MakeFrame(std::uint64_t sequence, const std::shared_ptr<Pixels>& pixels,
                const std::shared_ptr<Buffer>& buffer, std::uint64_t sleep_image_epoch = 0) {
  return Frame{
      .sequence = sequence,
      .sleep_image_epoch = sleep_image_epoch,
      .compose_buffer = buffer,
      .source_buffer = nullptr,
      .grayscale_buffer = pixels,
      .dirty_rect = Rect{.left = 0, .top = 0, .right = 2, .bottom = 2},
      .engine_mode = ToEngineMode(RefreshMode::kGc16Partial),
      .policy_flag = 0,
      .handwriting = false,
  };
}

Frame MakeDirectVendorFrame(std::uint64_t sequence, const std::shared_ptr<Buffer>& buffer) {
  return Frame{
      .sequence = sequence,
      .compose_buffer = buffer,
      .source_buffer = nullptr,
      .grayscale_buffer = nullptr,
      .dirty_rect = Rect{.left = 0, .top = 0, .right = 2, .bottom = 2},
      .engine_mode = ToEngineMode(RefreshMode::kGc16Partial),
      .policy_flag = 0,
      .handwriting = false,
  };
}

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

bool WaitForOutputSlot(PresentationAdapter& adapter) {
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (adapter.output_slot_state().next_eligible.has_value()) return true;
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

}  // namespace

int main() {
  auto buffer = std::make_shared<Buffer>();
  auto pixels = std::make_shared<Pixels>(std::vector<std::uint8_t>{0, 64, 128, 255});
  FakeEngine engine;
  PresentationAdapter adapter(engine);

  Require(!adapter.Enqueue(MakeFrame(1, pixels, buffer)),
          "enqueue before Start must fail");
  adapter.Start();
  Require(adapter.Enqueue(MakeFrame(1, pixels, buffer)), "first enqueue failed");
  Require(engine.WaitForFirstSubmit(), "first frame was not submitted");

  // While the first frame is in-flight, two newer submissions occupy the
  // one pending slot. The older pending frame must be replaced.
  Require(adapter.Enqueue(MakeFrame(2, pixels, buffer)), "second enqueue failed");
  Require(adapter.Enqueue(MakeFrame(3, pixels, buffer)), "third enqueue failed");
  engine.ReleaseFirstSubmit();
  Require(engine.WaitForCount(2), "coalesced frame was not submitted");

  // The IdisplayEpdcDeviceSync backend converts from the captured
  // GraphicBuffer itself. It must not be rejected just because it intentionally
  // supplies no duplicate CPU grayscale staging buffer.
  Require(adapter.Enqueue(MakeDirectVendorFrame(4, buffer)),
          "direct vendor frame without CPU staging was rejected");
  Require(engine.WaitForCount(3), "direct vendor frame was not submitted");

  // M3's owned GPU snapshot is released after worker-side conversion. The
  // serial capture-only path therefore submits only owned grayscale staging.
  Require(adapter.Enqueue(MakeFrame(5, pixels, nullptr)),
          "grayscale-only capture frame was rejected");
  Require(engine.WaitForCount(4), "grayscale-only capture frame was not submitted");

  adapter.Stop();
  const auto submitted = engine.Submitted();
  Require(submitted.size() == 4, "unexpected number of engine submissions");
  Require(submitted[0] == 1, "first submission changed order");
  Require(submitted[1] == 3, "latest pending frame did not win");
  Require(submitted[2] == 4, "direct vendor frame changed order");
  Require(submitted[3] == 5, "grayscale-only capture frame changed order");
  const auto diagnostics = adapter.diagnostics();
  Require(diagnostics.enqueues_accepted == 5, "enqueue telemetry count is wrong");
  Require(diagnostics.pending_replaced == 1, "pending replacement telemetry count is wrong");
  Require(diagnostics.submit_calls == 4 && diagnostics.submit_rejected == 0,
          "submit telemetry count is wrong");

  // Aura's lower call needs both planes from the same owned snapshot. Prove
  // that the serial scheduler retains both references while Submit is
  // blocked, then releases them after the call returns.
  FakeEngine color_engine;
  PresentationAdapter color_adapter(color_engine);
  color_adapter.Start();
  auto color_buffer = std::make_shared<Buffer>();
  auto color_pixels = std::make_shared<Pixels>(std::vector<std::uint8_t>{1, 2, 3, 4});
  std::weak_ptr<Buffer> color_buffer_lifetime = color_buffer;
  std::weak_ptr<Pixels> color_pixels_lifetime = color_pixels;
  Require(color_adapter.Enqueue(MakeFrame(6, color_pixels, color_buffer)),
          "paired color-frame enqueue failed");
  color_buffer.reset();
  color_pixels.reset();
  Require(color_engine.WaitForFirstSubmit(), "paired color frame was not submitted");
  Require(!color_buffer_lifetime.expired() && !color_pixels_lifetime.expired(),
          "paired color frame was released during asynchronous Submit");
  color_engine.ReleaseFirstSubmit();
  Require(color_engine.WaitForCount(1), "paired color frame did not complete");
  color_adapter.Stop();
  Require(color_buffer_lifetime.expired() && color_pixels_lifetime.expired(),
          "paired color frame remained retained after Submit returned");

  FakeEngine trailing_engine;
  PresentationAdapter trailing_adapter(
          trailing_engine, {}, [] { return std::chrono::milliseconds(40); });
  trailing_adapter.Start();
  Require(trailing_adapter.Enqueue(MakeFrame(10, pixels, buffer)), "trailing first enqueue failed");
  Require(trailing_engine.WaitForFirstSubmit(), "trailing first frame was not submitted");
  trailing_engine.ReleaseFirstSubmit();
  Require(trailing_engine.WaitForCount(1), "trailing first frame did not complete");
  Require(WaitForOutputSlot(trailing_adapter), "output slot did not report its rate-limit boundary");
  const auto idle_slot = trailing_adapter.output_slot_state();
  Require(!idle_slot.has_retained_candidate && idle_slot.next_eligible.has_value(),
          "output slot did not report its rate-limit boundary");
  Require(trailing_adapter.Enqueue(MakeFrame(11, pixels, buffer)), "trailing second enqueue failed");
  Require(trailing_adapter.Enqueue(MakeFrame(12, pixels, buffer)), "trailing replacement enqueue failed");
  const auto retained_slot = trailing_adapter.output_slot_state();
  Require(retained_slot.has_retained_candidate && retained_slot.next_eligible.has_value(),
          "output slot did not expose the retained trailing candidate");
  Require(trailing_engine.WaitForCount(2), "trailing frame was not submitted without another event");
  Require(trailing_adapter.Enqueue(MakeFrame(13, pixels, buffer, 9)),
          "pending sleep image enqueue failed");
  Require(trailing_adapter.CancelPendingSleepImage(9),
          "matching pending sleep image was not cancelled");
  Require(!trailing_adapter.CancelPendingSleepImage(9),
          "sleep image cancellation succeeded twice");
  std::this_thread::sleep_for(50ms);
  trailing_adapter.Stop();
  const auto trailing_submitted = trailing_engine.Submitted();
  Require(trailing_submitted.size() == 2 && trailing_submitted[1] == 12,
          "trailing scheduler did not retain the newest frame");
  const auto trailing_diagnostics = trailing_adapter.diagnostics();
  Require(trailing_diagnostics.trailing_deferred == 1 &&
                  trailing_diagnostics.trailing_submitted == 1,
          "trailing scheduler telemetry is wrong");
  Require(trailing_diagnostics.pending_sleep_images_cancelled == 1,
          "pending sleep-image cancellation telemetry is wrong");
  for (int cancellation : {0, 1, 2}) {
    const bool sleep_replaces = cancellation == 1;
    FakeEngine refresh_engine;
    PresentationAdapter refresh_adapter(refresh_engine);
    refresh_adapter.Start();
    Require(refresh_adapter.Enqueue(MakeFrame(20, pixels, buffer)), "initial enqueue");
    Require(refresh_engine.WaitForFirstSubmit(), "initial submit");
    auto refresh = MakeFrame(21, pixels, buffer);
    refresh.diagnostic_refresh = neo2::eink::DiagnosticRefresh::kWhiteThenTarget;
    refresh.diagnostic_request = 7;
    Require(refresh_adapter.Enqueue(refresh), "refresh enqueue");
    Require(refresh_adapter.Enqueue(MakeFrame(22, pixels, buffer, sleep_replaces ? 1 : 0)),
            "replacement enqueue");
    if (cancellation == 2) {
      Require(refresh_adapter.CancelPendingDiagnosticRefresh(), "explicit refresh cancellation");
      Require(!refresh_adapter.CancelPendingDiagnosticRefresh(), "duplicate refresh cancellation");
    }
    refresh_engine.ReleaseFirstSubmit();
    Require(refresh_engine.WaitForCount(2), "refresh replacement submit");
    refresh_adapter.Stop();
    Require(refresh_engine.Submitted()[1] == 22, "refresh used stale target");
    Require(refresh_engine.RefreshIds()[1] == (cancellation != 0 ? 0 : 7),
            "refresh must follow newest awake frame but never transfer to sleep");
  }
  std::cout << "PASS\n";
  return 0;
}

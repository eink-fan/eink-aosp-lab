#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Neo2EinkVendorSync"

#include "neo2/eink/android_vendor_eink_pipeline.h"

#include "neo2/eink/android_eink_pipeline.h"

#include <eink_device_profile.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace neo2::eink::android {

namespace {

constexpr const auto& kDeviceProfile = BuildEinkDeviceProfile();
constexpr int kObservedGc16FullMode =
        ToEngineMode(RefreshMode::kGc16Partial) + ToEngineMode(RefreshMode::kFullUpdate);

}  // namespace

class AndroidVendorEinkPipeline::Impl {
 public:
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::optional<CapturedComposition> pending;
  std::thread worker;
  Diagnostics diagnostics;
  std::uint64_t next_sequence = 1;
  bool enabled = false;
  bool started = false;
  bool stopping = false;
};

AndroidVendorEinkPipeline::AndroidVendorEinkPipeline(Engine& engine)
    : engine_(engine), adapter_(engine_), impl_(std::make_unique<Impl>()) {}

AndroidVendorEinkPipeline::~AndroidVendorEinkPipeline() {
  Stop();
}

void AndroidVendorEinkPipeline::Start() {
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->started || impl_->stopping) {
      return;
    }
    impl_->started = true;
  }
  adapter_.Start();
  impl_->worker = std::thread(&AndroidVendorEinkPipeline::WorkerMain, this);
}

void AndroidVendorEinkPipeline::Stop() {
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->started || impl_->stopping) {
      return;
    }
    impl_->stopping = true;
    impl_->enabled = false;
    impl_->pending.reset();
  }
  impl_->condition.notify_one();
  impl_->worker.join();
  // PresentationAdapter serially joins an in-flight Engine::Submit. The
  // retained AndroidComposeBuffer then releases its snapshot-pool lease.
  adapter_.Stop();
}

void AndroidVendorEinkPipeline::SetEnabled(bool enabled) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->started && !impl_->stopping) {
    impl_->enabled = enabled;
  }
}

void AndroidVendorEinkPipeline::OnCapturedComposition(CapturedComposition composition) {
  const bool is_owned_panel_snapshot = composition.ownership_lease && composition.compose_buffer &&
          composition.ready_fence &&
          composition.compose_buffer->getWidth() == kDeviceProfile.panel_width &&
          composition.compose_buffer->getHeight() == kDeviceProfile.panel_height;
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->started || impl_->stopping || !impl_->enabled) {
      return;
    }
    if (!is_owned_panel_snapshot) {
      ++impl_->diagnostics.rejected;
      return;
    }
    if (impl_->pending) {
      ++impl_->diagnostics.coalesced;
    }
    impl_->pending = std::move(composition);
    ++impl_->diagnostics.accepted;
  }
  impl_->condition.notify_one();
}

AndroidVendorEinkPipeline::Diagnostics AndroidVendorEinkPipeline::diagnostics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostics;
}

void AndroidVendorEinkPipeline::WorkerMain() {
  while (true) {
    std::optional<CapturedComposition> composition;
    std::uint64_t sequence = 0;
    {
      std::unique_lock lock(impl_->mutex);
      impl_->condition.wait(lock, [this] {
        return impl_->stopping || impl_->pending.has_value();
      });
      if (impl_->stopping) {
        return;
      }
      composition = std::move(impl_->pending);
      impl_->pending.reset();
      sequence = impl_->next_sequence++;
    }

    Frame frame{
            .sequence = sequence,
            .compose_buffer = std::make_shared<AndroidComposeBuffer>(
                    std::move(composition->compose_buffer), std::move(composition->ready_fence),
                    std::move(composition->ownership_lease)),
            .source_buffer = nullptr,
            .grayscale_buffer = nullptr,
            .dirty_rect = {.left = 0,
                           .top = 0,
                           .right = static_cast<int>(kDeviceProfile.panel_width),
                           .bottom = static_cast<int>(kDeviceProfile.panel_height)},
            .engine_mode = kObservedGc16FullMode,
            .policy_flag = 0,
            .handwriting = false,
    };
    const bool enqueued = adapter_.Enqueue(std::move(frame));
    if (!enqueued) {
      std::lock_guard lock(impl_->mutex);
      ++impl_->diagnostics.rejected;
      continue;
    }
    std::lock_guard lock(impl_->mutex);
    ++impl_->diagnostics.enqueued;
  }
}

}  // namespace neo2::eink::android

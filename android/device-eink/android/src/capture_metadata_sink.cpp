#include "neo2/eink/capture_metadata_sink.h"

namespace neo2::eink::android {

CaptureMetadataSink::CaptureMetadataSink(const EinkDeviceProfile& profile)
    : geometry_gate_(profile) {}

CaptureMetadataSink::CaptureMetadataSink(std::uint32_t expected_width,
                                         std::uint32_t expected_height)
    : geometry_gate_(expected_width, expected_height) {}

void CaptureMetadataSink::OnCapturedComposition(CapturedComposition composition) {
  if (!composition.compose_buffer) {
    return;
  }
  std::lock_guard lock(mutex_);
  const CaptureMetadata metadata{
          .buffer_id = composition.buffer_id,
          .width = composition.compose_buffer->getWidth(),
          .height = composition.compose_buffer->getHeight(),
          .pixel_format = static_cast<std::int32_t>(composition.compose_buffer->getPixelFormat()),
          .geometry_status = geometry_gate_.Observe(composition.compose_buffer->getWidth(),
                                                    composition.compose_buffer->getHeight()),
  };
  latest_capture_ = metadata;
}

std::uint64_t CaptureMetadataSink::CaptureCount() const {
  std::lock_guard lock(mutex_);
  return geometry_gate_.SampleCount();
}

std::uint64_t CaptureMetadataSink::MismatchedCaptureCount() const {
  std::lock_guard lock(mutex_);
  return geometry_gate_.MismatchCount();
}

PanelGeometryStatus CaptureMetadataSink::GeometryStatus() const {
  std::lock_guard lock(mutex_);
  return geometry_gate_.Status();
}

bool CaptureMetadataSink::IsPanelGeometryVerified() const {
  std::lock_guard lock(mutex_);
  return geometry_gate_.IsVerified();
}

std::uint32_t CaptureMetadataSink::ExpectedWidth() const {
  std::lock_guard lock(mutex_);
  return geometry_gate_.ExpectedWidth();
}

std::uint32_t CaptureMetadataSink::ExpectedHeight() const {
  std::lock_guard lock(mutex_);
  return geometry_gate_.ExpectedHeight();
}

bool CaptureMetadataSink::HasRecognizedProfile() const {
  std::lock_guard lock(mutex_);
  return geometry_gate_.HasRecognizedProfile();
}

std::optional<CaptureMetadata> CaptureMetadataSink::LatestCapture() const {
  std::lock_guard lock(mutex_);
  return latest_capture_;
}

}  // namespace neo2::eink::android

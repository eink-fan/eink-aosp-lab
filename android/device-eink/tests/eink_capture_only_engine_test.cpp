#include "eink_capture_only_engine.h"

#include <cassert>
#include <memory>
#include <optional>

namespace {

class Buffer final : public neo2::eink::ComposeBuffer {};

}  // namespace

int main() {
  std::optional<neo2::eink::CaptureOnlySubmission> observed;
  neo2::eink::CaptureOnlyEngine engine(
          [&observed](const neo2::eink::CaptureOnlySubmission& submission) { observed = submission; });
  const auto buffer = std::make_shared<Buffer>();
  const neo2::eink::Frame frame{
          .sequence = 41,
          .compose_buffer = buffer,
          .source_buffer = nullptr,
          .grayscale_buffer = nullptr,
          .dirty_rect = {.left = 3, .top = 5, .right = 13, .bottom = 17},
          .engine_mode = 0x22,
          .policy_flag = 7,
          .handwriting = true,
  };

  assert(engine.SubmissionCount() == 0);
  assert(!engine.LatestSubmission().has_value());
  assert(engine.Submit(frame));
  assert(engine.SubmissionCount() == 1);

  const auto submission = engine.LatestSubmission();
  assert(submission.has_value());
  assert(submission->sequence == 41);
  assert(submission->dirty_rect.left == 3 && submission->dirty_rect.top == 5);
  assert(submission->dirty_rect.right == 13 && submission->dirty_rect.bottom == 17);
  assert(submission->engine_mode == 0x22);
  assert(submission->policy_flag == 7);
  assert(submission->handwriting);
  assert(!submission->has_grayscale_staging);
  assert(observed.has_value());
  assert(observed->sequence == 41);
}

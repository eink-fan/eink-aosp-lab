#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Neo2PogoButtons"

#include "neo2/eink/pogo_button_long_press_configurator.h"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <fcntl.h>
#include <log/log.h>
#include <unistd.h>

#include <string>
#include <string_view>

namespace neo2::eink::android {
namespace {

constexpr char kLongPressUpPath[] = "/sys/eink/kpd/kpd_longpress_up_switch";
constexpr char kLongPressDownPath[] = "/sys/eink/kpd/kpd_longpress_down_switch";
constexpr std::string_view kLongPressUpSelector = "4";    // KEY_F5
constexpr std::string_view kLongPressDownSelector = "5";  // KEY_F6

bool ReadSelector(const char* path, std::string* value) {
  std::string raw;
  if (!::android::base::ReadFileToString(path, &raw)) return false;
  *value = ::android::base::Trim(raw);
  return !value->empty();
}

bool WriteSelector(const char* path, std::string_view value) {
  const int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) return false;
  const ssize_t written = write(fd, value.data(), value.size());
  const int close_result = close(fd);
  if (written != static_cast<ssize_t>(value.size()) || close_result != 0) return false;
  std::string observed;
  return ReadSelector(path, &observed) && observed == value;
}

}  // namespace

bool ConfigurePogoButtonLongPressSelectors() {
  std::string original_up;
  std::string original_down;
  if (!ReadSelector(kLongPressUpPath, &original_up) ||
      !ReadSelector(kLongPressDownPath, &original_down) ||
      access(kLongPressUpPath, W_OK) != 0 || access(kLongPressDownPath, W_OK) != 0) {
    ALOGE("long-press selector endpoints are unavailable; pogo configuration unchanged");
    return false;
  }

  if (WriteSelector(kLongPressUpPath, kLongPressUpSelector) &&
      WriteSelector(kLongPressDownPath, kLongPressDownSelector)) {
    ALOGI("pogo long-press selectors configured: up=F5 down=F6");
    return true;
  }

  const bool restored_down = WriteSelector(kLongPressDownPath, original_down);
  const bool restored_up = WriteSelector(kLongPressUpPath, original_up);
  ALOGE("pogo long-press selector configuration failed; rollback=%s",
        restored_up && restored_down ? "complete" : "incomplete");
  return false;
}

}  // namespace neo2::eink::android

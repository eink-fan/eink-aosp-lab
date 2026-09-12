#pragma once

namespace neo2::eink::android {

// Applies the Neo 2 vendor keypad's boot-local long-press selectors. Short
// presses remain owned by the unchanged kernel keymap; this function touches
// only the two dedicated long-press selector endpoints.
[[nodiscard]] bool ConfigurePogoButtonLongPressSelectors();

}  // namespace neo2::eink::android

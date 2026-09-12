cmake_minimum_required(VERSION 3.20)
project(neo2_eink_presentation_adapter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(eink_presentation_adapter
    src/eink_capture_admission_policy.cpp
    src/eink_capture_pressure_accounting.cpp
    src/eink_debug_property.cpp
    src/eink_frame_demand_gate.cpp
    src/eink_full_pool_resample_policy.cpp
    src/eink_frontlight_audit.cpp
    src/eink_frontlight_policy.cpp
    src/eink_manual_frontlight_control.cpp
    src/eink_color_quality.cpp
    src/eink_gray_preference.cpp
    src/eink_grayscale_delta.cpp
    src/eink_grayscale_fidelity.cpp
    src/eink_idle_gate.cpp
    src/eink_lower_engine_layout.cpp
    src/eink_main_thread_resample_dispatcher.cpp
    src/eink_micro_delta_policy.cpp
    src/eink_presentation_adapter.cpp
    src/eink_capture_only_engine.cpp
    src/eink_one_shot_engine.cpp
    src/eink_policy.cpp
    src/eink_presentation_arm_gate.cpp
    src/eink_scalar_strncpy.cpp
    src/eink_snapshot_pool.cpp
    src/eink_sleep_image_background.cpp
    src/eink_sleep_image_latch.cpp
    src/eink_sleep_image_overlay.cpp
    src/eink_submission_observer.cpp
    src/neo2_panel_geometry_gate.cpp
    android/src/eink_sleep_image_catalog.cpp
    src/software_grayscale.cpp)
target_include_directories(eink_presentation_adapter PUBLIC include android/include)
target_compile_options(eink_presentation_adapter PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
file(GLOB host_tests CONFIGURE_DEPENDS tests/*_test.cpp)
foreach(source IN LISTS host_tests)
  get_filename_component(name "${source}" NAME_WE)
  add_executable(${name} "${source}")
  target_link_libraries(${name} PRIVATE eink_presentation_adapter)
  target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME ${name} COMMAND ${name})
endforeach()

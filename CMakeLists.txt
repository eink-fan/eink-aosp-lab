cmake_minimum_required(VERSION 3.20)
project(eink_aosp_lab LANGUAGES CXX)

enable_testing()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(einklab_core
  core/src/eink_capture_admission_policy.cpp
  core/src/eink_capture_pressure_accounting.cpp
  core/src/eink_frame_demand_gate.cpp
  core/src/eink_full_pool_resample_policy.cpp
  core/src/eink_grayscale_delta.cpp
  core/src/eink_grayscale_fidelity.cpp
  core/src/eink_idle_gate.cpp
  core/src/eink_main_thread_resample_dispatcher.cpp
  core/src/eink_micro_delta_policy.cpp
  core/src/eink_presentation_adapter.cpp
  core/src/eink_presentation_arm_gate.cpp
  core/src/eink_snapshot_pool.cpp
  core/src/eink_submission_observer.cpp
  core/src/software_grayscale.cpp)
target_include_directories(einklab_core PUBLIC
  core/include/einklab)
target_compile_options(einklab_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

set(EINKLAB_TESTS
  eink_capture_admission_policy_test
  eink_capture_pressure_accounting_test
  eink_frame_demand_gate_test
  eink_full_pool_resample_policy_test
  eink_grayscale_delta_test
  eink_grayscale_fidelity_test
  eink_idle_gate_test
  eink_main_thread_resample_dispatcher_test
  eink_micro_delta_policy_test
  eink_presentation_adapter_test
  eink_presentation_arm_gate_test
  eink_snapshot_pool_test
  eink_submission_observer_test
  software_grayscale_test)

foreach(test_name IN LISTS EINKLAB_TESTS)
  add_executable(${test_name} core/tests/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE einklab_core)
  target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

add_executable(neo2_android14_profile_test
  profiles/neo2-android14/tests/profile_test.cpp)
target_include_directories(neo2_android14_profile_test PRIVATE
  profiles/neo2-android14/include)
target_compile_options(neo2_android14_profile_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME neo2_android14_profile_test COMMAND neo2_android14_profile_test)

add_executable(neo2_android17_profile_test
  profiles/neo2-android17/tests/profile_test.cpp)
target_include_directories(neo2_android17_profile_test PRIVATE
  profiles/neo2-android17/include)
target_compile_options(neo2_android17_profile_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME neo2_android17_profile_test COMMAND neo2_android17_profile_test)

add_test(NAME public_tree_privacy_audit
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/audit-public-tree.sh)

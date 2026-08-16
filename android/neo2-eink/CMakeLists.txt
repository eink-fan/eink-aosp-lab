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
    src/eink_grayscale_delta.cpp
    src/eink_grayscale_fidelity.cpp
    src/eink_idle_gate.cpp
    src/eink_main_thread_resample_dispatcher.cpp
    src/eink_micro_delta_policy.cpp
    src/eink_presentation_adapter.cpp
    src/eink_capture_only_engine.cpp
    src/eink_one_shot_engine.cpp
    src/eink_policy.cpp
    src/eink_presentation_arm_gate.cpp
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

add_executable(eink_presentation_adapter_test
    tests/eink_presentation_adapter_test.cpp)
target_link_libraries(eink_presentation_adapter_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_presentation_adapter_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_test(NAME eink_sleep_image_binder_service_contract_test
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/eink_sleep_image_binder_service_contract_test.sh)
add_test(NAME eink_sleep_image_direct_presentation_contract_test
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/eink_sleep_image_direct_presentation_contract_test.sh)
add_test(NAME eink_sleep_image_overlay_contract_test
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/eink_sleep_image_overlay_contract_test.sh)
add_test(NAME sleep_image_catalog_contract_test
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/sleep_image_catalog_contract_test.sh)
find_program(NEO2_JAVAC_EXECUTABLE javac)
find_program(NEO2_JAVA_EXECUTABLE java)
if(NEO2_JAVAC_EXECUTABLE AND NEO2_JAVA_EXECUTABLE)
  execute_process(COMMAND ${NEO2_JAVA_EXECUTABLE} -version
                  RESULT_VARIABLE NEO2_JAVA_RUNTIME_RESULT
                  OUTPUT_QUIET ERROR_QUIET)
  if(NEO2_JAVA_RUNTIME_RESULT EQUAL 0)
    add_test(NAME sleep_image_catalog_host_test
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/sleep_image_catalog_host_test.sh)
  endif()
endif()
add_test(NAME eink_terminal_black_data_plane_contract_test
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/eink_terminal_black_data_plane_contract_test.sh)
add_test(NAME eink_sleep_image_lifecycle_contract_test
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/eink_sleep_image_lifecycle_contract_test.sh)
add_executable(eink_sleep_image_shared_memory_size_test
    tests/eink_sleep_image_shared_memory_size_test.cpp)
target_link_libraries(eink_sleep_image_shared_memory_size_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_sleep_image_shared_memory_size_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_sleep_image_shared_memory_size_test
         COMMAND eink_sleep_image_shared_memory_size_test)
add_test(NAME eink_presentation_adapter_test COMMAND eink_presentation_adapter_test)

add_executable(eink_capture_admission_policy_test tests/eink_capture_admission_policy_test.cpp)
target_link_libraries(eink_capture_admission_policy_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_capture_admission_policy_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_capture_admission_policy_test COMMAND eink_capture_admission_policy_test)

add_executable(eink_capture_pressure_accounting_test tests/eink_capture_pressure_accounting_test.cpp)
target_link_libraries(eink_capture_pressure_accounting_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_capture_pressure_accounting_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_capture_pressure_accounting_test COMMAND eink_capture_pressure_accounting_test)

add_executable(eink_debug_property_test tests/eink_debug_property_test.cpp)
target_link_libraries(eink_debug_property_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_debug_property_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_debug_property_test COMMAND eink_debug_property_test)

add_executable(eink_capture_only_engine_test tests/eink_capture_only_engine_test.cpp)
target_link_libraries(eink_capture_only_engine_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_capture_only_engine_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_capture_only_engine_test COMMAND eink_capture_only_engine_test)

add_executable(eink_snapshot_pool_test tests/eink_snapshot_pool_test.cpp)
target_link_libraries(eink_snapshot_pool_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_snapshot_pool_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_snapshot_pool_test COMMAND eink_snapshot_pool_test)

add_executable(eink_software_grayscale_test
    tests/software_grayscale_test.cpp)
target_link_libraries(eink_software_grayscale_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_software_grayscale_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_software_grayscale_test COMMAND eink_software_grayscale_test)

add_executable(eink_policy_test tests/eink_policy_test.cpp)
target_link_libraries(eink_policy_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_policy_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_policy_test COMMAND eink_policy_test)

add_executable(eink_presentation_arm_gate_test tests/eink_presentation_arm_gate_test.cpp)
target_link_libraries(eink_presentation_arm_gate_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_presentation_arm_gate_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_presentation_arm_gate_test COMMAND eink_presentation_arm_gate_test)

add_executable(vendor_hwc_epdc_abi_test tests/vendor_hwc_epdc_abi_test.cpp)
target_link_libraries(vendor_hwc_epdc_abi_test PRIVATE eink_presentation_adapter)
target_compile_options(vendor_hwc_epdc_abi_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME vendor_hwc_epdc_abi_test COMMAND vendor_hwc_epdc_abi_test)

add_executable(neo2_panel_geometry_gate_test tests/neo2_panel_geometry_gate_test.cpp)
target_link_libraries(neo2_panel_geometry_gate_test PRIVATE eink_presentation_adapter)
target_compile_options(neo2_panel_geometry_gate_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME neo2_panel_geometry_gate_test COMMAND neo2_panel_geometry_gate_test)

add_executable(eink_one_shot_engine_test tests/eink_one_shot_engine_test.cpp)
target_link_libraries(eink_one_shot_engine_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_one_shot_engine_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_one_shot_engine_test COMMAND eink_one_shot_engine_test)

add_executable(eink_frame_demand_gate_test tests/eink_frame_demand_gate_test.cpp)
target_link_libraries(eink_frame_demand_gate_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_frame_demand_gate_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_frame_demand_gate_test COMMAND eink_frame_demand_gate_test)

add_executable(eink_full_pool_resample_policy_test
    tests/eink_full_pool_resample_policy_test.cpp)
target_link_libraries(eink_full_pool_resample_policy_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_full_pool_resample_policy_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_full_pool_resample_policy_test COMMAND eink_full_pool_resample_policy_test)

add_executable(eink_main_thread_resample_dispatcher_test
    tests/eink_main_thread_resample_dispatcher_test.cpp)
target_link_libraries(eink_main_thread_resample_dispatcher_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_main_thread_resample_dispatcher_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_main_thread_resample_dispatcher_test COMMAND eink_main_thread_resample_dispatcher_test)

add_executable(eink_frontlight_policy_test tests/eink_frontlight_policy_test.cpp)
target_link_libraries(eink_frontlight_policy_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_frontlight_policy_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_frontlight_policy_test COMMAND eink_frontlight_policy_test)

add_executable(eink_manual_frontlight_control_test tests/eink_manual_frontlight_control_test.cpp)
target_link_libraries(eink_manual_frontlight_control_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_manual_frontlight_control_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_manual_frontlight_control_test COMMAND eink_manual_frontlight_control_test)

add_executable(eink_frontlight_audit_test tests/eink_frontlight_audit_test.cpp)
target_link_libraries(eink_frontlight_audit_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_frontlight_audit_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_frontlight_audit_test COMMAND eink_frontlight_audit_test)

add_executable(eink_grayscale_delta_test tests/eink_grayscale_delta_test.cpp)
target_link_libraries(eink_grayscale_delta_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_grayscale_delta_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_grayscale_delta_test COMMAND eink_grayscale_delta_test)

add_executable(eink_grayscale_fidelity_test tests/eink_grayscale_fidelity_test.cpp)
target_link_libraries(eink_grayscale_fidelity_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_grayscale_fidelity_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_grayscale_fidelity_test COMMAND eink_grayscale_fidelity_test)

add_executable(eink_idle_gate_test tests/eink_idle_gate_test.cpp)
target_link_libraries(eink_idle_gate_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_idle_gate_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_idle_gate_test COMMAND eink_idle_gate_test)

add_executable(eink_micro_delta_policy_test tests/eink_micro_delta_policy_test.cpp)
target_link_libraries(eink_micro_delta_policy_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_micro_delta_policy_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_micro_delta_policy_test COMMAND eink_micro_delta_policy_test)

add_executable(eink_submission_observer_test tests/eink_submission_observer_test.cpp)
target_link_libraries(eink_submission_observer_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_submission_observer_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_submission_observer_test COMMAND eink_submission_observer_test)

add_executable(eink_sleep_image_latch_test tests/eink_sleep_image_latch_test.cpp)
target_link_libraries(eink_sleep_image_latch_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_sleep_image_latch_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_sleep_image_latch_test COMMAND eink_sleep_image_latch_test)

add_executable(eink_sleep_image_overlay_test tests/eink_sleep_image_overlay_test.cpp)
target_link_libraries(eink_sleep_image_overlay_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_sleep_image_overlay_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_sleep_image_overlay_test COMMAND eink_sleep_image_overlay_test)

add_executable(eink_sleep_image_background_test tests/eink_sleep_image_background_test.cpp)
target_link_libraries(eink_sleep_image_background_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_sleep_image_background_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_sleep_image_background_test COMMAND eink_sleep_image_background_test)

add_executable(eink_sleep_image_catalog_test tests/eink_sleep_image_catalog_test.cpp)
target_link_libraries(eink_sleep_image_catalog_test PRIVATE eink_presentation_adapter)
target_compile_options(eink_sleep_image_catalog_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME eink_sleep_image_catalog_test COMMAND eink_sleep_image_catalog_test)

add_test(
    NAME eink_diagnostics_format_contract_test
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/eink_diagnostics_format_contract_test.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/android/src/render_surface_capture_session.cpp)

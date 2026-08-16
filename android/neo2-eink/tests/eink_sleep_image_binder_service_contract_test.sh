#!/usr/bin/env bash
set -euo pipefail

adapter_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file="$adapter_root/android/src/sleep_image_binder_service.cpp"
capture_session_source="$adapter_root/android/src/render_surface_capture_session.cpp"
bp_file="$adapter_root/android/Android.bp"

grep -Fq '#include <android/sharedmem.h>' "$source_file"
grep -Fq '#include <dlfcn.h>' "$source_file"
grep -Fq 'dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL)' "$source_file"
grep -Fq 'dlsym(handle, "ASharedMemory_getSize")' "$source_file"
grep -Fq 'GetSharedMemorySize(fd)' "$source_file"
grep -Fq 'EinkSleepImageCatalog::IsPanelByteSize(shared_memory_size)' "$source_file"
grep -Fq 'constexpr std::int32_t kProtocolVersion = 2;' "$source_file"
grep -Fq 'ReadPlane(data, &entry->panel_gray) && ReadPlane(data, &entry->alpha)' "$source_file"
grep -Fq 'PROT_READ, MAP_SHARED, fd, 0' "$source_file"
! grep -Fq 'PROT_READ, MAP_PRIVATE, fd, 0' "$source_file"
! grep -Fq 'fstat(fd' "$source_file"
! grep -Fq '"libandroid",' "$bp_file"
grep -Fq '"libdl",' "$bp_file"

grep -Fq 'if (epoch <= m3_->sleep_image_catalog_epoch)' "$capture_session_source"
grep -Fq 'g_session->PresentSleepImage(epoch)' "$source_file"
grep -Fq 'g_session->DisarmSleepCycle(epoch)' "$source_file"
grep -Fq 'epoch == m3_->sleep_image_catalog_epoch' "$capture_session_source"
grep -Fq 'pipeline->PresentSleepImage(epoch)' "$capture_session_source"
grep -Fq 'pending_catalog->mode' "$capture_session_source"
! grep -Eq 'SetScreen(Off|On)Epoch|force_sleep_capture' "$capture_session_source"
! grep -Fq 'ControlsProvider' "$capture_session_source"

printf 'Sleep-image native shared-memory contract tests passed.\n'

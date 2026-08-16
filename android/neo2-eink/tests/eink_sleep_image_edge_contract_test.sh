#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 FRAMEWORK_BASE_PATCH" >&2
  exit 2
fi

patch_file=$1
if [[ ! -r "$patch_file" ]]; then
  echo "missing framework patch: $patch_file" >&2
  exit 2
fi

grep -Fq 'private final Object mStateLock = new Object();' "$patch_file"
grep -Fq 'registerReceiverAsUser(mScreenReceiver, UserHandle.ALL, filter, null, null);' "$patch_file"
grep -Fq 'mHandler.post(this::requestRetainedCatalog);' "$patch_file"
grep -Fq '++mScreenEdgesReceived;' "$patch_file"
grep -Fq '++mScreenOffEpochDelivered;' "$patch_file"
grep -Fq '++mScreenOnEpochDelivered;' "$patch_file"
grep -Fq '" edge=" + mScreenEdgesReceived + "/" +' "$patch_file"
grep -Fq 'mScreenOffEpochDelivered + "/" + mScreenOnEpochDelivered);' "$patch_file"

screen_off_block=$(sed -n '/private void handleScreenOff()/,/^+    }$/p' "$patch_file")
screen_on_block=$(sed -n '/private void handleScreenOn()/,/^+    }$/p' "$patch_file")

[[ "$screen_off_block" == *'synchronized (mStateLock)'* ]]
[[ "$screen_off_block" == *'copyRetainedCatalog()'* ]]
[[ "$screen_off_block" == *'sendCatalog(mEpoch, catalog, mRandom.nextInt(catalog.size()))'* ]]
[[ "$screen_off_block" == *'sendEpoch(SF_SCREEN_OFF, mEpoch)'* ]]
[[ "$screen_off_block" != *'mProvider.transact('* ]]
[[ "$screen_off_block" != *'mHandler.post('* ]]

catalog_line=$(printf '%s\n' "$screen_off_block" | nl -ba | rg 'sendCatalog\(' | head -n 1 | awk '{print $1}')
off_line=$(printf '%s\n' "$screen_off_block" | nl -ba | rg 'sendEpoch\(SF_SCREEN_OFF' | head -n 1 | awk '{print $1}')
[[ -n "$catalog_line" && -n "$off_line" && "$catalog_line" -lt "$off_line" ]]

[[ "$screen_on_block" == *'synchronized (mStateLock)'* ]]
[[ "$screen_on_block" == *'sendEpoch(SF_SCREEN_ON, mEpoch)'* ]]
[[ "$screen_on_block" == *'mHandler.post(this::requestRetainedCatalog);'* ]]
[[ "$screen_on_block" != *'mProvider.transact('* ]]

! grep -Fq 'registerReceiverAsUser(mScreenReceiver, UserHandle.ALL, filter, null, mHandler);' "$patch_file"
! grep -Fq 'mHandler.post(() -> handleScreenOff())' "$patch_file"
! grep -Fq 'mHandler.post(() -> handleScreenOn())' "$patch_file"

printf 'Sleep-image screen-edge contract tests passed.\n'

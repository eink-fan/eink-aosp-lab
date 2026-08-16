#!/usr/bin/env bash
set -euo pipefail

adapter_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
app="$adapter_root/android/frontlight-app/src/org/neo2/controls"
store="$app/SleepImageStore.java"
transforms="$app/SleepImageTransforms.java"
service="$app/SleepImageCatalogService.java"
preview="$app/SleepImagePreviewActivity.java"
controls="$app/ControlsActivity.java"
manifest="$adapter_root/android/frontlight-app/AndroidManifest.xml"
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

for source in "$store" "$transforms" "$service" "$preview" "$controls"; do
  [[ -s "$source" ]] || fail "missing catalog source: $source"
done

grep -Fq 'MAXIMUM_ENTRIES = 8' "$store" || fail 'catalog is not bounded to eight entries'
grep -Fq 'current.generation != expectedGeneration' "$store" || fail 'mutations are not generation guarded'
grep -Fq 'MutationResult.FINAL_CANDIDATE' "$store" || fail 'final candidate removal is not rejected'
grep -Fq 'MutationResult.PROTECTED' "$store" || fail 'built-in fallback is not protected'
grep -Fq 'legacyEntries()' "$store" || fail 'legacy pair migration is absent'
grep -Fq 'LEGACY_RETIRED = "legacy-pairs-retired.v1"' "$store" ||
  fail 'legacy retirement marker is absent'
grep -Fq 'if (retireLegacy) publishPendingLegacyRetirement(generation, next)' "$store" ||
  fail 'legacy retirement lacks a replayable pre-manifest transaction'
grep -Fq 'recoverPendingLegacyRetirement()' "$store" ||
  fail 'pending legacy retirement is not recovered before catalog repair'
grep -Fq 'if (retireLegacy) completeLegacyRetirement()' "$store" ||
  fail 'legacy retirement is not completed after generation publication'
grep -Fq 'declaredBuiltIn != authoritativeBuiltIn' "$store" ||
  fail 'conflicting built-in metadata is not detected'
grep -Fq 'entry.id.equals(BUILT_IN_ID)' "$store" ||
  fail 'fallback protection is not derived from the authoritative ID'
grep -Fq 'setBuiltInRotation(long expectedGeneration, boolean enabled)' "$store" ||
  fail 'built-in rotation eligibility is not generation guarded'
grep -Fq 'fields.length == 2 || fields[2].equals("1")' "$store" ||
  fail 'legacy manifests do not default the built-in into rotation'
grep -Fq 'new PublicationEntry(fallback, true, true)' "$store" ||
  fail 'empty selection and missing-cover paths do not retain fallback'
grep -Fq 'readStableCover(File file)' "$store" ||
  fail 'bounded exact-file cover reader is absent'
grep -Fq 'source.length() != beforeLength' "$store" ||
  fail 'cover stability is not rechecked after reading'
grep -Fq 'fallbackGray.clone()' "$store" || fail 'empty/corrupt repair lacks the reviewed fallback'
grep -Fq 'output.getFD().sync()' "$store" || fail 'generation publication is not durably flushed'
grep -Fq 'StandardCopyOption.ATOMIC_MOVE' "$store" || fail 'catalog generation is not atomically published'

for bound in MAX_ENCODED_BYTES MAX_DIMENSION MAX_SOURCE_PIXELS MAX_DECODE_PIXELS; do
  grep -Fq "$bound" "$transforms" || fail "missing import bound: $bound"
done
grep -Fq 'animated images are not supported' "$transforms" || fail 'animation rejection is absent'
grep -Fq 'PRESENTATION_TO_PANEL_ROTATION = 270' "$transforms" ||
  fail 'measured portrait-to-panel rotation contract is absent'
grep -Fq 'presentationToPanel(presentation, panelHeight, panelWidth)' "$transforms" ||
  fail 'portrait framing is not followed by the fixed panel transform'
grep -Fq 'panelToPresentationPixels' "$transforms" ||
  fail 'final panel planes lack an inverse UI presentation transform'
grep -Fq 'java.util.Arrays.fill(gray, (byte) 0xf0)' "$transforms" ||
  fail 'fit padding is not white panel gray'
grep -Fq 'alpha[destination]' "$transforms" || fail 'alpha plane is not retained'
grep -Fq '& 0xf0' "$transforms" || fail 'final gray plane is not quantized to 16 levels'
for orientation in 1 2 3 4 5 6 7 8; do
  grep -Fq "case $orientation:" "$transforms" || fail "missing EXIF orientation $orientation"
done

grep -Fq 'info.isAnimated()' "$preview" || fail 'encoded animation is not rejected before decode'
grep -Fq 'decoder.setTargetSampleSize' "$preview" || fail 'working decode is not pre-downsampled'
grep -Fq 'getIntent().setData(null)' "$preview" || fail 'preview does not release its source URI'
! grep -Eq 'takePersistableUriPermission|write\([^)]*Uri|ACTION_IMPORT' "$app"/*.java ||
  fail 'source URI persistence or immediate import was reintroduced'
grep -Fq 'mCatalog.commit' "$preview" || fail 'explicit Import is not the sole publish action'
grep -Fq 'cancel.setOnClickListener(view -> finish())' "$preview" || fail 'explicit cancel is absent'
grep -Fq 'overlay samples on checkerboard and white' "$preview" ||
  fail 'overlay preview does not label both transparency samples'
grep -Fq 'SleepImageTransforms.renderForPanel' "$preview" ||
  fail 'import preview does not use the shared portrait-to-panel transform'
grep -Fq 'SleepImageCatalogService.PRESENTATION_WIDTH' "$preview" ||
  fail 'import preview is not rendered in portrait presentation geometry'

grep -Fq 'RefreshResult.PENDING' "$service" || fail 'durable and refresh results are conflated'
grep -Fq 'generation != storeGeneration' "$service" || fail 'refresh callback is not generation bound'
grep -Fq 'PROVIDER_PROTOCOL_VERSION = 4' "$service" ||
  fail 'fallback/eligibility provider metadata protocol is absent'
grep -Fq 'candidate.fallback ? 1 : 0' "$service" ||
  fail 'provider does not identify the retained fallback'
grep -Fq 'candidate.eligible ? 1 : 0' "$service" ||
  fail 'provider does not identify rotation eligibility'
grep -Fq '"/storage/emulated/0/screensaver"' "$service" ||
  fail 'KOReader integration is not confined to the established cover directory'
grep -Fq 'KOREADER_COVER_NAME = "current.png"' "$service" ||
  fail 'KOReader integration is not confined to the exact cover file'
grep -Fq 'FileObserver.CLOSE_WRITE' "$service" ||
  fail 'awake exact-file refresh observer is absent'
grep -Fq 'info.isAnimated()' "$service" ||
  fail 'KOReader animated covers are not rejected before decode'
grep -Fq 'SleepImageTransforms.plan(encoded.length' "$service" ||
  fail 'KOReader cover does not reuse bounded decode planning'
grep -Fq 'SleepImageTransforms.renderForPanel' "$service" ||
  fail 'KOReader cover does not use the shared portrait-to-panel transform'
grep -Fq 'mStore.summaries(snapshot, 134, 181)' "$service" ||
  fail 'catalog thumbnails are not requested in portrait geometry'
grep -Fq 'MODE_KOREADER_COVER = 3' "$controls" ||
  fail 'KOReader source mode does not preserve existing mode numbers'
grep -Fq 'Exclude built-in fallback from random rotation' "$controls" ||
  fail 'built-in rotation exclusion accessibility action is absent'
grep -Fq 'Include built-in fallback in random rotation' "$controls" ||
  fail 'built-in rotation inclusion accessibility action is absent'
grep -Fq 'THUMBNAIL_WIDTH = 134' "$controls" ||
  fail 'catalog UI does not construct portrait thumbnails'
grep -Fq 'android.permission.MANAGE_EXTERNAL_STORAGE' "$manifest" ||
  fail 'privileged exact-cover read permission is absent'
grep -Fq 'setAccessibilityHeading(true)' "$controls" || fail 'catalog heading accessibility is absent'
grep -Fq 'setMinimumHeight(dp(ACTION_HEIGHT_DP))' "$controls" || fail 'large action targets are absent'
grep -Fq 'new AlertDialog.Builder(this)' "$controls" || fail 'removal confirmation is absent'

printf 'Sleep-image catalog/import source contract tests passed.\n'

package org.neo2.controls;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;

public final class SleepImageCatalogHostTest {
    private static final int WIDTH = 181;
    private static final int HEIGHT = 134;
    private static final int BYTES = WIDTH * HEIGHT;

    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("neo2-sleep-store-").toFile();
        try {
            testCatalog(root);
            testRecoveryAndMigration(new File(root, "recovery"));
            testPendingLegacyRetirement(new File(root, "pending-retirement"));
            testLegacyResurrection(new File(root, "legacy-loss"), false);
            testLegacyResurrection(new File(root, "legacy-corrupt"), true);
            testBuiltInMetadata(new File(root, "metadata"));
            testRotationAndPublication(new File(root, "rotation"));
            testPanelOrientation(new File(root, "orientation"));
            testStableCover(new File(root, "cover"));
            testAtomicFailure(new File(root, "failure"));
            testFinalCandidate(new File(root, "final"));
            testTransforms();
        } finally {
            delete(root);
        }
        System.out.println("Sleep-image catalog host tests passed.");
    }

    private static void testPendingLegacyRetirement(File root) throws Exception {
        require(root.mkdirs(), "cannot create pending retirement fixture");
        byte[] legacyGray = gray(0x50);
        byte[] legacyAlpha = alpha(0x64);
        write(new File(root, "candidate-pending.gray"), legacyGray);
        write(new File(root, "candidate-pending.alpha"), legacyAlpha);
        SleepImageStore failing = new SleepImageStore(root, WIDTH, HEIGHT, operation -> {
            if (operation.equals("manifest")) throw new java.io.IOException("injected boundary");
        });
        try {
            failing.openAndRepair(gray(0x20), alpha(0xff));
            throw new AssertionError("pending retirement boundary did not fail");
        } catch (java.io.IOException expected) {
            require(Files.readString(new File(root, "legacy-pairs-retired.v1").toPath())
                            .startsWith("pending\ngeneration="),
                    "migration intent was not durable before manifest failure");
            require(new File(root, "candidate-pending.gray").isFile() &&
                            new File(root, "candidate-pending.alpha").isFile(),
                    "legacy planes were removed before migration committed");
        }

        SleepImageStore retry = new SleepImageStore(root, WIDTH, HEIGHT);
        SleepImageStore.Snapshot recovered = retry.openAndRepair(gray(0x30), alpha(0xff));
        require(recovered.entries.size() == 2 && recovered.entries.get(0).builtIn &&
                        Arrays.equals(recovered.entries.get(1).gray, legacyGray) &&
                        Arrays.equals(recovered.entries.get(1).alpha, legacyAlpha),
                "pending retirement retry did not retain byte-identical legacy planes");
        require(Files.readString(new File(root, "legacy-pairs-retired.v1").toPath())
                        .equals("retired\n"),
                "recovered migration did not complete retirement");
    }

    private static void testLegacyResurrection(File root, boolean corruptManifest)
            throws Exception {
        require(root.mkdirs(), "cannot create legacy retirement fixture");
        byte[] legacyGray = gray(0x50);
        byte[] legacyAlpha = alpha(0x70);
        File legacyGrayFile = new File(root, "candidate-retired.gray");
        File legacyAlphaFile = new File(root, "candidate-retired.alpha");
        write(legacyGrayFile, legacyGray);
        write(legacyAlphaFile, legacyAlpha);
        SleepImageStore store = new SleepImageStore(root, WIDTH, HEIGHT);
        SleepImageStore.Snapshot migrated = store.openAndRepair(gray(0x20), alpha(0xff));
        require(new File(root, "legacy-pairs-retired.v1").isFile(),
                "migration did not publish its retirement marker");
        String migratedId = migrated.entries.get(1).id;

        // Model best-effort legacy cleanup leaving both original files behind.
        write(legacyGrayFile, legacyGray);
        write(legacyAlphaFile, legacyAlpha);
        SleepImageStore.Mutation removed = store.remove(migrated.generation, migratedId);
        require(removed.result == SleepImageStore.MutationResult.COMMITTED &&
                removed.snapshot.entries.size() == 1, "migrated candidate removal failed");
        File manifest = new File(root, "catalog.v1");
        if (corruptManifest) {
            write(manifest, "generation=corrupt\nlegacy-0\t0\n".getBytes(StandardCharsets.UTF_8));
        } else {
            require(manifest.delete(), "cannot remove manifest fixture");
        }
        SleepImageStore.Snapshot repaired = store.openAndRepair(gray(0x30), alpha(0xff));
        require(repaired.entries.size() == 1 && repaired.entries.get(0).builtIn &&
                !repaired.entries.get(0).id.startsWith("legacy-"),
                "removed legacy candidate resurrected after manifest " +
                        (corruptManifest ? "corruption" : "loss"));
    }

    private static void testBuiltInMetadata(File root) throws Exception {
        require(root.mkdirs(), "cannot create metadata fixtures");
        File falseBuiltIn = new File(root, "false-built-in");
        require(falseBuiltIn.mkdirs(), "cannot create false built-in fixture");
        write(new File(falseBuiltIn, "entry-built-in-fallback.gray"), gray(0x20));
        write(new File(falseBuiltIn, "entry-built-in-fallback.alpha"), alpha(0xff));
        write(new File(falseBuiltIn, "catalog.v1"),
                "generation=5\nbuilt-in-fallback\t0\n".getBytes(StandardCharsets.UTF_8));
        SleepImageStore falseStore = new SleepImageStore(falseBuiltIn, WIDTH, HEIGHT);
        SleepImageStore.Snapshot correctedBuiltIn =
                falseStore.openAndRepair(gray(0x30), alpha(0xff));
        require(correctedBuiltIn.generation == 6 && correctedBuiltIn.entries.size() == 1 &&
                correctedBuiltIn.entries.get(0).builtIn &&
                falseStore.remove(6, SleepImageStore.BUILT_IN_ID).result ==
                        SleepImageStore.MutationResult.PROTECTED &&
                Files.readString(new File(falseBuiltIn, "catalog.v1").toPath())
                        .contains("built-in-fallback\t1"),
                "BUILT_IN_ID=false metadata was not repaired as protected");

        File falseForeign = new File(root, "false-foreign");
        require(falseForeign.mkdirs(), "cannot create false foreign fixture");
        write(new File(falseForeign, "entry-built-in-fallback.gray"), gray(0x20));
        write(new File(falseForeign, "entry-built-in-fallback.alpha"), alpha(0xff));
        write(new File(falseForeign, "entry-foreign.gray"), gray(0x40));
        write(new File(falseForeign, "entry-foreign.alpha"), alpha(0x80));
        write(new File(falseForeign, "catalog.v1"),
                "generation=5\nbuilt-in-fallback\t1\nforeign\t1\n"
                        .getBytes(StandardCharsets.UTF_8));
        SleepImageStore foreignStore = new SleepImageStore(falseForeign, WIDTH, HEIGHT);
        SleepImageStore.Snapshot correctedForeign =
                foreignStore.openAndRepair(gray(0x30), alpha(0xff));
        require(correctedForeign.generation == 6 && correctedForeign.entries.size() == 2 &&
                !correctedForeign.entries.get(1).builtIn &&
                Files.readString(new File(falseForeign, "catalog.v1").toPath())
                        .contains("foreign\t0"),
                "non-built-in=true metadata was not repaired as removable");
        require(foreignStore.remove(6, "foreign").result ==
                SleepImageStore.MutationResult.COMMITTED,
                "repaired non-built-in candidate remained protected");

        File missingBuiltIn = new File(root, "missing-built-in");
        require(missingBuiltIn.mkdirs(), "cannot create missing built-in fixture");
        write(new File(missingBuiltIn, "entry-user.gray"), gray(0x40));
        write(new File(missingBuiltIn, "entry-user.alpha"), alpha(0xff));
        write(new File(missingBuiltIn, "catalog.v1"),
                "generation=7\nuser\t0\t1\n".getBytes(StandardCharsets.UTF_8));
        SleepImageStore missingStore = new SleepImageStore(missingBuiltIn, WIDTH, HEIGHT);
        SleepImageStore.Snapshot restored = missingStore.openAndRepair(gray(0x30), alpha(0xff));
        require(restored.generation == 8 && restored.entries.size() == 2 &&
                        restored.entries.get(0).builtIn,
                "missing built-in fallback was not restored without losing user planes");
    }

    private static void testRotationAndPublication(File root) throws Exception {
        SleepImageStore store = new SleepImageStore(root, WIDTH, HEIGHT);
        SleepImageStore.Snapshot initial = store.openAndRepair(gray(0x20), alpha(0xff));
        require(initial.entries.get(0).rotationEnabled,
                "legacy/default built-in state was not included in rotation");
        SleepImageStore.Mutation added = store.add(initial.generation, gray(0x40), alpha(0xff));
        SleepImageStore.Mutation excluded = store.setBuiltInRotation(
                added.snapshot.generation, false);
        require(excluded.result == SleepImageStore.MutationResult.COMMITTED &&
                        !excluded.snapshot.entries.get(0).rotationEnabled,
                "built-in rotation exclusion was not committed");
        require(store.setBuiltInRotation(added.snapshot.generation, true).result ==
                        SleepImageStore.MutationResult.STALE,
                "stale built-in rotation toggle mutated the catalog");
        String manifest = Files.readString(new File(root, "catalog.v1").toPath());
        require(manifest.contains("built-in-fallback\t1\t0"),
                "built-in rotation exclusion was not durable");

        java.util.ArrayList<SleepImageStore.PublicationEntry> random =
                store.publication(excluded.snapshot, false, null, null);
        require(random.size() == 2 && random.get(0).fallback && !random.get(0).eligible &&
                        !random.get(1).fallback && random.get(1).eligible,
                "excluded built-in remained eligible in ordinary random publication");

        java.util.ArrayList<SleepImageStore.PublicationEntry> missingCover =
                store.publication(excluded.snapshot, true, null, null);
        require(missingCover.size() == 1 && missingCover.get(0).fallback &&
                        missingCover.get(0).eligible,
                "missing KOReader cover did not select the retained fallback");
        java.util.ArrayList<SleepImageStore.PublicationEntry> validCover =
                store.publication(excluded.snapshot, true, gray(0x60), alpha(0xff));
        require(validCover.size() == 2 && validCover.get(0).fallback &&
                        !validCover.get(0).eligible && !validCover.get(1).fallback &&
                        validCover.get(1).eligible &&
                        validCover.get(1).entry.id.equals("koreader-cover"),
                "valid KOReader cover did not replace fallback as the eligible source");

        SleepImageStore.Mutation removed = store.remove(excluded.snapshot.generation,
                excluded.snapshot.entries.get(1).id);
        java.util.ArrayList<SleepImageStore.PublicationEntry> fallbackOnly =
                store.publication(removed.snapshot, false, null, null);
        require(fallbackOnly.size() == 1 && fallbackOnly.get(0).fallback &&
                        fallbackOnly.get(0).eligible,
                "empty ordinary rotation did not deterministically retain fallback");
        SleepImageStore recreated = new SleepImageStore(root, WIDTH, HEIGHT);
        require(!recreated.read().entries.get(0).rotationEnabled,
                "process recreation lost built-in rotation exclusion");
    }

    private static void testStableCover(File root) throws Exception {
        require(root.mkdirs(), "cannot create stable-cover fixture");
        require(SleepImageStore.readStableCover(new File(root, "missing.png")) == null,
                "missing cover was accepted");
        write(new File(root, "empty.png"), new byte[0]);
        require(SleepImageStore.readStableCover(new File(root, "empty.png")) == null,
                "empty cover was accepted");

        byte[] bytes = { 1, 2, 3 };
        require(Arrays.equals(bytes, SleepImageStore.readStableCover(
                        stableSource(bytes, bytes.length, 7, false))),
                "stable bounded cover was rejected");
        require(SleepImageStore.readStableCover(
                        stableSource(bytes, SleepImageTransforms.MAX_ENCODED_BYTES + 1, 7, false)) ==
                        null,
                "oversized cover was accepted");
        require(SleepImageStore.readStableCover(stableSource(bytes, bytes.length, 7, true)) == null,
                "unreadable cover was accepted");
        SleepImageStore.StableCoverSource changing = new SleepImageStore.StableCoverSource() {
            int lengthCalls;
            @Override public long length() { return ++lengthCalls == 1 ? bytes.length : bytes.length + 1; }
            @Override public long lastModified() { return 7; }
            @Override public InputStream open() { return new ByteArrayInputStream(bytes); }
        };
        require(SleepImageStore.readStableCover(changing) == null,
                "cover changed during publication was accepted");
        require(SleepImageStore.readStableCover(
                        stableSource(new byte[] { 1, 2, 3, 4 }, 3, 7, false)) == null,
                "cover longer than its stable stat was accepted");
    }

    private static void testPanelOrientation(File root) throws Exception {
        byte[] presentationGray = {
                0x10, 0x20,
                0x30, 0x40,
                0x50, 0x60,
        };
        byte[] presentationAlpha = {
                1, 2,
                3, 4,
                5, 6,
        };
        SleepImageTransforms.Planes panel = SleepImageTransforms.presentationToPanel(
                new SleepImageTransforms.Planes(presentationGray, presentationAlpha), 2, 3);
        require(Arrays.equals(panel.gray, new byte[] {
                        0x20, 0x40, 0x60,
                        0x10, 0x30, 0x50,
                }) && Arrays.equals(panel.alpha, new byte[] {
                        2, 4, 6,
                        1, 3, 5,
                }), "portrait presentation did not receive the fixed panel ROTATION_270");

        int[] preview = SleepImageTransforms.panelToPresentationPixels(
                panel.gray, panel.alpha, 3, 2, 2, 3);
        for (int index = 0; index < preview.length; ++index) {
            require(((preview[index] >>> 24) & 0xff) == (presentationAlpha[index] & 0xff) &&
                            ((preview[index] >>> 16) & 0xff) ==
                                    (presentationGray[index] & 0xff),
                    "panel preview was not the exact inverse portrait mapping");
        }

        int[] sourcePixels = {
                0xff101010, 0xff202020,
                0xff303030, 0xff404040,
                0xff505050, 0xff606060,
        };
        SleepImageTransforms.Planes rendered = SleepImageTransforms.renderForPanel(
                sourcePixels, 2, 3, 0, SleepImageTransforms.Framing.FIT,
                0.5f, 0.5f, 3, 2);
        require(Arrays.equals(rendered.gray, panel.gray),
                "import path did not frame in portrait before panel rotation");

        SleepImageStore store = new SleepImageStore(root, 3, 2);
        SleepImageStore.Snapshot snapshot = store.openAndRepair(
                panel.gray.clone(), panel.alpha.clone());
        byte[] beforeGray = snapshot.entries.get(0).gray.clone();
        byte[] beforeAlpha = snapshot.entries.get(0).alpha.clone();
        int[] thumbnail = store.summaries(snapshot, 2, 3).get(0).thumbnail;
        require(Arrays.equals(snapshot.entries.get(0).gray, beforeGray) &&
                        Arrays.equals(snapshot.entries.get(0).alpha, beforeAlpha),
                "portrait thumbnail generation rewrote stored panel planes");
        require(Arrays.equals(thumbnail, preview),
                "catalog thumbnail did not use the inverse panel mapping");
    }

    private static SleepImageStore.StableCoverSource stableSource(byte[] bytes, long length,
            long modified, boolean unreadable) {
        return new SleepImageStore.StableCoverSource() {
            @Override public long length() { return length; }
            @Override public long lastModified() { return modified; }
            @Override public InputStream open() throws IOException {
                if (unreadable) throw new IOException("injected");
                return new ByteArrayInputStream(bytes);
            }
        };
    }

    private static void testCatalog(File root) throws Exception {
        SleepImageStore store = new SleepImageStore(root, WIDTH, HEIGHT);
        SleepImageStore.Snapshot one = store.openAndRepair(gray(0x20), alpha(0xff));
        require(one.entries.size() == 1 && one.entries.get(0).builtIn, "fallback repair failed");
        SleepImageStore.Mutation two = store.add(one.generation, gray(0x30), alpha(0x80));
        require(two.result == SleepImageStore.MutationResult.COMMITTED &&
                two.snapshot.entries.size() == 2, "two-entry commit failed");
        SleepImageStore.Mutation stale = store.remove(one.generation,
                two.snapshot.entries.get(1).id);
        require(stale.result == SleepImageStore.MutationResult.STALE &&
                stale.snapshot.entries.size() == 2, "stale removal mutated catalog");
        require(store.remove(two.snapshot.generation, SleepImageStore.BUILT_IN_ID).result ==
                SleepImageStore.MutationResult.PROTECTED, "built-in fallback was removable");

        SleepImageStore.Snapshot current = two.snapshot;
        for (int value = 4; value <= 9; ++value) {
            SleepImageStore.Mutation added = store.add(current.generation, gray(value << 4),
                    alpha(0xff));
            require(added.result == SleepImageStore.MutationResult.COMMITTED, "bounded add failed");
            current = added.snapshot;
        }
        require(current.entries.size() == 8, "eight-entry catalog not retained");
        SleepImageStore.Mutation pruned = store.add(current.generation, gray(0xa0), alpha(0xff));
        require(pruned.result == SleepImageStore.MutationResult.COMMITTED &&
                pruned.snapshot.entries.size() == 8 && pruned.snapshot.entries.get(0).builtIn,
                "ninth import did not prune oldest removable entry");
        File[] retainedPlanes = root.listFiles((directory, name) -> name.startsWith("entry-") &&
                (name.endsWith(".gray") || name.endsWith(".alpha")));
        require(retainedPlanes != null && retainedPlanes.length == 16,
                "pruned entry planes were retained");
        SleepImageStore recreated = new SleepImageStore(root, WIDTH, HEIGHT);
        require(recreated.read().generation == pruned.snapshot.generation &&
                recreated.read().entries.size() == 8, "process recreation lost catalog");
        require(recreated.summaries(recreated.read(), 9, 7).get(0).thumbnail.length == 63,
                "thumbnail was not derived from final planes");
    }

    private static void testRecoveryAndMigration(File root) throws Exception {
        require(root.mkdirs(), "cannot create migration fixture");
        byte[] legacyGray = gray(0x50);
        write(new File(root, "candidate-old.gray"), legacyGray);
        write(new File(root, "candidate-old.alpha"), alpha(0x44));
        write(new File(root, "candidate-partial.gray"), new byte[] { 1, 2, 3 });
        SleepImageStore store = new SleepImageStore(root, WIDTH, HEIGHT);
        SleepImageStore.Snapshot migrated = store.openAndRepair(gray(0x20), alpha(0xff));
        require(migrated.entries.size() == 2 && migrated.entries.get(0).builtIn,
                "legacy catalog migration did not retain fallback and valid entry");
        require(Arrays.equals(legacyGray, migrated.entries.get(1).gray),
                "legacy migration changed gray-plane bytes");

        File corrupt = new File(root, "corrupt");
        require(corrupt.mkdirs(), "cannot create corrupt fixture");
        write(new File(corrupt, "catalog.v1"),
                "generation=9\nbad\t0\n".getBytes(StandardCharsets.UTF_8));
        SleepImageStore repaired = new SleepImageStore(corrupt, WIDTH, HEIGHT);
        SleepImageStore.Snapshot result = repaired.openAndRepair(gray(0x60), alpha(0xff));
        require(result.entries.size() == 1 && result.entries.get(0).builtIn &&
                result.generation == 10, "corrupt store did not repair atomically");

        File corruptFallback = new File(root, "corrupt-fallback");
        require(corruptFallback.mkdirs(), "cannot create corrupt fallback fixture");
        write(new File(corruptFallback, "entry-built-in-fallback.gray"), new byte[] { 1 });
        write(new File(corruptFallback, "entry-built-in-fallback.alpha"), new byte[] { 1 });
        write(new File(corruptFallback, "catalog.v1"),
                "generation=3\nbuilt-in-fallback\t1\n".getBytes(StandardCharsets.UTF_8));
        SleepImageStore replaced = new SleepImageStore(corruptFallback, WIDTH, HEIGHT);
        SleepImageStore.Snapshot replacement = replaced.openAndRepair(gray(0x40), alpha(0xff));
        require(replacement.entries.size() == 1 && replacement.entries.get(0).builtIn &&
                (replacement.entries.get(0).gray[0] & 0xff) == 0x40,
                "corrupt fallback files blocked repair");
    }

    private static void testAtomicFailure(File root) throws Exception {
        SleepImageStore baseline = new SleepImageStore(root, WIDTH, HEIGHT);
        SleepImageStore.Snapshot initial = baseline.openAndRepair(gray(0x20), alpha(0xff));
        SleepImageStore failing = new SleepImageStore(root, WIDTH, HEIGHT, operation -> {
            if (operation.equals("manifest")) throw new java.io.IOException("injected");
        });
        SleepImageStore.Mutation result = failing.add(initial.generation, gray(0x70), alpha(0xff));
        require(result.result == SleepImageStore.MutationResult.IO_FAILURE,
                "atomic commit failure was not reported");
        SleepImageStore.Snapshot unchanged = baseline.read();
        require(unchanged.generation == initial.generation && unchanged.entries.size() == 1,
                "failed commit published a partial generation");
    }

    private static void testFinalCandidate(File root) throws Exception {
        require(root.mkdirs(), "cannot create final-candidate fixture");
        write(new File(root, "entry-only.gray"), gray(0x20));
        write(new File(root, "entry-only.alpha"), alpha(0xff));
        write(new File(root, "catalog.v1"),
                "generation=1\nonly\t0\n".getBytes(StandardCharsets.UTF_8));
        SleepImageStore store = new SleepImageStore(root, WIDTH, HEIGHT);
        require(store.remove(1, "only").result == SleepImageStore.MutationResult.FINAL_CANDIDATE,
                "final candidate removal was accepted");
    }

    private static void testTransforms() {
        SleepImageTransforms.Planes fallback = SleepImageTransforms.builtInFallback(181, 134);
        require(fallback.gray.length == BYTES && fallback.alpha.length == BYTES,
                "built-in fallback geometry is wrong");
        for (byte value : fallback.gray) {
            require((value & 0x0f) == 0, "built-in fallback is not 16-level gray");
        }
        for (byte value : fallback.alpha) {
            require((value & 0xff) == 0xff, "built-in fallback is not opaque");
        }
        require(SleepImageTransforms.plan(32L * 1024 * 1024, 16384, 4096, false)
                .sampleSize >= 4, "decode was not downsampled before working allocation");
        reject(() -> SleepImageTransforms.plan(32L * 1024 * 1024 + 1, 1, 1, false));
        reject(() -> SleepImageTransforms.plan(1, 16385, 1, false));
        reject(() -> SleepImageTransforms.plan(1, 9000, 9000, false));
        reject(() -> SleepImageTransforms.plan(1, 10, 10, true));
        for (int orientation = 1; orientation <= 8; ++orientation) {
            int width = orientation >= 5 ? 3 : 2;
            int height = orientation >= 5 ? 2 : 3;
            int index = SleepImageTransforms.exifSourceIndex(0, 0, 2, 3, orientation);
            require(index >= 0 && index < 6, "EXIF orientation escaped source bounds");
        }
        SleepImageTransforms.Crop left =
                SleepImageTransforms.crop(400, 200, -1f, -1f, 134, 181);
        SleepImageTransforms.Crop right =
                SleepImageTransforms.crop(400, 200, 2f, 2f, 134, 181);
        require(left.left == 0 && right.left + right.width == 400 &&
                Math.abs((long) left.width * 181 - (long) left.height * 134) <= 181,
                "aspect-locked crop was not boundary clamped");
        int[] transparentRed = { 0x00ff0000, 0xffff0000 };
        SleepImageTransforms.Planes fit = SleepImageTransforms.render(transparentRed, 2, 1, 0,
                SleepImageTransforms.Framing.FIT, 0.5f, 0.5f, 4, 4);
        require(fit.gray.length == 16 && fit.alpha.length == 16, "final geometry is wrong");
        require((fit.gray[0] & 0xff) == 0xf0 && (fit.alpha[0] & 0xff) == 0,
                "fit padding is not white-gray with zero alpha");
        for (byte value : fit.gray) require((value & 0x0f) == 0, "gray is not 16-level");
        for (int rotation : new int[] { 0, 90, 180, 270 }) {
            SleepImageTransforms.render(transparentRed, 2, 1, rotation,
                    SleepImageTransforms.Framing.CROP, 0.5f, 0.5f, 4, 4);
        }
    }

    private static byte[] gray(int value) {
        byte[] bytes = new byte[BYTES];
        Arrays.fill(bytes, (byte) value);
        return bytes;
    }

    private static byte[] alpha(int value) {
        byte[] bytes = new byte[BYTES];
        Arrays.fill(bytes, (byte) value);
        return bytes;
    }

    private static void write(File file, byte[] bytes) throws Exception {
        try (FileOutputStream output = new FileOutputStream(file)) { output.write(bytes); }
    }

    private static void reject(ThrowingRunnable action) {
        try { action.run(); throw new AssertionError("invalid input accepted"); }
        catch (IllegalArgumentException expected) { }
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void delete(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) delete(child);
        file.delete();
    }

    private interface ThrowingRunnable { void run(); }
}

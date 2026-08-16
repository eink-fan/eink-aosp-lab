package org.neo2.controls;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Lock-external, generation-based durable catalog with fail-closed validation. */
final class SleepImageStore {
    static final int MAXIMUM_ENTRIES = 8;
    static final String BUILT_IN_ID = "built-in-fallback";
    private static final String MANIFEST = "catalog.v1";
    private static final String LEGACY_RETIRED = "legacy-pairs-retired.v1";
    private static final String ENTRY_PREFIX = "entry-";

    interface FaultInjector {
        void before(String operation) throws IOException;
    }

    static final FaultInjector NO_FAULTS = operation -> { };

    static final class Entry {
        final String id;
        final boolean builtIn;
        final boolean rotationEnabled;
        final byte[] gray;
        final byte[] alpha;

        Entry(String id, boolean builtIn, byte[] gray, byte[] alpha) {
            this(id, builtIn, true, gray, alpha);
        }

        Entry(String id, boolean builtIn, boolean rotationEnabled, byte[] gray, byte[] alpha) {
            this.id = id;
            this.builtIn = builtIn;
            this.rotationEnabled = builtIn ? rotationEnabled : true;
            this.gray = gray;
            this.alpha = alpha;
        }
    }

    static final class Summary {
        final String id;
        final boolean builtIn;
        final boolean rotationEnabled;
        final int[] thumbnail;

        Summary(String id, boolean builtIn, boolean rotationEnabled, int[] thumbnail) {
            this.id = id;
            this.builtIn = builtIn;
            this.rotationEnabled = rotationEnabled;
            this.thumbnail = thumbnail;
        }
    }

    static final class PublicationEntry {
        final Entry entry;
        final boolean fallback;
        final boolean eligible;

        PublicationEntry(Entry entry, boolean fallback, boolean eligible) {
            this.entry = entry;
            this.fallback = fallback;
            this.eligible = eligible;
        }
    }

    interface StableCoverSource {
        long length();
        long lastModified();
        InputStream open() throws IOException;
    }

    static final class Snapshot {
        final long generation;
        final ArrayList<Entry> entries;
        final boolean metadataConflict;

        Snapshot(long generation, ArrayList<Entry> entries) {
            this(generation, entries, false);
        }

        Snapshot(long generation, ArrayList<Entry> entries, boolean metadataConflict) {
            this.generation = generation;
            this.entries = entries;
            this.metadataConflict = metadataConflict;
        }
    }

    enum MutationResult { COMMITTED, STALE, FINAL_CANDIDATE, PROTECTED, IO_FAILURE }

    static final class Mutation {
        final MutationResult result;
        final Snapshot snapshot;

        Mutation(MutationResult result, Snapshot snapshot) {
            this.result = result;
            this.snapshot = snapshot;
        }
    }

    private final File root;
    private final int width;
    private final int height;
    private final int bytes;
    private final FaultInjector faults;

    SleepImageStore(File root, int width, int height) {
        this(root, width, height, NO_FAULTS);
    }

    SleepImageStore(File root, int width, int height, FaultInjector faults) {
        this.root = root;
        this.width = width;
        this.height = height;
        this.bytes = Math.multiplyExact(width, height);
        this.faults = faults;
    }

    Snapshot openAndRepair(byte[] fallbackGray, byte[] fallbackAlpha) throws IOException {
        validatePlanes(fallbackGray, fallbackAlpha);
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("cannot create store");
        recoverPendingLegacyRetirement();
        Snapshot snapshot = read();
        if (!snapshot.entries.isEmpty() && !snapshot.metadataConflict &&
                findBuiltIn(snapshot.entries) >= 0) return snapshot;
        if (!snapshot.entries.isEmpty()) {
            ArrayList<Entry> repaired = new ArrayList<>(snapshot.entries);
            if (findBuiltIn(repaired) < 0) {
                while (repaired.size() >= MAXIMUM_ENTRIES) {
                    int prune = firstRemovable(repaired);
                    if (prune < 0) throw new IOException("cannot restore built-in fallback");
                    repaired.remove(prune);
                }
                repaired.add(0, new Entry(BUILT_IN_ID, true,
                        fallbackGray.clone(), fallbackAlpha.clone()));
            }
            return commitEntries(snapshot.generation, repaired);
        }

        boolean legacyPresent = hasLegacyFiles();
        ArrayList<Entry> migrated = isLegacyRetired() ? new ArrayList<>() : legacyEntries();
        if (migrated.isEmpty()) {
            migrated.add(new Entry(BUILT_IN_ID, true,
                    fallbackGray.clone(), fallbackAlpha.clone()));
        } else if (migrated.size() < MAXIMUM_ENTRIES) {
            migrated.add(0, new Entry(BUILT_IN_ID, true,
                    fallbackGray.clone(), fallbackAlpha.clone()));
        }
        Snapshot committed = commitEntries(Math.max(0, snapshot.generation), migrated,
                legacyPresent && !isLegacyRetired());
        return committed;
    }

    Snapshot read() {
        return readManifest(new File(root, MANIFEST));
    }

    private Snapshot readManifest(File manifest) {
        if (!manifest.isFile()) return new Snapshot(0, new ArrayList<>());
        long generation = 0;
        ArrayList<Entry> entries = new ArrayList<>();
        Set<String> ids = new HashSet<>();
        boolean metadataConflict = false;
        try (BufferedReader reader = new BufferedReader(new FileReader(manifest))) {
            String header = reader.readLine();
            if (header == null || !header.startsWith("generation=")) {
                return new Snapshot(0, new ArrayList<>());
            }
            generation = Long.parseLong(header.substring("generation=".length()));
            if (generation <= 0) return new Snapshot(0, new ArrayList<>());
            String line;
            while ((line = reader.readLine()) != null && entries.size() < MAXIMUM_ENTRIES) {
                String[] fields = line.split("\\t", -1);
                if ((fields.length != 2 && fields.length != 3) ||
                        !safeId(fields[0]) || !ids.add(fields[0]) ||
                        !(fields[1].equals("0") || fields[1].equals("1"))) continue;
                boolean declaredBuiltIn = fields[1].equals("1");
                boolean authoritativeBuiltIn = fields[0].equals(BUILT_IN_ID);
                if (declaredBuiltIn != authoritativeBuiltIn) metadataConflict = true;
                boolean rotationEnabled = fields.length == 2 || fields[2].equals("1");
                if (fields.length == 3 &&
                        !(fields[2].equals("0") || fields[2].equals("1"))) continue;
                if (!authoritativeBuiltIn && !rotationEnabled) metadataConflict = true;
                Entry entry = loadEntry(fields[0], authoritativeBuiltIn, rotationEnabled);
                if (entry != null) entries.add(entry);
            }
        } catch (IOException | NumberFormatException ignored) {
            return new Snapshot(0, new ArrayList<>());
        }
        return new Snapshot(generation, entries, metadataConflict);
    }

    Mutation add(long expectedGeneration, byte[] gray, byte[] alpha) {
        try {
            validatePlanes(gray, alpha);
            Snapshot current = read();
            if (current.generation != expectedGeneration) {
                return new Mutation(MutationResult.STALE, current);
            }
            ArrayList<Entry> next = new ArrayList<>(current.entries);
            while (next.size() >= MAXIMUM_ENTRIES) {
                int prune = firstRemovable(next);
                if (prune < 0) return new Mutation(MutationResult.IO_FAILURE, current);
                next.remove(prune);
            }
            String id = "g" + (current.generation + 1) + "-" + Long.toUnsignedString(System.nanoTime());
            next.add(new Entry(id, false, gray.clone(), alpha.clone()));
            return new Mutation(MutationResult.COMMITTED,
                    commitEntries(current.generation, next));
        } catch (IOException | RuntimeException failure) {
            return new Mutation(MutationResult.IO_FAILURE, read());
        }
    }

    Mutation remove(long expectedGeneration, String id) {
        Snapshot current = read();
        if (current.generation != expectedGeneration) {
            return new Mutation(MutationResult.STALE, current);
        }
        int selected = -1;
        for (int index = 0; index < current.entries.size(); ++index) {
            if (current.entries.get(index).id.equals(id)) selected = index;
        }
        if (selected < 0) return new Mutation(MutationResult.STALE, current);
        if (current.entries.get(selected).id.equals(BUILT_IN_ID)) {
            return new Mutation(MutationResult.PROTECTED, current);
        }
        if (current.entries.size() <= 1) {
            return new Mutation(MutationResult.FINAL_CANDIDATE, current);
        }
        ArrayList<Entry> next = new ArrayList<>(current.entries);
        next.remove(selected);
        try {
            return new Mutation(MutationResult.COMMITTED,
                    commitEntries(current.generation, next));
        } catch (IOException failure) {
            return new Mutation(MutationResult.IO_FAILURE, read());
        }
    }

    Mutation setBuiltInRotation(long expectedGeneration, boolean enabled) {
        Snapshot current = read();
        if (current.generation != expectedGeneration) {
            return new Mutation(MutationResult.STALE, current);
        }
        int selected = findBuiltIn(current.entries);
        if (selected < 0) return new Mutation(MutationResult.IO_FAILURE, current);
        ArrayList<Entry> next = new ArrayList<>(current.entries);
        Entry fallback = next.get(selected);
        next.set(selected, new Entry(fallback.id, true, enabled,
                fallback.gray, fallback.alpha));
        try {
            return new Mutation(MutationResult.COMMITTED,
                    commitEntries(current.generation, next));
        } catch (IOException failure) {
            return new Mutation(MutationResult.IO_FAILURE, read());
        }
    }

    ArrayList<Summary> summaries(Snapshot snapshot, int thumbnailWidth, int thumbnailHeight) {
        ArrayList<Summary> result = new ArrayList<>();
        for (Entry entry : snapshot.entries) {
            int[] thumbnail = SleepImageTransforms.panelToPresentationPixels(
                    entry.gray, entry.alpha, width, height, thumbnailWidth, thumbnailHeight);
            result.add(new Summary(entry.id, entry.builtIn, entry.rotationEnabled, thumbnail));
        }
        return result;
    }

    ArrayList<PublicationEntry> publication(Snapshot snapshot, boolean coverMode,
            byte[] coverGray, byte[] coverAlpha) {
        ArrayList<PublicationEntry> result = new ArrayList<>();
        int fallbackIndex = findBuiltIn(snapshot.entries);
        if (fallbackIndex < 0) return result;
        Entry fallback = snapshot.entries.get(fallbackIndex);
        if (coverMode) {
            if (validPlanes(coverGray, coverAlpha)) {
                result.add(new PublicationEntry(fallback, true, false));
                result.add(new PublicationEntry(new Entry("koreader-cover", false,
                        coverGray, coverAlpha), false, true));
            } else {
                result.add(new PublicationEntry(fallback, true, true));
            }
            return result;
        }
        boolean anyEligible = false;
        for (Entry entry : snapshot.entries) {
            boolean eligible = !entry.builtIn || entry.rotationEnabled;
            result.add(new PublicationEntry(entry, entry.builtIn, eligible));
            anyEligible |= eligible;
        }
        if (!anyEligible) {
            result.set(fallbackIndex, new PublicationEntry(fallback, true, true));
        }
        return result;
    }

    static byte[] readStableCover(File file) {
        if (file == null || !file.isFile()) return null;
        return readStableCover(new StableCoverSource() {
            @Override public long length() { return file.length(); }
            @Override public long lastModified() { return file.lastModified(); }
            @Override public InputStream open() throws IOException {
                return new FileInputStream(file);
            }
        });
    }

    static byte[] readStableCover(StableCoverSource source) {
        if (source == null) return null;
        long beforeLength = source.length();
        long beforeModified = source.lastModified();
        if (beforeLength <= 0 || beforeLength > SleepImageTransforms.MAX_ENCODED_BYTES) return null;
        try (InputStream input = source.open()) {
            byte[] encoded = input.readNBytes(Math.toIntExact(beforeLength) + 1);
            if (encoded.length != beforeLength || input.read() != -1 ||
                    source.length() != beforeLength ||
                    source.lastModified() != beforeModified) return null;
            return encoded;
        } catch (IOException | ArithmeticException ignored) {
            return null;
        }
    }

    private Snapshot commitEntries(long oldGeneration, List<Entry> next) throws IOException {
        return commitEntries(oldGeneration, next, false);
    }

    private Snapshot commitEntries(long oldGeneration, List<Entry> next,
            boolean retireLegacy) throws IOException {
        if (next.isEmpty() || next.size() > MAXIMUM_ENTRIES) throw new IOException("invalid count");
        long generation = oldGeneration + 1;
        for (Entry entry : next) {
            validatePlanes(entry.gray, entry.alpha);
            writeEntry(entry);
        }
        if (retireLegacy) publishPendingLegacyRetirement(generation, next);
        faults.before("manifest");
        publishManifest(generation, next);
        if (retireLegacy) completeLegacyRetirement();
        Snapshot committed = read();
        pruneUnreferenced(committed.entries);
        return committed;
    }

    private void publishManifest(long generation, List<Entry> entries) throws IOException {
        File temporary = new File(root, ".catalog.v1." + generation + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(manifestBytes(generation, entries));
            output.getFD().sync();
        }
        File target = new File(root, MANIFEST);
        moveAtomic(temporary, target);
    }

    private void writeEntry(Entry entry) throws IOException {
        if (!safeId(entry.id)) throw new IOException("unsafe candidate id");
        File gray = grayFile(entry.id);
        File alpha = alphaFile(entry.id);
        if (gray.isFile() && alpha.isFile()) {
            try {
                if (Arrays.equals(readExact(gray), entry.gray) &&
                        Arrays.equals(readExact(alpha), entry.alpha)) return;
            } catch (IOException corruptExistingEntry) {
                // The replacement remains hidden until the next manifest is atomic.
            }
        }
        writeAtomic(gray, entry.gray, "gray");
        try {
            writeAtomic(alpha, entry.alpha, "alpha");
        } catch (IOException failure) {
            gray.delete();
            throw failure;
        }
    }

    private void writeAtomic(File target, byte[] content, String operation) throws IOException {
        faults.before(operation);
        File temporary = new File(root, "." + target.getName() + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(content);
            output.getFD().sync();
        }
        moveAtomic(temporary, target);
    }

    private Entry loadEntry(String id, boolean builtIn, boolean rotationEnabled) {
        try {
            byte[] gray = readExact(grayFile(id));
            byte[] alpha = readExact(alphaFile(id));
            validatePlanes(gray, alpha);
            return new Entry(id, builtIn, rotationEnabled, gray, alpha);
        } catch (IOException ignored) {
            return null;
        }
    }

    private ArrayList<Entry> legacyEntries() {
        File[] grayFiles = root.listFiles((directory, name) ->
                name.startsWith("candidate-") && name.endsWith(".gray"));
        ArrayList<Entry> result = new ArrayList<>();
        if (grayFiles == null) return result;
        Arrays.sort(grayFiles, Comparator.comparing(File::getName));
        for (File grayFile : grayFiles) {
            if (result.size() >= MAXIMUM_ENTRIES - 1) break;
            String stem = grayFile.getName().substring(0,
                    grayFile.getName().length() - ".gray".length());
            try {
                byte[] gray = readExact(grayFile);
                File alphaFile = new File(root, stem + ".alpha");
                byte[] alpha;
                if (alphaFile.isFile()) {
                    alpha = readExact(alphaFile);
                } else {
                    alpha = new byte[bytes];
                    Arrays.fill(alpha, (byte) 0xff);
                }
                validatePlanes(gray, alpha);
                result.add(new Entry("legacy-" + result.size(), false, gray, alpha));
            } catch (IOException ignored) { }
        }
        return result;
    }

    private boolean hasLegacyFiles() {
        File[] files = root.listFiles((directory, name) -> name.startsWith("candidate-") &&
                (name.endsWith(".gray") || name.endsWith(".alpha")));
        return files != null && files.length > 0;
    }

    private boolean isLegacyRetired() {
        File marker = new File(root, LEGACY_RETIRED);
        if (!marker.isFile() || marker.length() != 8) return false;
        try (FileInputStream input = new FileInputStream(marker)) {
            return Arrays.equals(input.readNBytes(9), "retired\n".getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
            return false;
        }
    }

    // The marker is first published as a replayable transaction containing the
    // exact generation manifest. Only after that manifest is durable is the
    // same marker atomically replaced by the monotonic retired state.
    private void publishPendingLegacyRetirement(long generation, List<Entry> entries)
            throws IOException {
        faults.before("legacy-retirement");
        byte[] manifest = manifestBytes(generation, entries);
        byte[] pending = new byte["pending\n".length() + manifest.length];
        System.arraycopy("pending\n".getBytes(StandardCharsets.UTF_8), 0, pending, 0,
                "pending\n".length());
        System.arraycopy(manifest, 0, pending, "pending\n".length(), manifest.length);
        publishLegacyMarker(pending);
    }

    private void completeLegacyRetirement() throws IOException {
        publishLegacyMarker("retired\n".getBytes(StandardCharsets.UTF_8));
        deleteLegacyFiles();
    }

    private void publishLegacyMarker(byte[] content) throws IOException {
        File marker = new File(root, LEGACY_RETIRED);
        File temporary = new File(root, "." + LEGACY_RETIRED + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(content);
            output.getFD().sync();
        }
        moveAtomic(temporary, marker);
    }

    private void recoverPendingLegacyRetirement() throws IOException {
        File marker = new File(root, LEGACY_RETIRED);
        if (!marker.isFile() || marker.length() <= "pending\n".length() ||
                marker.length() > 2048) return;
        byte[] content = Files.readAllBytes(marker.toPath());
        byte[] prefix = "pending\n".getBytes(StandardCharsets.UTF_8);
        if (!startsWith(content, prefix)) return;
        File recovery = new File(root, ".catalog.v1.recovery");
        try (FileOutputStream output = new FileOutputStream(recovery)) {
            output.write(content, prefix.length, content.length - prefix.length);
            output.getFD().sync();
        }
        Snapshot pending = readManifest(recovery);
        recovery.delete();
        if (pending.entries.isEmpty() || pending.metadataConflict) {
            throw new IOException("invalid pending legacy retirement");
        }
        Snapshot current = read();
        if (current.entries.isEmpty() || current.generation <= pending.generation) {
            publishManifest(pending.generation, pending.entries);
        }
        completeLegacyRetirement();
    }

    private byte[] manifestBytes(long generation, List<Entry> entries) {
        StringBuilder manifest = new StringBuilder("generation=").append(generation).append('\n');
        for (Entry entry : entries) {
            manifest.append(entry.id).append('\t')
                    .append(entry.id.equals(BUILT_IN_ID) ? '1' : '0').append('\t')
                    .append(entry.id.equals(BUILT_IN_ID) && !entry.rotationEnabled ? '0' : '1')
                    .append('\n');
        }
        return manifest.toString().getBytes(StandardCharsets.UTF_8);
    }

    private static boolean startsWith(byte[] value, byte[] prefix) {
        if (value.length < prefix.length) return false;
        for (int index = 0; index < prefix.length; ++index) {
            if (value[index] != prefix[index]) return false;
        }
        return true;
    }

    private void deleteLegacyFiles() {
        File[] files = root.listFiles((directory, name) -> name.startsWith("candidate-") &&
                (name.endsWith(".gray") || name.endsWith(".alpha")));
        if (files == null) return;
        for (File file : files) file.delete();
    }

    private byte[] readExact(File file) throws IOException {
        if (!file.isFile() || file.length() != bytes) throw new IOException("invalid plane size");
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] value = input.readNBytes(bytes + 1);
            if (value.length != bytes) throw new IOException("short plane");
            return value;
        }
    }

    private void pruneUnreferenced(List<Entry> retained) {
        Set<String> names = new HashSet<>();
        for (Entry entry : retained) {
            names.add(grayFile(entry.id).getName());
            names.add(alphaFile(entry.id).getName());
        }
        File[] files = root.listFiles((directory, name) -> name.startsWith(ENTRY_PREFIX) &&
                (name.endsWith(".gray") || name.endsWith(".alpha")));
        if (files == null) return;
        for (File file : files) if (!names.contains(file.getName())) file.delete();
    }

    private static void moveAtomic(File source, File target) throws IOException {
        Files.move(source.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING);
    }

    private boolean validPlanes(byte[] gray, byte[] alpha) {
        try {
            validatePlanes(gray, alpha);
            return true;
        } catch (IOException ignored) {
            return false;
        }
    }

    private void validatePlanes(byte[] gray, byte[] alpha) throws IOException {
        if (gray == null || alpha == null || gray.length != bytes || alpha.length != bytes) {
            throw new IOException("invalid planes");
        }
        for (byte value : gray) if ((value & 0x0f) != 0) throw new IOException("invalid gray");
    }

    private File grayFile(String id) { return new File(root, ENTRY_PREFIX + id + ".gray"); }
    private File alphaFile(String id) { return new File(root, ENTRY_PREFIX + id + ".alpha"); }

    private static int firstRemovable(List<Entry> entries) {
        for (int index = 0; index < entries.size(); ++index) {
            if (!entries.get(index).id.equals(BUILT_IN_ID)) return index;
        }
        return -1;
    }

    private static int findBuiltIn(List<Entry> entries) {
        for (int index = 0; index < entries.size(); ++index) {
            if (entries.get(index).id.equals(BUILT_IN_ID)) return index;
        }
        return -1;
    }

    private static boolean safeId(String id) {
        return id != null && id.matches("[A-Za-z0-9][A-Za-z0-9._-]{0,95}");
    }
}

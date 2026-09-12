package org.neo2.controls;

import android.app.Service;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import android.os.Binder;
import android.os.FileObserver;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.Process;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SharedMemory;
import android.os.SystemProperties;
import android.system.ErrnoException;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;

/** Direct-boot owner of the private, generation-based preconverted sleep-image store. */
public final class SleepImageCatalogService extends Service {
    static final EinkDeviceProfile DEVICE_PROFILE = EinkDeviceProfile.require(
            SystemProperties.get("ro.neo2.eink.device_variant", "neo2"));
    public static final int PRESENTATION_WIDTH = DEVICE_PROFILE.presentationWidth;
    public static final int PRESENTATION_HEIGHT = DEVICE_PROFILE.presentationHeight;
    public static final int WIDTH = DEVICE_PROFILE.panelWidth;
    public static final int HEIGHT = DEVICE_PROFILE.panelHeight;
    public static final int BYTES = WIDTH * HEIGHT;
    public static final int THUMBNAIL_WIDTH = 134;
    public static final int THUMBNAIL_HEIGHT = Math.max(1, Math.round(
            (float) THUMBNAIL_WIDTH * PRESENTATION_HEIGHT / PRESENTATION_WIDTH));
    public static final int MAXIMUM_ENTRIES = 8;
    public static final String ACTION_MANAGE = "org.neo2.controls.action.MANAGE_SLEEP_IMAGES";
    private static final String KOREADER_COVER_DIRECTORY =
            "/storage/emulated/0/screensaver";
    private static final String KOREADER_COVER_NAME = "current.png";
    private static final String KOREADER_STORAGE_ROOT = "/storage/emulated/0";
    private static final int COVER_OBSERVER_EVENTS = FileObserver.CLOSE_WRITE |
            FileObserver.CREATE | FileObserver.DELETE | FileObserver.MOVED_FROM |
            FileObserver.MOVED_TO;
    private static final long COVER_REFRESH_DELAY_MILLIS = 250;
    private static final String PROVIDER_DESCRIPTOR = "org.neo2.controls.ISleepImageCatalog";
    private static final int REQUEST_CATALOG = IBinder.FIRST_CALL_TRANSACTION;
    private static final int PROVIDER_PROTOCOL_VERSION = DEVICE_PROFILE.hasColorCfa ? 5 : 4;
    private static final int RESULT_OK = 0;
    private static final int RESULT_UNAVAILABLE = 1;
    private static final int RESULT_EMPTY = 2;
    private static final int REASON_NONE = 0;
    private static final int REASON_STORE_IO = 1;
    private static final int REASON_SHARED_MEMORY = 2;
    private static final int REASON_NO_VALID_CANDIDATE = 3;
    private static final String PROVIDER_CALLBACK_DESCRIPTOR =
            "org.neo2.controls.ISleepImageCatalogCallback";
    private static final int PROVIDER_CALLBACK_RESULT = IBinder.FIRST_CALL_TRANSACTION;
    private static final String MANAGER_SERVICE = "neo2_sleep_image_manager";
    private static final String MANAGER_DESCRIPTOR = "android.neo2.INeo2SleepImageManager";
    private static final int MANAGER_REFRESH = IBinder.FIRST_CALL_TRANSACTION + 1;
    private static final String REFRESH_CALLBACK_DESCRIPTOR =
            "org.neo2.controls.ISleepImageRefreshCallback";
    private static final int REFRESH_CALLBACK_RESULT = IBinder.FIRST_CALL_TRANSACTION;
    private static final int MODE_KEEP_LAST_SCREEN = 0;
    private static final int MODE_SLEEP_IMAGE = 1;
    private static final int MODE_OVERLAY_SLEEP_IMAGE = 2;
    private static final int MODE_KOREADER_COVER = 3;

    interface UiCallback {
        void onCatalog(CatalogView catalog);
        void onMutation(StoreResult store, RefreshResult refresh, CatalogView catalog);
    }

    enum StoreResult { UNCHANGED, COMMITTED, STALE, FINAL_CANDIDATE, PROTECTED, FAILED }
    enum RefreshResult { NOT_REQUESTED, PENDING, ACCEPTED, REJECTED, UNAVAILABLE }

    static final class CatalogItem {
        final String id;
        final boolean builtIn;
        final boolean rotationEnabled;
        final int[] thumbnail;

        CatalogItem(String id, boolean builtIn, boolean rotationEnabled, int[] thumbnail) {
            this.id = id;
            this.builtIn = builtIn;
            this.rotationEnabled = rotationEnabled;
            this.thumbnail = thumbnail;
        }
    }

    static final class CatalogView {
        final long generation;
        final ArrayList<CatalogItem> items;

        CatalogView(long generation, ArrayList<CatalogItem> items) {
            this.generation = generation;
            this.items = items;
        }
    }

    final class UiBinder extends Binder {
        void load(UiCallback callback) {
            mWorker.post(() -> deliverCatalog(callback, ensureCatalogOnWorker()));
        }

        void commit(long expectedGeneration, byte[] gray, byte[] alpha, byte[] rgba, UiCallback callback) {
            final byte[] ownedGray = gray == null ? null : gray.clone();
            final byte[] ownedAlpha = alpha == null ? null : alpha.clone();
            final byte[] ownedRgba = rgba == null ? null : rgba.clone();
            mWorker.post(() -> {
                SleepImageStore.Mutation mutation;
                synchronized (mStoreLock) {
                    mutation = mStore.add(expectedGeneration, ownedGray, ownedAlpha, ownedRgba);
                }
                handleMutation(callback, mutation);
            });
        }

        void remove(long expectedGeneration, String id, UiCallback callback) {
            mWorker.post(() -> {
                SleepImageStore.Mutation mutation;
                synchronized (mStoreLock) {
                    mutation = mStore.remove(expectedGeneration, id);
                }
                handleMutation(callback, mutation);
            });
        }

        void setBuiltInRotation(long expectedGeneration, boolean enabled, UiCallback callback) {
            mWorker.post(() -> {
                SleepImageStore.Mutation mutation;
                synchronized (mStoreLock) {
                    mutation = mStore.setBuiltInRotation(expectedGeneration, enabled);
                }
                handleMutation(callback, mutation);
            });
        }
    }

    private final Object mStoreLock = new Object();
    private final HandlerThread mWorkerThread = new HandlerThread("Neo2SleepImageCatalog");
    private final Handler mMain = new Handler(Looper.getMainLooper());
    private final UiBinder mUiBinder = new UiBinder();
    private Handler mWorker;
    private SleepImageStore mStore;
    private SleepImageTransforms.Planes mFallback;
    private FileObserver mStorageObserver;
    private FileObserver mCoverObserver;
    private final Runnable mCoverRefresh = () -> {
        SleepImageStore.Snapshot snapshot = ensureCatalogOnWorker();
        if (snapshot.generation > 0) {
            requestManagerRefresh(snapshot.generation, null, catalogView(snapshot));
        }
    };

    @Override public void onCreate() {
        super.onCreate();
        File root = createDeviceProtectedStorageContext().getDir("sleep-images", MODE_PRIVATE);
        mStore = new SleepImageStore(root, WIDTH, HEIGHT,
                DEVICE_PROFILE.presentationToPanelRotation,
                DEVICE_PROFILE.builtInSleepImageId);
        mWorkerThread.start();
        mWorker = new Handler(mWorkerThread.getLooper());
        mWorker.post(this::ensureCatalogOnWorker);
        startCoverObservers();
    }

    @Override public IBinder onBind(Intent intent) {
        return intent != null && ACTION_MANAGE.equals(intent.getAction()) ? mUiBinder : mProviderBinder;
    }

    @Override public void onDestroy() {
        if (mStorageObserver != null) mStorageObserver.stopWatching();
        if (mCoverObserver != null) mCoverObserver.stopWatching();
        mWorkerThread.quitSafely();
        super.onDestroy();
    }

    private void startCoverObservers() {
        mStorageObserver = new FileObserver(new File(KOREADER_STORAGE_ROOT),
                COVER_OBSERVER_EVENTS) {
            @Override public void onEvent(int event, String path) {
                if ("screensaver".equals(path)) {
                    startCoverDirectoryObserver();
                    scheduleCoverRefresh();
                }
            }
        };
        mStorageObserver.startWatching();
        startCoverDirectoryObserver();
    }

    private void startCoverDirectoryObserver() {
        if (mCoverObserver != null) mCoverObserver.stopWatching();
        mCoverObserver = null;
        File directory = new File(KOREADER_COVER_DIRECTORY);
        if (!directory.isDirectory()) return;
        mCoverObserver = new FileObserver(directory, COVER_OBSERVER_EVENTS) {
            @Override public void onEvent(int event, String path) {
                if (KOREADER_COVER_NAME.equals(path)) scheduleCoverRefresh();
            }
        };
        mCoverObserver.startWatching();
    }

    private void scheduleCoverRefresh() {
        if (mWorker == null) return;
        mWorker.removeCallbacks(mCoverRefresh);
        mWorker.postDelayed(mCoverRefresh, COVER_REFRESH_DELAY_MILLIS);
    }

    private SleepImageStore.Snapshot ensureCatalogOnWorker() {
        synchronized (mStoreLock) {
            try {
                if (mFallback == null) mFallback = decodeFallback();
                return mStore.openAndRepair(mFallback.gray, mFallback.alpha);
            } catch (IOException | RuntimeException ignored) {
                return mStore.read();
            }
        }
    }

    private SleepImageTransforms.Planes decodeFallback() throws IOException {
        final int width = 320, height = 480;
        int[] pixels = new int[width * height];
        java.util.Arrays.fill(pixels, 0xfff4f1e8);
        for (int y = 120; y < 360; ++y) {
            for (int x = 48; x < 272; ++x) {
                boolean edge = x < 52 || x >= 268 || y < 124 || y >= 356 ||
                        (x >= 158 && x <= 161);
                boolean line = y > 145 && y < 335 && y % 24 < 2 &&
                        ((x > 65 && x < 145) || (x > 175 && x < 255));
                pixels[y * width + x] = edge || line ? 0xff202020 : 0xffffffff;
            }
        }
        return SleepImageTransforms.renderForPanel(pixels, width, height, 0,
                SleepImageTransforms.Framing.FIT, 0.5f, 0.5f,
                DEVICE_PROFILE.panelWidth, DEVICE_PROFILE.panelHeight,
                DEVICE_PROFILE.presentationToPanelRotation, DEVICE_PROFILE.hasColorCfa);
    }

    private SleepImageTransforms.Planes decodeKoreaderCover() throws IOException {
        byte[] encoded = SleepImageStore.readStableCover(
                new File(KOREADER_COVER_DIRECTORY, KOREADER_COVER_NAME));
        if (encoded == null) throw new IOException("KOReader cover is unavailable");
        final SleepImageTransforms.DecodePlan[] plan = new SleepImageTransforms.DecodePlan[1];
        Bitmap decoded = ImageDecoder.decodeBitmap(
                ImageDecoder.createSource(ByteBuffer.wrap(encoded)), (decoder, info, source) -> {
                    plan[0] = SleepImageTransforms.plan(encoded.length,
                            info.getSize().getWidth(), info.getSize().getHeight(),
                            info.isAnimated());
                    decoder.setAllocator(ImageDecoder.ALLOCATOR_SOFTWARE);
                    decoder.setMemorySizePolicy(ImageDecoder.MEMORY_POLICY_LOW_RAM);
                    decoder.setTargetSampleSize(plan[0].sampleSize);
                    decoder.setOnPartialImageListener(exception -> false);
                });
        if (decoded == null || plan[0] == null) throw new IOException("KOReader cover is invalid");
        try {
            int[] pixels = new int[decoded.getWidth() * decoded.getHeight()];
            decoded.getPixels(pixels, 0, decoded.getWidth(), 0, 0,
                    decoded.getWidth(), decoded.getHeight());
            return SleepImageTransforms.renderForPanel(pixels,
                    decoded.getWidth(), decoded.getHeight(), 0,
                    SleepImageTransforms.Framing.FIT, 0.5f, 0.5f, WIDTH, HEIGHT,
                    DEVICE_PROFILE.presentationToPanelRotation, DEVICE_PROFILE.hasColorCfa);
        } finally {
            decoded.recycle();
        }
    }

    private void handleMutation(UiCallback callback, SleepImageStore.Mutation mutation) {
        StoreResult storeResult = mapStoreResult(mutation.result);
        CatalogView catalog = catalogView(mutation.snapshot);
        if (storeResult != StoreResult.COMMITTED) {
            deliverMutation(callback, storeResult, RefreshResult.NOT_REQUESTED, catalog);
            return;
        }
        deliverMutation(callback, storeResult, RefreshResult.PENDING, catalog);
        requestManagerRefresh(mutation.snapshot.generation, callback, catalog);
    }

    private void requestManagerRefresh(long storeGeneration, UiCallback callback, CatalogView catalog) {
        IBinder manager = ServiceManager.getService(MANAGER_SERVICE);
        if (manager == null) {
            deliverMutation(callback, StoreResult.COMMITTED, RefreshResult.UNAVAILABLE, catalog);
            return;
        }
        IBinder resultCallback = new Binder() {
            private boolean delivered;
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                if (code != REFRESH_CALLBACK_RESULT || (flags & IBinder.FLAG_ONEWAY) == 0 ||
                        Binder.getCallingUid() != Process.SYSTEM_UID || delivered) return false;
                try {
                    data.enforceInterface(REFRESH_CALLBACK_DESCRIPTOR);
                    long generation = data.readLong();
                    int result = data.readInt();
                    data.enforceNoDataAvail();
                    if (generation != storeGeneration) return false;
                    delivered = true;
                    if (callback != null) {
                        deliverMutation(callback, StoreResult.COMMITTED,
                                result == RESULT_OK ? RefreshResult.ACCEPTED : RefreshResult.REJECTED,
                                catalog);
                    }
                    return true;
                } catch (RuntimeException ignored) {
                    return false;
                }
            }
        };
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(MANAGER_DESCRIPTOR);
            data.writeLong(storeGeneration);
            data.writeStrongBinder(resultCallback);
            if (!manager.transact(MANAGER_REFRESH, data, reply, 0)) {
                deliverMutation(callback, StoreResult.COMMITTED, RefreshResult.UNAVAILABLE, catalog);
                return;
            }
            reply.readException();
            if (reply.readInt() != RESULT_OK) {
                deliverMutation(callback, StoreResult.COMMITTED, RefreshResult.REJECTED, catalog);
            }
        } catch (RemoteException | RuntimeException ignored) {
            deliverMutation(callback, StoreResult.COMMITTED, RefreshResult.UNAVAILABLE, catalog);
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    private void deliverCatalog(UiCallback callback, SleepImageStore.Snapshot snapshot) {
        CatalogView catalog = catalogView(snapshot);
        mMain.post(() -> callback.onCatalog(catalog));
    }

    private void deliverMutation(UiCallback callback, StoreResult store, RefreshResult refresh,
            CatalogView catalog) {
        if (callback == null) return;
        mMain.post(() -> callback.onMutation(store, refresh, catalog));
    }

    private CatalogView catalogView(SleepImageStore.Snapshot snapshot) {
        ArrayList<CatalogItem> items = new ArrayList<>();
        for (SleepImageStore.Summary summary : mStore.summaries(snapshot,
                THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT)) {
            items.add(new CatalogItem(summary.id, summary.builtIn, summary.rotationEnabled,
                    summary.thumbnail));
        }
        return new CatalogView(snapshot.generation, items);
    }

    private static StoreResult mapStoreResult(SleepImageStore.MutationResult result) {
        switch (result) {
            case COMMITTED: return StoreResult.COMMITTED;
            case STALE: return StoreResult.STALE;
            case FINAL_CANDIDATE: return StoreResult.FINAL_CANDIDATE;
            case PROTECTED: return StoreResult.PROTECTED;
            default: return StoreResult.FAILED;
        }
    }

    private final IBinder mProviderBinder = new Binder() {
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            if (code != REQUEST_CATALOG || Binder.getCallingUid() != Process.SYSTEM_UID ||
                    (flags & IBinder.FLAG_ONEWAY) == 0) return false;
            long requestGeneration;
            int mode;
            IBinder callback;
            try {
                data.enforceInterface(PROVIDER_DESCRIPTOR);
                if (data.readInt() != PROVIDER_PROTOCOL_VERSION ||
                        (requestGeneration = data.readLong()) <= 0 ||
                        !validMode(mode = data.readInt()) ||
                        (callback = data.readStrongBinder()) == null || data.dataAvail() != 0) {
                    return false;
                }
            } catch (SecurityException ignored) {
                return false;
            }
            final long generation = requestGeneration;
            final int requestMode = mode;
            final IBinder resultCallback = callback;
            mWorker.post(() -> publishResult(generation, requestMode, resultCallback));
            return true;
        }
    };

    private static boolean validMode(int mode) {
        return mode == MODE_KEEP_LAST_SCREEN || mode == MODE_SLEEP_IMAGE ||
                mode == MODE_OVERLAY_SLEEP_IMAGE || mode == MODE_KOREADER_COVER;
    }

    private void publishResult(long requestGeneration, int mode, IBinder callback) {
        SleepImageStore.Snapshot snapshot = ensureCatalogOnWorker();
        SleepImageTransforms.Planes cover = null;
        if (mode == MODE_KOREADER_COVER) {
            try {
                cover = decodeKoreaderCover();
            } catch (IOException | RuntimeException ignored) { }
        }
        ArrayList<SleepImageStore.PublicationEntry> publication = mStore.publication(snapshot,
                mode == MODE_KOREADER_COVER, cover == null ? null : cover.gray,
                cover == null ? null : cover.alpha);
        if (publication.isEmpty()) {
            sendResult(callback, requestGeneration, snapshot.generation, RESULT_EMPTY,
                    REASON_NO_VALID_CANDIDATE, publication, cover);
            return;
        }
        if (!sendResult(callback, requestGeneration, snapshot.generation, RESULT_OK, REASON_NONE,
                publication, cover)) {
            sendResult(callback, requestGeneration, snapshot.generation, RESULT_UNAVAILABLE,
                    REASON_SHARED_MEMORY, new ArrayList<>(), null);
        }
    }

    private boolean sendResult(IBinder callback, long requestGeneration, long storeGeneration,
            int result, int reason, ArrayList<SleepImageStore.PublicationEntry> candidates,
            SleepImageTransforms.Planes cover) {
        Parcel data = Parcel.obtain();
        ArrayList<SharedMemory> memory = new ArrayList<>();
        try {
            data.writeInterfaceToken(PROVIDER_CALLBACK_DESCRIPTOR);
            data.writeInt(PROVIDER_PROTOCOL_VERSION);
            data.writeLong(requestGeneration);
            data.writeLong(storeGeneration);
            data.writeInt(result);
            data.writeInt(reason);
            data.writeInt(result == RESULT_OK ? candidates.size() : 0);
            if (result == RESULT_OK) {
                for (SleepImageStore.PublicationEntry candidate : candidates) {
                    byte[] pixels = candidate.entry.gray;
                    if (DEVICE_PROFILE.hasColorCfa) {
                        pixels = cover != null && !candidate.fallback ? cover.rgba
                                : candidate.entry.builtIn && mFallback != null && mFallback.rgba != null
                                        ? mFallback.rgba : mStore.colorPlane(candidate.entry);
                        if (pixels == null || pixels.length != Math.multiplyExact(BYTES, 4)) {
                            throw new IOException("invalid color candidate");
                        }
                    }
                    SharedMemory grayEntry = SharedMemory.create("neo2-sleep-image-pixels",
                            DEVICE_PROFILE.hasColorCfa ? Math.multiplyExact(BYTES, 4) : BYTES);
                    SharedMemory alphaEntry = null;
                    try {
                        alphaEntry = SharedMemory.create("neo2-sleep-image-alpha", BYTES);
                        writeSharedMemory(grayEntry, pixels);
                        writeSharedMemory(alphaEntry, candidate.entry.alpha);
                        data.writeInt(candidate.fallback ? 1 : 0);
                        data.writeInt(candidate.eligible ? 1 : 0);
                        data.writeInt(WIDTH);
                        data.writeInt(HEIGHT);
                        data.writeFileDescriptor(grayEntry.getFileDescriptor());
                        data.writeFileDescriptor(alphaEntry.getFileDescriptor());
                        memory.add(grayEntry);
                        memory.add(alphaEntry);
                        grayEntry = null;
                        alphaEntry = null;
                    } finally {
                        if (grayEntry != null) grayEntry.close();
                        if (alphaEntry != null) alphaEntry.close();
                    }
                }
            }
            callback.transact(PROVIDER_CALLBACK_RESULT, data, null, IBinder.FLAG_ONEWAY);
            return true;
        } catch (ErrnoException | IOException ignored) {
            return false;
        } catch (RemoteException | RuntimeException ignored) {
            return true;
        } finally {
            for (SharedMemory entry : memory) entry.close();
            data.recycle();
        }
    }

    private static void writeSharedMemory(SharedMemory memory, byte[] bytes) throws ErrnoException {
        ByteBuffer mapped = memory.mapReadWrite();
        try {
            mapped.put(bytes);
        } finally {
            SharedMemory.unmap(mapped);
        }
        if (!memory.setProtect(android.system.OsConstants.PROT_READ)) {
            throw new IllegalStateException("cannot protect shared-memory plane");
        }
    }
}

package org.neo2.controls;

import android.app.Service;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Binder;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Process;
import android.os.SharedMemory;
import android.system.ErrnoException;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.FutureTask;

/** Direct-boot owner of the private preconverted sleep-image store. */
public final class SleepImageCatalogService extends Service {
    public static final int WIDTH = 1448;
    public static final int HEIGHT = 1072;
    public static final int BYTES = WIDTH * HEIGHT;
    public static final int MAXIMUM_ENTRIES = 8;
    public static final String ACTION_IMPORT = "org.neo2.controls.action.IMPORT_SLEEP_IMAGE";
    static final String DESCRIPTOR = "org.neo2.controls.ISleepImageCatalog";
    static final int GET_CATALOG = IBinder.FIRST_CALL_TRANSACTION;

    private final Object mLock = new Object();
    private final HandlerThread mWorkerThread = new HandlerThread("Neo2SleepImageCatalog");
    private Handler mWorker;
    private File mStore;

    @Override public void onCreate() {
        super.onCreate();
        mStore = createDeviceProtectedStorageContext().getDir("sleep-images", MODE_PRIVATE);
        mWorkerThread.start();
        mWorker = new Handler(mWorkerThread.getLooper());
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_IMPORT.equals(intent.getAction()) && intent.getData() != null) {
            mWorker.post(() -> {
                try { importImage(intent.getData().toString()); } catch (IOException ignored) { }
            });
        }
        return START_NOT_STICKY;
    }

    @Override public IBinder onBind(Intent intent) { return new CatalogBinder(); }

    @Override public void onDestroy() {
        mWorkerThread.quitSafely();
        super.onDestroy();
    }

    /** ControlsActivity supplies a content URI; it is decoded and copied here, never by SF. */
    void importImage(String source) throws IOException {
        Bitmap decoded = BitmapFactory.decodeStream(getContentResolver().openInputStream(
                android.net.Uri.parse(source)));
        if (decoded == null) throw new IOException("unsupported image");
        Bitmap panel = Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(panel);
        canvas.drawColor(Color.WHITE);
        Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
        float scale = Math.min((float) WIDTH / decoded.getWidth(), (float) HEIGHT / decoded.getHeight());
        int drawWidth = Math.round(decoded.getWidth() * scale);
        int drawHeight = Math.round(decoded.getHeight() * scale);
        canvas.drawBitmap(decoded, null, new android.graphics.Rect(
                (WIDTH - drawWidth) / 2, (HEIGHT - drawHeight) / 2,
                (WIDTH + drawWidth) / 2, (HEIGHT + drawHeight) / 2), paint);
        decoded.recycle();
        int[] pixels = new int[BYTES];
        panel.getPixels(pixels, 0, WIDTH, 0, 0, WIDTH, HEIGHT);
        panel.recycle();
        byte[] gray = new byte[BYTES];
        for (int i = 0; i < pixels.length; ++i) {
            int p = pixels[i];
            int luminance = (77 * Color.red(p) + 150 * Color.green(p) + 29 * Color.blue(p)) >> 8;
            gray[i] = (byte) (luminance & 0xf0);
        }
        synchronized (mLock) {
            File target = new File(mStore, "candidate-" + System.nanoTime() + ".gray");
            try (FileOutputStream output = new FileOutputStream(target)) { output.write(gray); }
            pruneLocked();
        }
    }

    private ArrayList<File> candidatesLocked() {
        File[] files = mStore.listFiles((dir, name) -> name.startsWith("candidate-") && name.endsWith(".gray"));
        ArrayList<File> result = new ArrayList<>();
        if (files != null) result.addAll(Arrays.asList(files));
        result.removeIf(file -> file.length() != BYTES);
        result.sort(Comparator.comparing(File::getName));
        return result;
    }

    private void pruneLocked() {
        ArrayList<File> files = candidatesLocked();
        while (files.size() > MAXIMUM_ENTRIES) files.remove(0).delete();
    }

    private static boolean isPanelGray(byte[] bytes) {
        for (byte value : bytes) if ((value & 0x0f) != 0) return false;
        return true;
    }

    private ArrayList<byte[]> loadValidatedOnWorker() {
        ArrayList<byte[]> result = new ArrayList<>();
        synchronized (mLock) {
            for (File file : candidatesLocked()) {
                try (FileInputStream input = new FileInputStream(file)) {
                    byte[] bytes = input.readNBytes(BYTES);
                    if (bytes.length == BYTES && isPanelGray(bytes)) result.add(bytes);
                } catch (IOException ignored) { }
            }
        }
        return result;
    }

    private final class CatalogBinder extends Binder {
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            if (code != GET_CATALOG || Binder.getCallingUid() != Process.SYSTEM_UID || reply == null) {
                return false;
            }
            try {
                data.enforceInterface(DESCRIPTOR);
            } catch (SecurityException ignored) {
                return false;
            }
            FutureTask<ArrayList<byte[]>> task = new FutureTask<>(
                    SleepImageCatalogService.this::loadValidatedOnWorker);
            mWorker.post(task);
            try {
                ArrayList<byte[]> candidates = task.get();
                reply.writeNoException();
                reply.writeInt(candidates.size());
                for (byte[] bytes : candidates) {
                    try {
                        SharedMemory memory = SharedMemory.create("neo2-sleep-image", BYTES);
                        ByteBuffer mapped = memory.mapReadWrite();
                        mapped.put(bytes);
                        SharedMemory.unmap(mapped);
                        memory.setProtect(android.system.OsConstants.PROT_READ);
                        reply.writeInt(WIDTH);
                        reply.writeInt(HEIGHT);
                        reply.writeFileDescriptor(memory.getFileDescriptor());
                        memory.close();
                    } catch (ErrnoException ignored) {
                        reply.writeInt(0);
                        reply.writeInt(0);
                        reply.writeFileDescriptor(null);
                    }
                }
            } catch (InterruptedException | ExecutionException ignored) {
                Thread.currentThread().interrupt();
                reply.writeNoException();
                reply.writeInt(0);
            }
            return true;
        }
    }
}

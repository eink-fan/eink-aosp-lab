package org.neo2.controls;

import android.content.Context;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.ServiceManager;
import android.widget.Toast;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.IntConsumer;

/** Serialized, signature-protected quality-mode control. */
final class ColorQuality {
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    static void configure(Context context, int requested, IntConsumer finished) {
        Context app = context.getApplicationContext();
        WORKER.execute(() -> {
            int state = -1;
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            try {
                IBinder service = ServiceManager.checkService("neo2_frontlight_controls");
                if (service != null) {
                    data.writeInterfaceToken("org.neo2.controls.IFrontLightControl");
                    data.writeInt(requested);
                    if (service.transact(IBinder.FIRST_CALL_TRANSACTION + 2, data, reply, 0)) {
                        reply.readException();
                        state = reply.readInt();
                    }
                }
                if (state >= 0 && requested >= 0)
                    app.getSharedPreferences("color_quality", Context.MODE_PRIVATE)
                            .edit().putBoolean("enabled", state == 1).apply();
            } catch (android.os.RemoteException | RuntimeException ignored) {
                state = -1;
            } finally { data.recycle(); reply.recycle(); }
            int result = state;
            new Handler(Looper.getMainLooper()).post(() -> finished.accept(result));
        });
    }
    static void cleanup(Context context) {
        Context app = context.getApplicationContext();
        WORKER.execute(() -> {
            boolean queued = false;
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            try {
                IBinder service = ServiceManager.checkService("neo2_frontlight_controls");
                if (service != null) {
                    data.writeInterfaceToken("org.neo2.controls.IFrontLightControl");
                    if (service.transact(IBinder.FIRST_CALL_TRANSACTION + 3, data, reply, 0)) {
                        reply.readException();
                        String receipt = reply.readString();
                        queued = receipt != null && receipt.startsWith("refresh queued id=");
                    }
                }
            } catch (android.os.RemoteException | RuntimeException ignored) {
                queued = false;
            } finally { data.recycle(); reply.recycle(); }
            if (!queued) new Handler(Looper.getMainLooper()).post(() ->
                    Toast.makeText(app, "Color cleanup unavailable", Toast.LENGTH_SHORT).show());
        });
    }
    private ColorQuality() {}
}

package org.neo2.controls;

import android.content.Context;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.ServiceManager;
import android.widget.Toast;
import java.util.concurrent.atomic.AtomicBoolean;

/** One shared entry for user-requested full-waveform cleanup. */
final class DisplayRefresh {
    private static final AtomicBoolean BUSY = new AtomicBoolean();
    static void request(Context context) {
        request(context, () -> {});
    }
    static void request(Context context, Runnable finished) {
        if (!BUSY.compareAndSet(false, true)) { finished.run(); return; }
        Context app = context.getApplicationContext();
        new Thread(() -> {
            boolean queued = false;
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                IBinder service = ServiceManager.checkService("neo2_frontlight_controls");
                if (service != null) {
                    data.writeInterfaceToken("org.neo2.controls.IFrontLightControl");
                    if (service.transact(IBinder.FIRST_CALL_TRANSACTION + 5, data, reply, 0)) {
                        reply.readException();
                        String receipt = reply.readString();
                        queued = receipt != null && receipt.startsWith("refresh queued id=");
                    }
                }
            } catch (android.os.RemoteException | RuntimeException e) {
                queued = false;
            } finally {
                data.recycle();
                reply.recycle();
                BUSY.set(false);
                finished.run();
            }
            // A success toast would itself change the image being refreshed.
            if (!queued) new Handler(Looper.getMainLooper()).post(() ->
                    Toast.makeText(app, "Full refresh unavailable", Toast.LENGTH_SHORT).show());
        }, "Neo2DisplayRefresh").start();
    }
    private DisplayRefresh() {}
}

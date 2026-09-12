package org.neo2.controls;

import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.ServiceManager;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Session-only diagnostic; starts disabled after SurfaceFlinger restarts. */
final class VividColor {
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    interface Callback { void finished(int mode, boolean redrawQueued); }
    static void configure(int requested, Callback callback) {
        WORKER.execute(() -> {
            int mode = -1;
            boolean queued = false;
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            try {
                IBinder service = ServiceManager.checkService("neo2_frontlight_controls");
                if (service != null) {
                    data.writeInterfaceToken("org.neo2.controls.IFrontLightControl");
                    data.writeInt(requested);
                    if (service.transact(IBinder.FIRST_CALL_TRANSACTION + 6, data, reply, 0)) {
                        reply.readException();
                        int returned = reply.readInt();
                        int redraw = reply.readInt();
                        if (returned >= 0 && returned <= 1 && (redraw == 0 || redraw == 1)) {
                            mode = returned;
                            queued = redraw == 1;
                        }
                    }
                }
            } catch (android.os.RemoteException | RuntimeException ignored) {
                mode = -1;
            } finally { data.recycle(); reply.recycle(); }
            final int result = mode;
            final boolean redrawQueued = queued;
            new Handler(Looper.getMainLooper()).post(() -> callback.finished(result, redrawQueued));
        });
    }
    private VividColor() {}
}

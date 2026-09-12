package org.neo2.controls;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.ServiceManager;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Signature-protected color treatment, independent of color cleanup. */
final class GrayPreference {
    static final int ORIGINAL = 0, BALANCED = 1, STRONGER = 2;
    static final String[] LABELS = {"Original", "Balanced", "Stronger"};
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    interface Callback { void finished(int mode, boolean saved, boolean redrawQueued); }
    private static SharedPreferences preferences(Context context) {
        return context.createDeviceProtectedStorageContext()
                .getSharedPreferences("gray_preference", Context.MODE_PRIVATE);
    }
    private static int[] transact(int requested) {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            IBinder service = ServiceManager.checkService("neo2_frontlight_controls");
            if (service == null) return null;
            data.writeInterfaceToken("org.neo2.controls.IFrontLightControl");
            data.writeInt(requested);
            if (!service.transact(IBinder.FIRST_CALL_TRANSACTION + 4, data, reply, 0)) return null;
            reply.readException();
            int mode = reply.readInt();
            int redraw = reply.readInt();
            if (mode < ORIGINAL || mode > STRONGER || (redraw != 0 && redraw != 1)) return null;
            return new int[] {mode, redraw};
        } catch (android.os.RemoteException | RuntimeException ignored) {
            return null;
        } finally { data.recycle(); reply.recycle(); }
    }
    // -1 restores the saved preference and reads it back. Used at boot and on
    // resume, so a SurfaceFlinger restart does not overwrite the saved choice.
    static void configure(Context context, int requested, Callback finished) {
        Context app = context.getApplicationContext();
        WORKER.execute(() -> {
            GrayPreferenceSelection.Result result;
            try {
                SharedPreferences prefs = preferences(app);
                result = GrayPreferenceSelection.apply(requested, new GrayPreferenceSelection.Store() {
                    public int read() { return prefs.getInt("mode", BALANCED); }
                    public boolean save(int mode) { return prefs.edit().putInt("mode", mode).commit(); }
                }, GrayPreference::transact);
            } catch (RuntimeException ignored) {
                result = new GrayPreferenceSelection.Result(-1, false, false);
            }
            GrayPreferenceSelection.Result outcome = result;
            new Handler(Looper.getMainLooper()).post(() -> finished.finished(
                    outcome.mode, outcome.saved, outcome.redrawQueued));
        });
    }
    private GrayPreference() {}
}

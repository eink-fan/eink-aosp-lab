package org.neo2.controls;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;

public final class GrayPreferenceBootReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if (!SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa || intent == null) return;
        String action = intent.getAction();
        if (!Intent.ACTION_LOCKED_BOOT_COMPLETED.equals(action) &&
                !Intent.ACTION_BOOT_COMPLETED.equals(action) &&
                !Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) return;
        PendingResult pending = goAsync();
        restore(context.getApplicationContext(), pending, 0);
    }
    private void restore(Context context, PendingResult pending, int attempt) {
        GrayPreference.configure(context, -1, (mode, saved, queued) -> {
            if (mode < 0 && attempt < 3) {
                new Handler(Looper.getMainLooper()).postDelayed(
                        () -> restore(context, pending, attempt + 1), 500);
            } else pending.finish();
        });
    }
}

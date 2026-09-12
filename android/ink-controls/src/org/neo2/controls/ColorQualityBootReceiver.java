package org.neo2.controls;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
public final class ColorQualityBootReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if (!SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa || intent == null ||
                (!Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction()) &&
                 !Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction()))) return;
        PendingResult pending = goAsync();
        boolean enabled = context.getSharedPreferences("color_quality", Context.MODE_PRIVATE)
                .getBoolean("enabled", false);
        ColorQuality.configure(context, enabled ? 1 : 0, state -> pending.finish());
    }
}

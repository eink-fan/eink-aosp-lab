package org.neo2.controls;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** Signature-protected action; future gesture integrations need an explicit grant design. */
public final class FullRefreshReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if ("org.neo2.controls.action.FULL_REFRESH".equals(intent.getAction())) {
            PendingResult pending = goAsync();
            DisplayRefresh.request(context, pending::finish);
        }
    }
}

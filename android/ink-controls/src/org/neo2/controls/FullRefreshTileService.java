package org.neo2.controls;

import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

/** Momentary action; it does not change the app's refresh policy. */
public final class FullRefreshTileService extends TileService {
    @Override public void onStartListening() {
        Tile tile = getQsTile();
        if (tile != null) {
            tile.setState(SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa
                    ? Tile.STATE_INACTIVE : Tile.STATE_UNAVAILABLE);
            tile.setLabel("Full refresh");
            tile.updateTile();
        }
    }
    @Override public void onClick() {
        if (SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa)
            unlockAndRun(() -> DisplayRefresh.request(this));
    }
}

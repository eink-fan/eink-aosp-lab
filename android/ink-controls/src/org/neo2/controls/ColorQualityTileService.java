package org.neo2.controls;
import android.app.AlertDialog;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;
public final class ColorQualityTileService extends TileService {
    private boolean busy;
    private void update(int state) {
        Tile tile = getQsTile();
        if (tile == null) return;
        tile.setLabel("Color quality");
        tile.setState(state < 0 ? Tile.STATE_UNAVAILABLE :
                state == 1 ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.updateTile();
    }
    @Override public void onStartListening() {
        if (!SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa) { update(-1); return; }
        ColorQuality.configure(this, -1, this::update);
    }
    @Override public void onClick() {
        if (busy || !SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa) return;
        unlockAndRun(() -> {
            busy = true;
            ColorQuality.configure(this, -1, state -> {
                if (state < 0) { busy = false; update(-1); return; }
                ColorQuality.configure(this, state == 1 ? 0 : 1, result -> {
                    busy = false; update(result);
                    if (result == 1) showDialog(new AlertDialog.Builder(this)
                            .setTitle("Clean colors now?")
                            .setMessage("Briefly whiten colored areas to clear existing ghosting.")
                            .setPositiveButton("Clean colors", (dialog, which) -> ColorQuality.cleanup(this))
                            .setNegativeButton("Later", null).create());
                });
            });
        });
    }
}

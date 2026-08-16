package org.neo2.controls;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.provider.Settings;
import android.view.Gravity;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.TextView;

/** Reader controls submit only final slider values, never drag events. */
public final class ControlsActivity extends Activity {
    private static final int IMPORT_SLEEP_IMAGE = 1;
    private static final int PREVIEW_SLEEP_IMAGE = 2;
    private static final String SERVICE = "neo2_frontlight";
    private static final String DESCRIPTOR = "android.neo2.INeo2FrontLightManager";
    private static final int APPLY = IBinder.FIRST_CALL_TRANSACTION;
    private static final int INITIAL_PERCENT = 50;
    private static final int ADJUSTMENT_STEP_PERCENT = 5;
    private static final int SECTION_LABEL_SP = 28;
    private static final int VALUE_SP = 34;
    private static final int BUTTON_SP = 26;
    private static final int CONTROL_HEIGHT_DP = 88;
    private static final int ACTION_HEIGHT_DP = 96;
    private static final int THUMBNAIL_WIDTH = 134;
    private static final int THUMBNAIL_HEIGHT = 181;
    private static final String SLEEP_IMAGE_MODE = "neo2_sleep_image_mode";
    private static final String SLEEP_IMAGE_SERVICE = "neo2_sleep_image_manager";
    private static final String SLEEP_IMAGE_DESCRIPTOR =
            "android.neo2.INeo2SleepImageManager";
    private static final int SET_SLEEP_IMAGE_MODE = IBinder.FIRST_CALL_TRANSACTION;
    private static final int SLEEP_IMAGE_MODE_QUEUED = 0;
    private static final int MODE_KEEP_LAST_SCREEN = 0;
    private static final int MODE_SLEEP_IMAGE = 1;
    private static final int MODE_OVERLAY_SLEEP_IMAGE = 2;
    private static final int MODE_KOREADER_COVER = 3;
    private SeekBar brightness;
    private SeekBar warmth;
    private TextView status;
    private TextView sleepModeStatus;
    private TextView catalogStatus;
    private LinearLayout catalogItems;
    private long catalogGeneration;
    private SleepImageCatalogService.UiBinder catalogService;
    private boolean catalogBound;
    private final ServiceConnection catalogConnection = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder service) {
            catalogService = (SleepImageCatalogService.UiBinder) service;
            loadCatalog();
        }
        @Override public void onServiceDisconnected(ComponentName name) {
            catalogService = null;
            catalogStatus.setText("Catalog service is unavailable");
        }
    };
    private final SleepImageCatalogService.UiCallback catalogCallback =
            new SleepImageCatalogService.UiCallback() {
        @Override public void onCatalog(SleepImageCatalogService.CatalogView catalog) {
            showCatalog(catalog);
            catalogStatus.setText("Catalog generation " + catalog.generation + " is ready");
        }
        @Override public void onMutation(SleepImageCatalogService.StoreResult store,
                SleepImageCatalogService.RefreshResult refresh,
                SleepImageCatalogService.CatalogView catalog) {
            if (store == SleepImageCatalogService.StoreResult.COMMITTED) {
                showCatalog(catalog);
                if (refresh == SleepImageCatalogService.RefreshResult.PENDING) {
                    catalogStatus.setText("Catalog saved; manager synchronization pending");
                } else if (refresh == SleepImageCatalogService.RefreshResult.ACCEPTED) {
                    catalogStatus.setText("Catalog saved and accepted by the awake manager");
                } else {
                    catalogStatus.setText("Catalog saved; manager kept its prior valid snapshot");
                }
            } else if (store == SleepImageCatalogService.StoreResult.STALE) {
                showCatalog(catalog);
                catalogStatus.setText("Catalog changed elsewhere; action was rejected");
            } else if (store == SleepImageCatalogService.StoreResult.PROTECTED) {
                catalogStatus.setText("The built-in fallback is protected");
            } else if (store == SleepImageCatalogService.StoreResult.FINAL_CANDIDATE) {
                catalogStatus.setText("The final valid sleep image cannot be removed");
            } else {
                catalogStatus.setText("Catalog was not changed");
            }
        }
    };

    @Override public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        // This activity is rendered beneath the product app bar on the reader.
        // Leave a deliberate visual gap so the first label cannot be hidden by it.
        root.setPadding(dp(32), dp(96), dp(32), dp(40));
        brightness = slider(INITIAL_PERCENT);
        warmth = slider(INITIAL_PERCENT);
        root.addView(controlSection("Front-light brightness", brightness));
        root.addView(controlSection("Cool — Warm", warmth));
        Button apply = actionButton("Apply light");
        apply.setOnClickListener(v -> apply(brightness.getProgress(), warmth.getProgress()));
        root.addView(apply);
        Button off = actionButton("Turn off");
        off.setOnClickListener(v -> apply(0, warmth.getProgress()));
        root.addView(off);
        Button importSleepImage = actionButton("Import sleep image");
        importSleepImage.setContentDescription("Choose an image to stage and preview before import");
        importSleepImage.setOnClickListener(v -> chooseSleepImage());
        root.addView(importSleepImage);
        TextView catalogHeading = label("Sleep image catalog");
        catalogHeading.setTextSize(SECTION_LABEL_SP);
        catalogHeading.setAccessibilityHeading(true);
        root.addView(catalogHeading);
        catalogStatus = label("Loading catalog…");
        catalogStatus.setTextSize(22);
        catalogStatus.setAccessibilityLiveRegion(android.view.View.ACCESSIBILITY_LIVE_REGION_POLITE);
        root.addView(catalogStatus);
        catalogItems = new LinearLayout(this);
        catalogItems.setOrientation(LinearLayout.VERTICAL);
        root.addView(catalogItems);
        TextView sleepModeHeading = label("When the screen turns off");
        sleepModeHeading.setTextSize(SECTION_LABEL_SP);
        root.addView(sleepModeHeading);
        Button useSleepImage = actionButton("Sleep image");
        useSleepImage.setOnClickListener(v -> setSleepImageMode(MODE_SLEEP_IMAGE));
        root.addView(useSleepImage);
        Button useKoreaderCover = actionButton("KOReader cover");
        useKoreaderCover.setContentDescription(
                "Use the current KOReader cover, or the built-in fallback if unavailable");
        useKoreaderCover.setOnClickListener(v -> setSleepImageMode(MODE_KOREADER_COVER));
        root.addView(useKoreaderCover);
        Button overlaySleepImage = actionButton("Overlay sleep image");
        overlaySleepImage.setOnClickListener(v -> setSleepImageMode(MODE_OVERLAY_SLEEP_IMAGE));
        root.addView(overlaySleepImage);
        Button keepLastScreen = actionButton("Keep last screen");
        keepLastScreen.setOnClickListener(v -> setSleepImageMode(MODE_KEEP_LAST_SCREEN));
        root.addView(keepLastScreen);
        sleepModeStatus = label("");
        sleepModeStatus.setTextSize(22);
        root.addView(sleepModeStatus);
        updateSleepImageMode();
        status = label("Not applied since launch");
        status.setTextSize(22);
        status.setPadding(0, dp(20), 0, dp(20));
        root.addView(status);
        scroll.addView(root);
        setContentView(scroll);
        Intent catalog = new Intent(this, SleepImageCatalogService.class)
                .setAction(SleepImageCatalogService.ACTION_MANAGE);
        catalogBound = bindService(catalog, catalogConnection, Context.BIND_AUTO_CREATE);
    }

    private LinearLayout controlSection(String title, SeekBar bar) {
        LinearLayout section = new LinearLayout(this);
        section.setOrientation(LinearLayout.VERTICAL);
        section.setPadding(0, 0, 0, dp(32));

        TextView heading = label(title);
        heading.setTextSize(SECTION_LABEL_SP);
        section.addView(heading);

        bar.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(CONTROL_HEIGHT_DP)));
        section.addView(bar);

        TextView value = new TextView(this);
        value.setGravity(Gravity.CENTER);
        value.setTextSize(VALUE_SP);
        updateValue(value, bar.getProgress());
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                updateValue(value, progress);
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {
                apply(brightness.getProgress(), warmth.getProgress());
            }
        });

        LinearLayout adjust = new LinearLayout(this);
        adjust.setGravity(Gravity.CENTER_VERTICAL);
        Button lower = adjustmentButton("− 5");
        lower.setOnClickListener(v -> adjust(bar, -ADJUSTMENT_STEP_PERCENT));
        adjust.addView(lower, weightedControl());
        adjust.addView(value, weightedControl());
        Button raise = adjustmentButton("+ 5");
        raise.setOnClickListener(v -> adjust(bar, ADJUSTMENT_STEP_PERCENT));
        adjust.addView(raise, weightedControl());
        section.addView(adjust);
        return section;
    }

    private TextView label(String value) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setPadding(0, dp(8), 0, dp(8));
        return view;
    }

    private SeekBar slider(int value) {
        SeekBar bar = new SeekBar(this);
        bar.setMax(100);
        bar.setProgress(value);
        bar.setMinimumHeight(dp(CONTROL_HEIGHT_DP));
        bar.setScaleY(1.5f);
        return bar;
    }

    private Button adjustmentButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextSize(BUTTON_SP);
        button.setMinimumHeight(dp(CONTROL_HEIGHT_DP));
        return button;
    }

    private Button actionButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextSize(BUTTON_SP);
        button.setMinimumHeight(dp(ACTION_HEIGHT_DP));
        return button;
    }

    private LinearLayout.LayoutParams weightedControl() {
        return new LinearLayout.LayoutParams(0, dp(CONTROL_HEIGHT_DP), 1);
    }

    private void adjust(SeekBar bar, int delta) {
        bar.setProgress(Math.max(0, Math.min(100, bar.getProgress() + delta)));
    }

    private void updateValue(TextView value, int percent) {
        value.setText(percent + "%");
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void chooseSleepImage() {
        if (catalogGeneration <= 0) {
            catalogStatus.setText("Wait for the catalog to finish loading");
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("image/*");
        startActivityForResult(intent, IMPORT_SLEEP_IMAGE);
    }

    private void setSleepImageMode(int mode) {
        final IBinder service = ServiceManager.getService(SLEEP_IMAGE_SERVICE);
        if (service == null) {
            sleepModeStatus.setText("Sleep behavior service is unavailable");
            return;
        }
        final Parcel data = Parcel.obtain();
        final Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(SLEEP_IMAGE_DESCRIPTOR);
            data.writeInt(mode);
            if (!service.transact(SET_SLEEP_IMAGE_MODE, data, reply, 0)) {
                sleepModeStatus.setText("Sleep behavior was not changed");
                return;
            }
            reply.readException();
            if (reply.readInt() != SLEEP_IMAGE_MODE_QUEUED) {
                sleepModeStatus.setText("Sleep behavior was not changed");
                return;
            }
            sleepModeStatus.setText("Queued: " + sleepModeName(mode));
        } catch (RemoteException | RuntimeException ignored) {
            sleepModeStatus.setText("Sleep behavior was not changed");
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    private void updateSleepImageMode() {
        final int mode = Settings.Global.getInt(getContentResolver(), SLEEP_IMAGE_MODE,
                MODE_SLEEP_IMAGE);
        sleepModeStatus.setText("Selected: " + sleepModeName(mode));
    }

    private static String sleepModeName(int mode) {
        if (mode == MODE_KEEP_LAST_SCREEN) return "Keep last screen";
        if (mode == MODE_OVERLAY_SLEEP_IMAGE) return "Overlay sleep image";
        if (mode == MODE_KOREADER_COVER) return "KOReader cover";
        return "Sleep image";
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PREVIEW_SLEEP_IMAGE && resultCode == RESULT_OK) {
            loadCatalog();
            return;
        }
        if (requestCode != IMPORT_SLEEP_IMAGE || resultCode != RESULT_OK || data == null ||
                data.getData() == null) return;
        Intent preview = new Intent(this, SleepImagePreviewActivity.class);
        preview.setData(data.getData());
        preview.putExtra(SleepImagePreviewActivity.EXTRA_EXPECTED_GENERATION, catalogGeneration);
        startActivityForResult(preview, PREVIEW_SLEEP_IMAGE);
    }

    private void loadCatalog() {
        if (catalogService != null) catalogService.load(catalogCallback);
    }

    private void showCatalog(SleepImageCatalogService.CatalogView catalog) {
        catalogGeneration = catalog.generation;
        catalogItems.removeAllViews();
        for (SleepImageCatalogService.CatalogItem item : catalog.items) {
            LinearLayout row = new LinearLayout(this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(0, dp(8), 0, dp(8));
            Bitmap bitmap = Bitmap.createBitmap(item.thumbnail,
                    THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT,
                    Bitmap.Config.ARGB_8888);
            ImageView thumbnail = new ImageView(this);
            thumbnail.setImageBitmap(bitmap);
            thumbnail.setContentDescription(item.builtIn
                    ? "Built-in fallback sleep image thumbnail"
                    : "Imported sleep image thumbnail");
            row.addView(thumbnail, new LinearLayout.LayoutParams(
                    dp(THUMBNAIL_WIDTH), dp(THUMBNAIL_HEIGHT)));
            Button action = actionButton(item.builtIn
                    ? item.rotationEnabled ? "Exclude from rotation" : "Include in rotation"
                    : "Remove");
            boolean removable = !item.builtIn && catalog.items.size() > 1;
            action.setEnabled(item.builtIn || removable);
            action.setContentDescription(item.builtIn
                    ? item.rotationEnabled
                            ? "Exclude built-in fallback from random rotation"
                            : "Include built-in fallback in random rotation"
                    : removable ? "Remove this sleep image" : "Final sleep image cannot be removed");
            if (item.builtIn) {
                action.setOnClickListener(view -> setBuiltInRotation(catalog.generation,
                        !item.rotationEnabled));
            } else {
                action.setOnClickListener(view -> confirmRemoval(item.id, catalog.generation));
            }
            row.addView(action, new LinearLayout.LayoutParams(0, dp(ACTION_HEIGHT_DP), 1));
            catalogItems.addView(row);
        }
    }

    private void setBuiltInRotation(long expectedGeneration, boolean enabled) {
        catalogStatus.setText(enabled
                ? "Including built-in fallback in random rotation…"
                : "Excluding built-in fallback from random rotation…");
        if (catalogService != null) {
            catalogService.setBuiltInRotation(expectedGeneration, enabled, catalogCallback);
        } else {
            catalogStatus.setText("Catalog service is unavailable");
        }
    }

    private void confirmRemoval(String id, long expectedGeneration) {
        new AlertDialog.Builder(this)
                .setTitle("Remove sleep image?")
                .setMessage("The catalog changes only after durable removal succeeds.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Remove", (dialog, which) -> {
                    catalogStatus.setText("Removing from durable catalog…");
                    if (catalogService != null) {
                        catalogService.remove(expectedGeneration, id, catalogCallback);
                    } else {
                        catalogStatus.setText("Catalog service is unavailable");
                    }
                })
                .show();
    }

    @Override protected void onDestroy() {
        if (catalogBound) unbindService(catalogConnection);
        super.onDestroy();
    }

    private void apply(int brightnessValue, int warmthValue) {
        IBinder service = ServiceManager.getService(SERVICE);
        if (service == null) { status.setText("Unavailable — not applied"); return; }
        Parcel data = Parcel.obtain(); Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR); data.writeInt(brightnessValue); data.writeInt(warmthValue);
            if (!service.transact(APPLY, data, reply, 0)) { status.setText("Failed — not applied"); return; }
            reply.readException(); status.setText(outcome(reply.readInt()));
        } catch (RemoteException | RuntimeException ignored) { status.setText("Failed — not applied"); }
        finally { reply.recycle(); data.recycle(); }
    }

    private static String outcome(int value) {
        switch (value) {
            case 0: return "Applied"; case 1: return "Applied (capped)"; case 2: return "Off applied";
            case 3: return "Unavailable — not applied"; case 4: return "Rejected — not applied";
            case 5: return "Failed — not applied"; case 6: return "Failed — service latched";
            case 7: return "Queued";
            default: return "Failed — not applied";
        }
    }
}

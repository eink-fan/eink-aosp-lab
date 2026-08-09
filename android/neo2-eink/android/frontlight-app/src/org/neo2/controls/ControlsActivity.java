package org.neo2.controls;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.TextView;

/** Reader controls submit only final slider values, never drag events. */
public final class ControlsActivity extends Activity {
    private static final int IMPORT_SLEEP_IMAGE = 1;
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
    private SeekBar brightness;
    private SeekBar warmth;
    private TextView status;

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
        importSleepImage.setOnClickListener(v -> chooseSleepImage());
        root.addView(importSleepImage);
        status = label("Not applied since launch");
        status.setTextSize(22);
        status.setPadding(0, dp(20), 0, dp(20));
        root.addView(status);
        scroll.addView(root);
        setContentView(scroll);
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
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("image/*");
        startActivityForResult(intent, IMPORT_SLEEP_IMAGE);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != IMPORT_SLEEP_IMAGE || resultCode != RESULT_OK ||
                data == null || data.getData() == null) return;
        Intent importIntent = new Intent(this, SleepImageCatalogService.class);
        importIntent.setAction(SleepImageCatalogService.ACTION_IMPORT);
        importIntent.setData(data.getData());
        startService(importIntent);
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

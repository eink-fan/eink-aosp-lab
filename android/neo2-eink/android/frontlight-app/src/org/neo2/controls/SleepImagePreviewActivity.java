package org.neo2.controls;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ImageDecoder;
import android.graphics.Paint;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import java.io.IOException;
import java.io.InputStream;

/** Non-mutating import staging and exact panel-plane preview. */
public final class SleepImagePreviewActivity extends Activity {
    static final String EXTRA_EXPECTED_GENERATION = "expected_catalog_generation";
    private static final int ACTION_HEIGHT_DP = 88;

    private final HandlerThread mDecodeThread = new HandlerThread("Neo2SleepImagePreview");
    private final Handler mMain = new Handler();
    private Uri mSource;
    private int[] mPixels;
    private int mWidth;
    private int mHeight;
    private int mRotation;
    private float mHorizontal = 0.5f;
    private float mVertical = 0.5f;
    private SleepImageTransforms.Framing mFraming = SleepImageTransforms.Framing.FIT;
    private boolean mOverlayPreview;
    private long mExpectedGeneration;
    private SleepImageTransforms.Planes mPlanes;
    private PanelPreview mPreview;
    private TextView mStatus;
    private Button mImport;
    private SeekBar mHorizontalControl;
    private SeekBar mVerticalControl;
    private SleepImageCatalogService.UiBinder mCatalog;
    private boolean mBound;

    private final ServiceConnection mConnection = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder service) {
            mCatalog = (SleepImageCatalogService.UiBinder) service;
            updateImportEnabled();
        }
        @Override public void onServiceDisconnected(ComponentName name) {
            mCatalog = null;
            updateImportEnabled();
        }
    };

    @Override public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        mSource = getIntent().getData();
        mExpectedGeneration = getIntent().getLongExtra(EXTRA_EXPECTED_GENERATION, -1);
        if (mSource == null || mExpectedGeneration <= 0) {
            finish();
            return;
        }
        mDecodeThread.start();
        buildUi();
        Intent service = new Intent(this, SleepImageCatalogService.class)
                .setAction(SleepImageCatalogService.ACTION_MANAGE);
        mBound = bindService(service, mConnection, Context.BIND_AUTO_CREATE);
        Uri source = mSource;
        new Handler(mDecodeThread.getLooper()).post(() -> decode(source));
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(40), dp(24), dp(32));
        TextView heading = text("Sleep image preview", 30);
        heading.setAccessibilityHeading(true);
        root.addView(heading);
        mStatus = text("Checking image…", 20);
        mStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        root.addView(mStatus);
        mPreview = new PanelPreview(this);
        mPreview.setContentDescription("Exact quantized sleep image preview");
        root.addView(mPreview, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        LinearLayout framing = row();
        Button fit = action("Fit with padding");
        fit.setOnClickListener(view -> { mFraming = SleepImageTransforms.Framing.FIT; render(); });
        framing.addView(fit, weight());
        Button crop = action("Crop to fill");
        crop.setOnClickListener(view -> { mFraming = SleepImageTransforms.Framing.CROP; render(); });
        framing.addView(crop, weight());
        root.addView(framing);

        mHorizontalControl = cropControl("Move crop left or right");
        mVerticalControl = cropControl("Move crop up or down");
        root.addView(mHorizontalControl);
        root.addView(mVerticalControl);

        LinearLayout adjustments = row();
        Button rotate = action("Rotate 90°");
        rotate.setOnClickListener(view -> { mRotation = (mRotation + 90) % 360; render(); });
        adjustments.addView(rotate, weight());
        Button previewMode = action("Opaque / overlay preview");
        previewMode.setOnClickListener(view -> { mOverlayPreview = !mOverlayPreview; render(); });
        adjustments.addView(previewMode, weight());
        root.addView(adjustments);

        LinearLayout decision = row();
        Button cancel = action("Cancel");
        cancel.setOnClickListener(view -> finish());
        decision.addView(cancel, weight());
        mImport = action("Import");
        mImport.setEnabled(false);
        mImport.setOnClickListener(view -> commit());
        decision.addView(mImport, weight());
        root.addView(decision);
        setContentView(root);
    }

    private void decode(Uri source) {
        try {
            long encodedBytes = countEncodedBytes(source);
            final SleepImageTransforms.DecodePlan[] plan = new SleepImageTransforms.DecodePlan[1];
            Bitmap bitmap = ImageDecoder.decodeBitmap(ImageDecoder.createSource(getContentResolver(), source),
                    (decoder, info, decodedSource) -> {
                        plan[0] = SleepImageTransforms.plan(encodedBytes, info.getSize().getWidth(),
                                info.getSize().getHeight(), info.isAnimated());
                        decoder.setAllocator(ImageDecoder.ALLOCATOR_SOFTWARE);
                        decoder.setMemorySizePolicy(ImageDecoder.MEMORY_POLICY_LOW_RAM);
                        decoder.setTargetSampleSize(plan[0].sampleSize);
                    });
            // ImageDecoder applies EXIF orientations 1-8 before this working bitmap is returned.
            int[] pixels = new int[bitmap.getWidth() * bitmap.getHeight()];
            bitmap.getPixels(pixels, 0, bitmap.getWidth(), 0, 0,
                    bitmap.getWidth(), bitmap.getHeight());
            int width = bitmap.getWidth();
            int height = bitmap.getHeight();
            bitmap.recycle();
            // The URI is session-only and is discarded immediately after the bounded decode.
            mSource = null;
            getIntent().setData(null);
            mMain.post(() -> {
                mPixels = pixels;
                mWidth = width;
                mHeight = height;
                mStatus.setText("Choose framing, rotation, and preview mode");
                render();
            });
        } catch (IOException | RuntimeException failure) {
            mSource = null;
            getIntent().setData(null);
            mMain.post(() -> {
                mStatus.setText("This image cannot be imported: " + safeReason(failure));
                updateImportEnabled();
            });
        }
    }

    private long countEncodedBytes(Uri source) throws IOException {
        long total = 0;
        byte[] buffer = new byte[64 * 1024];
        try (InputStream input = getContentResolver().openInputStream(source)) {
            if (input == null) throw new IOException("content is unavailable");
            int count;
            while ((count = input.read(buffer)) >= 0) {
                total += count;
                if (total > SleepImageTransforms.MAX_ENCODED_BYTES) {
                    throw new IllegalArgumentException("encoded image is larger than 32 MiB");
                }
            }
        }
        return total;
    }

    private void render() {
        if (mPixels == null) return;
        mHorizontalControl.setEnabled(mFraming == SleepImageTransforms.Framing.CROP);
        mVerticalControl.setEnabled(mFraming == SleepImageTransforms.Framing.CROP);
        mPlanes = SleepImageTransforms.renderForPanel(
                mPixels, mWidth, mHeight, mRotation, mFraming,
                mHorizontal, mVertical, SleepImageCatalogService.WIDTH,
                SleepImageCatalogService.HEIGHT);
        mPreview.setPlanes(mPlanes, mOverlayPreview);
        mStatus.setText((mFraming == SleepImageTransforms.Framing.FIT
                ? "Fit with transparent white padding; "
                : "181:134 boundary-clamped crop; ") +
                (mOverlayPreview ? "overlay samples on checkerboard and white"
                        : "opaque gray-plane preview"));
        updateImportEnabled();
    }

    private void commit() {
        if (mCatalog == null || mPlanes == null) return;
        mImport.setEnabled(false);
        mStatus.setText("Saving durable catalog generation…");
        SleepImageTransforms.Planes planes = mPlanes;
        mCatalog.commit(mExpectedGeneration, planes.gray, planes.alpha,
                new SleepImageCatalogService.UiCallback() {
            @Override public void onCatalog(SleepImageCatalogService.CatalogView catalog) { }
            @Override public void onMutation(SleepImageCatalogService.StoreResult store,
                    SleepImageCatalogService.RefreshResult refresh,
                    SleepImageCatalogService.CatalogView catalog) {
                if (store != SleepImageCatalogService.StoreResult.COMMITTED) {
                    mStatus.setText(store == SleepImageCatalogService.StoreResult.STALE
                            ? "Catalog changed elsewhere; import was not committed"
                            : "Import was not committed");
                    updateImportEnabled();
                    return;
                }
                mExpectedGeneration = catalog.generation;
                if (refresh == SleepImageCatalogService.RefreshResult.PENDING) {
                    mStatus.setText("Saved. Waiting for the awake manager to accept generation " +
                            catalog.generation + "…");
                } else if (refresh == SleepImageCatalogService.RefreshResult.ACCEPTED) {
                    setResult(RESULT_OK);
                    finish();
                } else {
                    mStatus.setText("Saved, but manager synchronization failed; retry from catalog");
                }
            }
        });
    }

    private void updateImportEnabled() {
        if (mImport != null) mImport.setEnabled(mCatalog != null && mPlanes != null);
    }

    @Override protected void onDestroy() {
        mSource = null;
        mPixels = null;
        mPlanes = null;
        if (mBound) unbindService(mConnection);
        if (mDecodeThread.isAlive()) mDecodeThread.quitSafely();
        super.onDestroy();
    }

    private SeekBar cropControl(String description) {
        SeekBar control = new SeekBar(this);
        control.setMax(100);
        control.setProgress(50);
        control.setMinimumHeight(dp(72));
        control.setContentDescription(description);
        control.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                if (bar == mHorizontalControl) mHorizontal = progress / 100f;
                else mVertical = progress / 100f;
                if (fromUser) render();
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { render(); }
        });
        return control;
    }

    private Button action(String value) {
        Button button = new Button(this);
        button.setText(value);
        button.setTextSize(20);
        button.setMinimumHeight(dp(ACTION_HEIGHT_DP));
        return button;
    }

    private TextView text(String value, int size) {
        TextView text = new TextView(this);
        text.setText(value);
        text.setTextSize(size);
        text.setPadding(0, dp(8), 0, dp(8));
        return text;
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        return row;
    }

    private LinearLayout.LayoutParams weight() {
        return new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static String safeReason(Throwable failure) {
        String message = failure.getMessage();
        return message == null || message.length() > 120 ? "unsupported or corrupt content" : message;
    }

    private static final class PanelPreview extends View {
        private final Paint mPaint = new Paint();
        private Bitmap mBitmap;
        private boolean mOverlay;

        PanelPreview(Context context) { super(context); }

        void setPlanes(SleepImageTransforms.Planes planes, boolean overlay) {
            int[] pixels = SleepImageTransforms.panelToPresentationPixels(
                    planes.gray, planes.alpha, SleepImageCatalogService.WIDTH,
                    SleepImageCatalogService.HEIGHT, SleepImageCatalogService.PRESENTATION_WIDTH,
                    SleepImageCatalogService.PRESENTATION_HEIGHT);
            if (!overlay) {
                for (int index = 0; index < pixels.length; ++index) {
                    pixels[index] |= 0xff000000;
                }
            }
            if (mBitmap != null) mBitmap.recycle();
            mBitmap = Bitmap.createBitmap(pixels, SleepImageCatalogService.PRESENTATION_WIDTH,
                    SleepImageCatalogService.PRESENTATION_HEIGHT, Bitmap.Config.ARGB_8888);
            mOverlay = overlay;
            invalidate();
        }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            if (mBitmap == null) return;
            Rect destination = fitRect(getWidth(), getHeight());
            if (!mOverlay) {
                canvas.drawColor(Color.WHITE);
                canvas.drawBitmap(mBitmap, null, destination, mPaint);
                return;
            }
            int split = destination.centerX();
            int checker = Math.max(8, destination.width() / 24);
            for (int y = destination.top; y < destination.bottom; y += checker) {
                for (int x = destination.left; x < split; x += checker) {
                    mPaint.setColor((((x - destination.left) / checker +
                            (y - destination.top) / checker) & 1) == 0 ? 0xffd0d0d0 : 0xffffffff);
                    canvas.drawRect(x, y, Math.min(split, x + checker),
                            Math.min(destination.bottom, y + checker), mPaint);
                }
            }
            mPaint.setColor(Color.WHITE);
            canvas.drawRect(split, destination.top, destination.right, destination.bottom, mPaint);
            canvas.save();
            canvas.clipRect(destination.left, destination.top, split, destination.bottom);
            canvas.drawBitmap(mBitmap, null, destination, mPaint);
            canvas.restore();
            canvas.save();
            canvas.clipRect(split, destination.top, destination.right, destination.bottom);
            canvas.drawBitmap(mBitmap, null, destination, mPaint);
            canvas.restore();
        }

        private Rect fitRect(int availableWidth, int availableHeight) {
            float scale = Math.min(
                    (float) availableWidth / SleepImageCatalogService.PRESENTATION_WIDTH,
                    (float) availableHeight / SleepImageCatalogService.PRESENTATION_HEIGHT);
            int width = Math.max(1,
                    Math.round(SleepImageCatalogService.PRESENTATION_WIDTH * scale));
            int height = Math.max(1,
                    Math.round(SleepImageCatalogService.PRESENTATION_HEIGHT * scale));
            return new Rect((availableWidth - width) / 2, (availableHeight - height) / 2,
                    (availableWidth + width) / 2, (availableHeight + height) / 2);
        }

        @Override protected void onDetachedFromWindow() {
            if (mBitmap != null) { mBitmap.recycle(); mBitmap = null; }
            super.onDetachedFromWindow();
        }
    }
}

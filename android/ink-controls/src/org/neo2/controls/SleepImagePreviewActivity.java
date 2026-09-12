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
    private static final int ACTION_HEIGHT_DP = 48;
    private static final int PAPER=0xfffaf9f5, INK=0xff20231f, LINE=0xffc6c9bf;
    private Button mFit, mCrop;
    private LinearLayout mCropControls;

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
    private boolean mSaving, mSaved;

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
        LinearLayout root = new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(PAPER);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR|View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        if(android.os.Build.VERSION.SDK_INT>=35&&getApplicationInfo().targetSdkVersion>=35)root.setOnApplyWindowInsetsListener((view,insets)->{
            android.graphics.Insets bars=insets.getInsets(android.view.WindowInsets.Type.systemBars()|android.view.WindowInsets.Type.displayCutout());
            view.setPadding(bars.left,bars.top,bars.right,bars.bottom);return insets;
        });
        LinearLayout header=row();header.setPadding(dp(24),0,dp(24),0);header.setBaselineAligned(false);
        TextView title=text("Import sleep image",18);title.setAccessibilityHeading(true);header.addView(title);
        root.addView(header,new LinearLayout.LayoutParams(-1,dp(64)));rule(root);
        android.widget.ScrollView scroll=new android.widget.ScrollView(this);scroll.setFillViewport(true);
        LinearLayout body=new LinearLayout(this);body.setOrientation(LinearLayout.VERTICAL);body.setPadding(0,dp(12),0,dp(12));
        int width=Math.min(580,Math.max(240,getResources().getConfiguration().screenWidthDp-48));
        scroll.addView(body,new android.widget.FrameLayout.LayoutParams(dp(width),-1,Gravity.CENTER_HORIZONTAL));
        root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        mStatus=text("Checking image…",14);mStatus.setMinHeight(dp(28));mStatus.setMaxLines(2);mStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);body.addView(mStatus);
        boolean beside=getResources().getConfiguration().screenWidthDp>=480;
        LinearLayout workspace=new LinearLayout(this);workspace.setOrientation(beside?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);
        workspace.setGravity(Gravity.CENTER_VERTICAL);body.addView(workspace,new LinearLayout.LayoutParams(-1,0,1));
        mPreview=new PanelPreview(this);mPreview.setMinimumHeight(dp(240));mPreview.setContentDescription("Sleep image preview");
        workspace.addView(mPreview,beside?new LinearLayout.LayoutParams(0,-1,1):new LinearLayout.LayoutParams(-1,dp(260)));
        LinearLayout controls=new LinearLayout(this);controls.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams controlsParams=new LinearLayout.LayoutParams(beside?dp(184):-1,-2);
        if(beside)controlsParams.leftMargin=dp(20);workspace.addView(controls,controlsParams);
        mFit=action("Fit");mCrop=action("Crop to fill");
        mFit.setOnClickListener(view->{mFraming=SleepImageTransforms.Framing.FIT;render();});
        mCrop.setOnClickListener(view->{mFraming=SleepImageTransforms.Framing.CROP;render();});
        controls.addView(mFit,stackedAction());controls.addView(mCrop,stackedAction());
        Button rotate=action("Rotate 90°");rotate.setOnClickListener(view->{mRotation=(mRotation+90)%360;render();});controls.addView(rotate,stackedAction());
        mCropControls=new LinearLayout(this);mCropControls.setOrientation(LinearLayout.VERTICAL);
        mHorizontalControl=cropControl("Horizontal crop position");mVerticalControl=cropControl("Vertical crop position");
        mCropControls.addView(text("Horizontal position",14));mCropControls.addView(mHorizontalControl,new LinearLayout.LayoutParams(-1,dp(48)));
        mCropControls.addView(text("Vertical position",14));mCropControls.addView(mVerticalControl,new LinearLayout.LayoutParams(-1,dp(48)));controls.addView(mCropControls);mCropControls.setVisibility(View.GONE);
        rule(controls);
        android.widget.Switch transparency=new android.widget.Switch(this);transparency.setText("Transparency");transparency.setContentDescription("Show overlay samples on checkerboard and white");transparency.setTextSize(14);transparency.setTextColor(INK);transparency.setShowText(false);transparency.setMinimumHeight(dp(48));
        transparency.setOnCheckedChangeListener((view,checked)->{mOverlayPreview=checked;render();});controls.addView(transparency,new LinearLayout.LayoutParams(-1,-2));
        rule(root);LinearLayout decision=row();decision.setPadding(dp(24),dp(12),dp(24),dp(12));
        Button cancel=action("Cancel");cancel.setOnClickListener(view -> finish());decision.addView(cancel,weight());
        mImport=action("Import image");mImport.setEnabled(false);style(mImport,true);mImport.setSelected(false);mImport.setStateDescription(null);mImport.setOnClickListener(view->{if(mSaved){setResult(RESULT_OK);finish();}else commit();});decision.addView(mImport,weight());root.addView(decision);
        style(mFit,true);style(mCrop,false);setContentView(root);
    }
    private LinearLayout.LayoutParams stackedAction(){LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,dp(48));params.bottomMargin=dp(8);return params;}
    private void rule(LinearLayout parent){View line=new View(this);line.setBackgroundColor(LINE);parent.addView(line,new LinearLayout.LayoutParams(-1,dp(1)));}
    private void style(Button button,boolean selected){
        button.setSelected(selected);button.setStateDescription(selected?"Selected":"Not selected");
        android.graphics.drawable.GradientDrawable background=new android.graphics.drawable.GradientDrawable();background.setColor(selected?INK:PAPER);background.setCornerRadius(dp(7));background.setStroke(dp(1),selected?INK:LINE);button.setBackground(background);
        button.setTextColor(new android.content.res.ColorStateList(new int[][]{new int[]{android.R.attr.state_enabled},new int[]{}},new int[]{selected?Color.WHITE:INK,0xff888888}));
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
                mStatus.setText("");
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
        if (mPixels == null || mSaving || mSaved) return;
        mHorizontalControl.setEnabled(mFraming == SleepImageTransforms.Framing.CROP);
        mVerticalControl.setEnabled(mFraming == SleepImageTransforms.Framing.CROP);
        mPlanes = SleepImageTransforms.renderForPanel(
                mPixels, mWidth, mHeight, mRotation, mFraming,
                mHorizontal, mVertical, SleepImageCatalogService.WIDTH,
                SleepImageCatalogService.HEIGHT,
                SleepImageCatalogService.DEVICE_PROFILE.presentationToPanelRotation,
                SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa);
        mPreview.setPlanes(mPlanes, mOverlayPreview);
        mStatus.setText(mFraming==SleepImageTransforms.Framing.FIT?"Fit preserves the whole image.":"Adjust the crop position.");
        updateImportEnabled();
    }

    private void commit() {
        if (mCatalog == null || mPlanes == null || mSaving || mSaved) return;
        mSaving = true;
        mImport.setEnabled(false);
        mStatus.setText("Saving image…");
        SleepImageTransforms.Planes planes = mPlanes;
        mCatalog.commit(mExpectedGeneration, planes.gray, planes.alpha, planes.rgba,
                new SleepImageCatalogService.UiCallback() {
            @Override public void onCatalog(SleepImageCatalogService.CatalogView catalog) { }
            @Override public void onMutation(SleepImageCatalogService.StoreResult store,
                    SleepImageCatalogService.RefreshResult refresh,
                    SleepImageCatalogService.CatalogView catalog) {
                if (store != SleepImageCatalogService.StoreResult.COMMITTED) {
                    mSaving = false;
                    mStatus.setText(store == SleepImageCatalogService.StoreResult.STALE
                            ? "Catalog changed elsewhere; import was not committed"
                            : "Import was not committed");
                    updateImportEnabled();
                    return;
                }
                mSaved = true;
                mSaving = false;
                mImport.setText("Done");
                updateImportEnabled();
                mExpectedGeneration = catalog.generation;
                if (refresh == SleepImageCatalogService.RefreshResult.PENDING) {
                    mStatus.setText("Saved. Waiting for the awake manager to accept generation " +
                            catalog.generation + "…");
                } else if (refresh == SleepImageCatalogService.RefreshResult.ACCEPTED) {
                    setResult(RESULT_OK);
                    finish();
                } else {
                    mStatus.setText("Image saved. The sleep manager could not activate it yet.");
                }
            }
        });
    }

    private void updateImportEnabled() {
        if(mFit!=null){style(mFit,mFraming==SleepImageTransforms.Framing.FIT);style(mCrop,mFraming==SleepImageTransforms.Framing.CROP);mCropControls.setVisibility(mFraming==SleepImageTransforms.Framing.CROP?View.VISIBLE:View.GONE);}
        if (mImport != null) mImport.setEnabled(mSaved || (!mSaving && mCatalog != null && mPlanes != null));
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
        control.setMinimumHeight(dp(48));
        control.setProgressTintList(android.content.res.ColorStateList.valueOf(INK));control.setThumbTintList(android.content.res.ColorStateList.valueOf(INK));
        control.setContentDescription(description);
        control.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                if (bar == mHorizontalControl) mHorizontal = progress / 100f;
                else mVertical = progress / 100f;

            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { render(); }
        });
        return control;
    }

    private Button action(String value) {
        Button button = new Button(this);
        button.setText(value);
        button.setTextSize(16);button.setAllCaps(false);button.setStateListAnimator(null);button.setElevation(0);button.setMinHeight(dp(48));button.setMinimumWidth(dp(48));button.setPadding(dp(12),dp(8),dp(12),dp(8));style(button,false);
        button.setMinimumHeight(dp(ACTION_HEIGHT_DP));
        return button;
    }

    private TextView text(String value, int size) {
        TextView text = new TextView(this);
        text.setText(value);
        text.setTextSize(size);
        text.setTextColor(INK);text.setPadding(0, dp(4), 0, dp(4));
        return text;
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        return row;
    }

    private LinearLayout.LayoutParams weight() {
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);params.setMargins(dp(3),dp(6),dp(3),dp(6));return params;
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
                    planes, SleepImageCatalogService.WIDTH,
                    SleepImageCatalogService.HEIGHT, SleepImageCatalogService.PRESENTATION_WIDTH,
                    SleepImageCatalogService.PRESENTATION_HEIGHT,
                    SleepImageCatalogService.DEVICE_PROFILE.presentationToPanelRotation);
            if (!overlay) {
                for (int index = 0; index < pixels.length; ++index) {
                    int argb = pixels[index];
                    int a = argb >>> 24;
                    int r = (((argb >>> 16) & 255) * a + 255 * (255 - a) + 127) / 255;
                    int g = (((argb >>> 8) & 255) * a + 255 * (255 - a) + 127) / 255;
                    int b = ((argb & 255) * a + 255 * (255 - a) + 127) / 255;
                    pixels[index] = 0xff000000 | (r << 16) | (g << 8) | b;
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

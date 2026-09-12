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
    private static final String MANAGER_SERVICE = "neo2_frontlight";
    private static final String MANAGER_DESCRIPTOR = "android.neo2.INeo2FrontLightManager";
    private static final String DIRECT_SERVICE = "neo2_frontlight_controls";
    private static final String DIRECT_DESCRIPTOR = "org.neo2.controls.IFrontLightControl";
    private static final int APPLY = IBinder.FIRST_CALL_TRANSACTION;
    private static final int INITIAL_PERCENT = 50;
    private static final int ADJUSTMENT_STEP_PERCENT = 5;
    private static final int SECTION_LABEL_SP = 18;
    private static final int VALUE_SP = 18;
    private static final int BUTTON_SP = 16;
    private static final int CONTROL_HEIGHT_DP = 48;
    private static final int ACTION_HEIGHT_DP = 48;
    private static final int THUMBNAIL_WIDTH = SleepImageCatalogService.THUMBNAIL_WIDTH;
    private static final int THUMBNAIL_HEIGHT = SleepImageCatalogService.THUMBNAIL_HEIGHT;
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
    private android.widget.Switch colorQuality;
    private boolean readingQuality;
    private android.widget.Switch vividColor;
    private Button fastProfile, hdProfile;
    private int vividMode=-1, qualityMode=-1;
    private boolean applyingProfile;
    private String preferredDisplayProfile="speed";
    private int[] displayProfile(String name){
        try{org.json.JSONObject profiles=new org.json.JSONObject(Settings.Global.getString(getContentResolver(),"neo2_display_profiles"));
            org.json.JSONObject p=profiles.getJSONObject(name);int v=p.getInt("vivid"),q=p.getInt("quality"),g=p.getInt("gray");
            if(v>=0&&v<=1&&q>=0&&q<=1&&g>=0&&g<=2)return new int[]{v,q,g};
        }catch(Exception ignored){}
        return new int[]{1,name.equals("hd")?1:0,1};
    }
    private boolean matchesProfile(String name){int[] p=displayProfile(name);return vividMode==p[0]&&qualityMode==p[1]&&grayPreferenceMode==p[2];}
    private void updateDisplayProfiles(){
        if(fastProfile==null)return;
        boolean fast=matchesProfile("speed"),hd=matchesProfile("hd");
        selected(fastProfile,fast&&(!hd||preferredDisplayProfile.equals("speed")));
        selected(hdProfile,hd&&(!fast||preferredDisplayProfile.equals("hd")));
        boolean ready=!applyingProfile&&vividMode>=0&&qualityMode>=0&&grayPreferenceMode>=0;
        fastProfile.setEnabled(ready);hdProfile.setEnabled(ready);
        if(vividColor!=null)vividColor.setEnabled(!applyingProfile&&vividMode>=0);
        if(colorQuality!=null)colorQuality.setEnabled(!applyingProfile&&qualityMode>=0);
        for(Button b:grayChoices)if(b!=null)b.setEnabled(!applyingProfile&&grayPreferenceMode>=0);
    }
    private void applyDisplayProfile(String name){
        if(applyingProfile)return;preferredDisplayProfile=name;applyingProfile=true;updateDisplayProfiles();int[] p=displayProfile(name);
        ColorQuality.configure(this,p[1],q->{showQuality(q);if(q!=p[1]){finishDisplayProfile(false);return;}
            GrayPreference.configure(this,p[2],(g,saved,queued)->{showGrayPreference(g);if(g!=p[2]||!saved){finishDisplayProfile(false);return;}
                VividColor.configure(p[0],(v,redraw)->{showVivid(v);finishDisplayProfile(v==p[0]);});});});
    }
    private void finishDisplayProfile(boolean success){
        applyingProfile=false;updateDisplayProfiles();
        if(success)new android.os.Handler(getMainLooper()).postDelayed(()->{if(!isFinishing())DisplayRefresh.request(this);},600);
        else android.widget.Toast.makeText(this,"Profile partly applied; check display options",android.widget.Toast.LENGTH_LONG).show();
    }

    private boolean readingVivid;
    private Button grayPreference;
    private final Button[] grayChoices = new Button[3];
    private int grayPreferenceMode = -1;
    private static final int PAPER = 0xfffaf9f5, INK = 0xff20231f, LINE = 0xffc6c9bf;
    private boolean navigationRail;
    private final Button[] navigation = new Button[4];
    private final ScrollView[] pageViews = new ScrollView[4];
    private int activePage, catalogPage;
    private String selectedImageId;
    private SleepImageCatalogService.CatalogView visibleCatalog;
    private android.widget.FrameLayout pageHost;
    private LinearLayout catalogActions;
    private Button sleepPicker, previousImages, nextImages;
    private TextView imagePage, deviceDiagnostics, lightStatus, catalogDiagnostics;
    private android.widget.Switch lightPower;
    private boolean readingLight;
    private LinearLayout lightContents;
    private int lightReadGeneration;
    private boolean adjustingLight;
    private int lastNonzero = 50;
    private final java.util.concurrent.ExecutorService lightWorker = java.util.concurrent.Executors.newSingleThreadExecutor();
    private int lightRequest;
    private boolean alive = true;
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
            setInlineStatus(catalogStatus,"Catalog service is unavailable");
        }
    };
    private final SleepImageCatalogService.UiCallback catalogCallback =
            new SleepImageCatalogService.UiCallback() {
        @Override public void onCatalog(SleepImageCatalogService.CatalogView catalog) {
            showCatalog(catalog);
            setInlineStatus(catalogStatus,"");
        }
        @Override public void onMutation(SleepImageCatalogService.StoreResult store,
                SleepImageCatalogService.RefreshResult refresh,
                SleepImageCatalogService.CatalogView catalog) {
            if (store == SleepImageCatalogService.StoreResult.COMMITTED) {
                showCatalog(catalog);
                if (refresh == SleepImageCatalogService.RefreshResult.PENDING) {
                    setInlineStatus(catalogStatus,"Catalog saved; manager synchronization pending");
                } else if (refresh == SleepImageCatalogService.RefreshResult.ACCEPTED) {
                    setInlineStatus(catalogStatus,"Catalog saved and accepted by the awake manager");
                } else {
                    setInlineStatus(catalogStatus,"Catalog saved; manager kept its prior valid snapshot");
                }
            } else if (store == SleepImageCatalogService.StoreResult.STALE) {
                showCatalog(catalog);
                setInlineStatus(catalogStatus,"Catalog changed elsewhere; action was rejected");
            } else if (store == SleepImageCatalogService.StoreResult.PROTECTED) {
                setInlineStatus(catalogStatus,"The built-in fallback is protected");
            } else if (store == SleepImageCatalogService.StoreResult.FINAL_CANDIDATE) {
                setInlineStatus(catalogStatus,"The final valid sleep image cannot be removed");
            } else {
                setInlineStatus(catalogStatus,"Catalog was not changed");
            }
        }
    };

    private void showGrayPreference(int mode) {
        if (isFinishing()) return;
        grayPreferenceMode = mode;
        for (int i=0;i<grayChoices.length;i++) if(grayChoices[i]!=null) {
            grayChoices[i].setEnabled(mode>=0);
            selected(grayChoices[i],i==mode);
        }
        updateDisplayProfiles();
    }
    private void selectGray(int requested) {
        for(Button b:grayChoices)b.setEnabled(false);
        GrayPreference.configure(this,requested,(mode,saved,queued)->{
            showGrayPreference(mode);
            if(mode<0||!saved)android.widget.Toast.makeText(this,"Gray preference could not be saved",android.widget.Toast.LENGTH_SHORT).show();
        });
    }

    private void showVivid(int state) {
        if (vividColor == null || isFinishing()) return;
        readingVivid = true;
        vividColor.setChecked(state == 1);
        vividColor.setEnabled(state >= 0);
        readingVivid = false;vividMode=state;updateDisplayProfiles();
    }

    private void showQuality(int state) {
        if (colorQuality == null || isFinishing()) return;
        readingQuality = true;
        colorQuality.setChecked(state == 1);
        colorQuality.setEnabled(state >= 0);
        readingQuality = false;qualityMode=state;updateDisplayProfiles();
    }
    @Override protected void onResume() {
        super.onResume();
        if (vividColor != null) VividColor.configure(-1, (mode, queued) -> showVivid(mode));
        if (colorQuality != null) ColorQuality.configure(this, -1, this::showQuality);
        if (grayChoices[0] != null) GrayPreference.configure(this, -1,
                (mode, saved, queued) -> showGrayPreference(mode));
        if(deviceDiagnostics!=null)updateDiagnostics();
        readCurrentLight();
    }

    @Override public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        if(savedState!=null){activePage=savedState.getInt("page",0);catalogPage=savedState.getInt("catalogPage",0);selectedImageId=savedState.getString("selectedImage");}
        android.content.SharedPreferences prefs=getSharedPreferences("light_requests",MODE_PRIVATE);
        lastNonzero=Math.max(1,Math.min(100,prefs.getInt("lastNonzero",50)));
        status=label("No light request in this session");
        catalogDiagnostics=label("Catalog not loaded");
        boolean rail=getResources().getConfiguration().screenWidthDp>=600;navigationRail=rail;
        LinearLayout shell=new LinearLayout(this);shell.setOrientation(rail?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);
        shell.setBackgroundColor(PAPER);
        LinearLayout outer=column();outer.setBackgroundColor(PAPER);
        getWindow().getDecorView().setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR|android.view.View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        if(android.os.Build.VERSION.SDK_INT>=35&&getApplicationInfo().targetSdkVersion>=35){
            outer.setOnApplyWindowInsetsListener((view,insets)->{
                android.graphics.Insets bars=insets.getInsets(android.view.WindowInsets.Type.systemBars()|android.view.WindowInsets.Type.displayCutout());
                view.setPadding(bars.left,bars.top,bars.right,bars.bottom);return insets;
            });
        }
        LinearLayout titleBar=row();titleBar.setBaselineAligned(false);titleBar.setPadding(dp(28),0,dp(28),0);
        TextView appName=label("Ink Controls");appName.setTextSize(22);appName.setTypeface(android.graphics.Typeface.DEFAULT,android.graphics.Typeface.BOLD);titleBar.addView(appName,new LinearLayout.LayoutParams(0,-2,1));
        TextView model=note(SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa?"AURA C":SleepImageCatalogService.DEVICE_PROFILE.name.toUpperCase(java.util.Locale.ROOT));model.setTextSize(12);model.setLetterSpacing(0.08f);titleBar.addView(model);
        outer.addView(titleBar,new LinearLayout.LayoutParams(-1,dp(rail?76:68)));rule(outer,false);
        outer.addView(shell,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout nav=new LinearLayout(this);nav.setOrientation(rail?LinearLayout.VERTICAL:LinearLayout.HORIZONTAL);
        nav.setPadding(dp(rail?12:8),dp(rail?22:8),dp(rail?12:8),dp(rail?22:8));
        String[] names={"Light","Color","Sleep","More"};
        for(int i=0;i<names.length;i++){
            final int page=i;Button button=actionButton(names[i]);navigation[i]=button;
            button.setGravity(rail?Gravity.LEFT|Gravity.CENTER_VERTICAL:Gravity.CENTER);
            button.setPadding(dp(rail?18:8),dp(10),dp(rail?18:8),dp(10));
            if(rail){NavigationIcon icon=new NavigationIcon(i);icon.setBounds(0,0,dp(19),dp(19));button.setCompoundDrawablesRelative(icon,null,null,null);button.setCompoundDrawablePadding(dp(10));}
            button.setOnClickListener(v->showPage(page));
            LinearLayout.LayoutParams params=rail?new LinearLayout.LayoutParams(-1,-2):new LinearLayout.LayoutParams(0,-2,1);
            if(rail)params.bottomMargin=dp(8);else params.setMargins(dp(2),0,dp(2),0);
            nav.addView(button,params);
            if((i==0&&!SleepImageCatalogService.DEVICE_PROFILE.hasFrontLight)||(i==1&&!SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa))button.setVisibility(android.view.View.GONE);
        }
        shell.addView(nav,rail?new LinearLayout.LayoutParams(dp(160),-1):new LinearLayout.LayoutParams(-1,-2));
        rule(shell,rail);
        pageHost=new android.widget.FrameLayout(this);
        shell.addView(pageHost,rail?new LinearLayout.LayoutParams(0,-1,1):new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout light=page(0,rail),display=page(1,rail),sleep=page(2,rail),more=page(3,rail);
        if (SleepImageCatalogService.DEVICE_PROFILE.hasFrontLight) buildLight(light,prefs);
        else light.addView(label("This model has no front light"));
        buildDisplay(display);buildSleep(sleep);buildMore(more);
        setContentView(outer);showPage(activePage);
        Intent catalog = new Intent(this, SleepImageCatalogService.class).setAction(SleepImageCatalogService.ACTION_MANAGE);
        catalogBound = bindService(catalog, catalogConnection, Context.BIND_AUTO_CREATE);
    }
    private LinearLayout page(int index,boolean rail){
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setClipToPadding(false);
        scroll.setPadding(dp(rail?32:24),dp(rail?32:20),dp(rail?32:24),dp(20));
        // Scrolling is a fallback for enlarged accessibility text or short windows.
        // Normal portrait pages fit; navigation is outside the scrolling surface.
        LinearLayout content=column();
        int width=Math.min(580,Math.max(240,getResources().getConfiguration().screenWidthDp-(rail?160:0)-(rail?64:48)));
        scroll.addView(content,new android.widget.FrameLayout.LayoutParams(dp(width),-2,Gravity.TOP|Gravity.CENTER_HORIZONTAL));
        pageHost.addView(scroll,new android.widget.FrameLayout.LayoutParams(-1,-1));pageViews[index]=scroll;
        return content;
    }
    @Override public void onWindowFocusChanged(boolean focused){
        super.onWindowFocusChanged(focused);if(focused&&activePage==0)readCurrentLight();
    }
    private void readCurrentLight(){
        if(lightContents==null||!alive||adjustingLight)return;
        final int generation=++lightReadGeneration,request=lightRequest;
        lightWorker.execute(()->{
            int[] current=null;IBinder manager=ServiceManager.checkService(MANAGER_SERVICE);
            if(manager==null)android.util.Log.w("Neo2Controls","Front-light manager unavailable");
            if(manager!=null){Parcel data=Parcel.obtain(),reply=Parcel.obtain();
                try{data.writeInterfaceToken(MANAGER_DESCRIPTOR);
                    if(manager.transact(IBinder.FIRST_CALL_TRANSACTION+3,data,reply,0)){reply.readException();int[] values=reply.createIntArray();
                        if(values!=null&&values.length==3&&(values[0]==0||values[0]==1)&&values[1]>=0&&values[1]<=100&&values[2]>=0&&values[2]<=100)current=values;else android.util.Log.w("Neo2Controls","Unexpected front-light state: "+java.util.Arrays.toString(values));}
                }catch(RemoteException|RuntimeException failure){android.util.Log.w("Neo2Controls","Front-light read failed",failure);}finally{data.recycle();reply.recycle();}}
            final int[] values=current;
            runOnUiThread(()->{
                if(!alive||generation!=lightReadGeneration||request!=lightRequest||adjustingLight)return;
                if(values==null){lightContents.setVisibility(android.view.View.INVISIBLE);setInlineStatus(lightStatus,"Current light settings unavailable");return;}
                readingLight=true;brightness.setProgress(values[0]==1?values[1]:0);warmth.setProgress(values[2]);lightPower.setChecked(values[0]==1);readingLight=false;
                if(values[1]>0)lastNonzero=values[1];
                setInlineStatus(lightStatus,"");lightContents.setVisibility(android.view.View.VISIBLE);
            });
        });
    }
    private void showPage(int page){
        activePage=Math.max(0,Math.min(3,page));
        if(activePage==1&&!SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa)activePage=0;
        if(activePage==0&&!SleepImageCatalogService.DEVICE_PROFILE.hasFrontLight)activePage=2;
        for(int i=0;i<4;i++){pageViews[i].setVisibility(i==activePage?android.view.View.VISIBLE:android.view.View.GONE);navigationStyle(navigation[i],i==activePage);}
        if(activePage==0)readCurrentLight();
    }
    @Override protected void onSaveInstanceState(Bundle out){
        out.putInt("page",activePage);out.putInt("catalogPage",catalogPage);out.putString("selectedImage",selectedImageId);super.onSaveInstanceState(out);
    }
    private void buildLight(LinearLayout root,android.content.SharedPreferences prefs){
        lightStatus=label("Reading current light settings…");
        lightStatus.setTextSize(14);lightStatus.setMinHeight(dp(40));
        if(!SleepImageCatalogService.DEVICE_PROFILE.hasFrontLight){root.addView(label("This model has no front light"));return;}
        lightContents=column();lightContents.setVisibility(android.view.View.INVISIBLE);root.addView(lightContents);root.addView(lightStatus);root=lightContents;
        brightness=slider(0);
        warmth=slider(0);
        lightPower=toggle("Front light");readingLight=true;lightPower.setChecked(brightness.getProgress()>0);readingLight=false;
        lightPower.setOnCheckedChangeListener((button,checked)->{
            if(readingLight)return;
            brightness.setProgress(checked?lastNonzero:0);applyCurrentLight();
        });
        root.addView(lightPower);space(root,20);rule(root,false);space(root,24);
        root.addView(controlSection("Brightness",brightness));
        root.addView(controlSection("Warmth",warmth));
    }
    private void buildDisplay(LinearLayout root){
        if(SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa){
            root.addView(heading("Display profile"));LinearLayout profiles=row();
            fastProfile=actionButton("Fast");hdProfile=actionButton("HD");
            fastProfile.setOnClickListener(v->applyDisplayProfile("speed"));hdProfile.setOnClickListener(v->applyDisplayProfile("hd"));
            profiles.addView(fastProfile,new LinearLayout.LayoutParams(0,-2,1));profiles.addView(hdProfile,new LinearLayout.LayoutParams(0,-2,1));root.addView(profiles);
            root.addView(note("Presets combine the options below. Customize them in Panel testing."));separator(root);updateDisplayProfiles();
            root.addView(heading("Gray preference"));
            LinearLayout choices=row();
            for(int i=0;i<3;i++){
                final int mode=i;Button b=actionButton(GrayPreference.LABELS[i]);grayChoices[i]=b;b.setEnabled(false);b.setOnClickListener(v->selectGray(mode));
                LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,-2,1);lp.setMargins(dp(i==0?0:4),0,0,0);choices.addView(b,lp);
            }
            root.addView(choices);root.addView(note("Original keeps source color. Balanced neutralizes faint tints; Stronger also neutralizes muted colors."));separator(root);
            colorQuality=toggle("Color quality");colorQuality.setEnabled(false);
            colorQuality.setOnCheckedChangeListener((button,checked)->{
                if(readingQuality)return;colorQuality.setEnabled(false);
                ColorQuality.configure(this,checked?1:0,result->{showQuality(result);
                    if(result<0)android.widget.Toast.makeText(this,"Color quality unavailable",android.widget.Toast.LENGTH_SHORT).show();
                    if(result==1)new AlertDialog.Builder(this).setTitle("Clean colors now?").setMessage("Briefly whiten colored areas to clear existing ghosting.")
                        .setPositiveButton("Clean colors",(dialog,which)->ColorQuality.cleanup(this)).setNegativeButton("Later",null).show();
                });
            });
            root.addView(colorQuality);root.addView(note("May flash white and take longer to update."));
            LinearLayout refreshRow=row();refreshRow.setGravity(Gravity.RIGHT);
            Button refresh=actionButton("Full refresh");refresh.setContentDescription("Full-waveform display refresh");refresh.setOnClickListener(v->DisplayRefresh.request(this));refreshRow.addView(refresh);root.addView(refreshRow);
            separator(root);root.addView(note("BETA"));
            vividColor=toggle("Vivid color");vividColor.setEnabled(false);
            vividColor.setOnCheckedChangeListener((button,checked)->{
                if(readingVivid)return;vividColor.setEnabled(false);
                VividColor.configure(checked?1:0,(mode,queued)->{showVivid(mode);if(mode<0)android.widget.Toast.makeText(this,"Vivid color unavailable",android.widget.Toast.LENGTH_SHORT).show();});
            });
            root.addView(vividColor);root.addView(note("Richer colors while preserving neutral grays. Resets after restart."));
            separator(root);

            root.addView(heading("Panel testing"));root.addView(note("Compare color, detail and speed on the same content."));
            Button testing=actionButton("Open panel testing");testing.setOnClickListener(v->{
                startActivity(new Intent(this,org.neo2.controls.panel.ComparisonActivity.class));
            });root.addView(testing);separator(root);

        }else root.addView(label("Color adjustments are not available on this display."));
    }
    private void buildSleep(LinearLayout root){
        root.addView(heading("When the screen turns off"));sleepPicker=actionButton("Sleep image");sleepPicker.setOnClickListener(v->chooseSleepMode());root.addView(sleepPicker);
        sleepModeStatus=note("");sleepModeStatus.setMinHeight(dp(30));root.addView(sleepModeStatus);updateSleepImageMode();space(root,12);
        LinearLayout collection=row();TextView title=heading("Image collection");collection.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        Button add=actionButton("Import");add.setOnClickListener(v->chooseSleepImage());collection.addView(add);root.addView(collection);
        catalogItems=column();root.addView(catalogItems);
        LinearLayout paging=row();previousImages=actionButton("Previous");nextImages=actionButton("Next");imagePage=label("");imagePage.setGravity(Gravity.CENTER);
        previousImages.setOnClickListener(v->{catalogPage--;renderCatalog();});nextImages.setOnClickListener(v->{catalogPage++;renderCatalog();});
        paging.addView(previousImages);paging.addView(imagePage,new LinearLayout.LayoutParams(0,-2,1));paging.addView(nextImages);root.addView(paging);
        catalogActions=column();root.addView(catalogActions);
        catalogStatus=note("Loading images…");catalogStatus.setAccessibilityLiveRegion(android.view.View.ACCESSIBILITY_LIVE_REGION_POLITE);root.addView(catalogStatus);
    }
    private void chooseSleepMode(){
        final int[] modes={MODE_SLEEP_IMAGE,MODE_KOREADER_COVER,MODE_OVERLAY_SLEEP_IMAGE,MODE_KEEP_LAST_SCREEN};
        String[] labels={"Sleep image","KOReader cover","Overlay sleep image","Keep last screen"};
        int actual=Settings.Global.getInt(getContentResolver(),SLEEP_IMAGE_MODE,MODE_SLEEP_IMAGE),checked=0;
        for(int i=0;i<modes.length;i++)if(modes[i]==actual)checked=i;
        android.widget.ArrayAdapter<String> adapter=new android.widget.ArrayAdapter<String>(this,android.R.layout.simple_list_item_single_choice,labels){
            @Override public android.view.View getView(int position,android.view.View convert,android.view.ViewGroup parent){android.view.View view=super.getView(position,convert,parent);view.setMinimumHeight(dp(64));return view;}
        };
        new AlertDialog.Builder(this).setTitle("When the screen turns off").setSingleChoiceItems(adapter,checked,(dialog,which)->{dialog.dismiss();setSleepImageMode(modes[which]);}).setNegativeButton("Cancel",null).show();
    }
    private void buildMore(LinearLayout root){
        if(SleepImageCatalogService.DEVICE_PROFILE.hasColorCfa){
            Button cleanup=actionButton("App-switch refresh");cleanup.setOnClickListener(v->startActivity(new Intent(this,org.neo2.controls.panel.RefreshDiagnosticsActivity.class)));root.addView(cleanup);separator(root);
        }
        root.addView(heading("Diagnostics"));deviceDiagnostics=label("");root.addView(deviceDiagnostics);root.addView(status);root.addView(catalogDiagnostics);
        root.addView(note("Requested or queued operations do not prove physical panel completion."));updateDiagnostics();
    }
    private void updateDiagnostics(){
        android.util.DisplayMetrics metrics=getResources().getDisplayMetrics();
        deviceDiagnostics.setText("Device: "+SleepImageCatalogService.DEVICE_PROFILE.name+"\nAndroid "+android.os.Build.VERSION.RELEASE+"\nDensity: "+metrics.densityDpi+" dpi · font scale "+getResources().getConfiguration().fontScale+"\nDisplay control endpoint: "+(ServiceManager.checkService(DIRECT_SERVICE)!=null?"registered":"not registered")+"\nSleep manager: "+(ServiceManager.checkService(SLEEP_IMAGE_SERVICE)!=null?"registered":"not registered"));
    }
    private LinearLayout controlSection(String title,SeekBar bar){
        LinearLayout section=column();
        LinearLayout header=row();header.addView(heading(title),new LinearLayout.LayoutParams(0,-2,1));TextView value=label(bar.getProgress()+"%");value.setTextSize(VALUE_SP);header.addView(value);section.addView(header);space(section,6);
        LinearLayout range=row();Button lower=adjustmentButton("−");lower.setContentDescription("Decrease "+title+" by 5 percent");lower.setOnClickListener(v->adjust(bar,-5));
        Button raise=adjustmentButton("+");raise.setContentDescription("Increase "+title+" by 5 percent");raise.setOnClickListener(v->adjust(bar,5));
        range.addView(lower,new LinearLayout.LayoutParams(dp(48),dp(48)));LinearLayout.LayoutParams trackSpace=new LinearLayout.LayoutParams(0,dp(48),1);trackSpace.setMargins(dp(10),0,dp(10),0);range.addView(bar,trackSpace);range.addView(raise,new LinearLayout.LayoutParams(dp(48),dp(48)));section.addView(range);
        LinearLayout detents=row();detents.setPadding(dp(48),0,dp(48),0);for(int level:new int[]{0,25,50,75,100}){Button detent=adjustmentButton("│\n"+level);detent.setTextSize(12);detent.setPadding(0,0,0,0);detent.setBackgroundColor(PAPER);detent.setStateListAnimator(null);detent.setElevation(0);detent.setContentDescription(title+" "+level+" percent");detent.setOnClickListener(v->{bar.setProgress(level);applyCurrentLight();});detents.addView(detent,new LinearLayout.LayoutParams(0,-2,1));}section.addView(detents);
        LinearLayout ends=row();ends.setPadding(dp(58),0,dp(58),0);TextView low=note(bar==brightness?"Off":"Cool"),high=note(bar==brightness?"Bright":"Warm");low.setTextSize(12);high.setTextSize(12);ends.addView(low,new LinearLayout.LayoutParams(0,-2,1));ends.addView(high);section.addView(ends);space(section,18);rule(section,false);space(section,18);
        bar.setContentDescription(title);bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            @Override public void onProgressChanged(SeekBar seekBar,int progress,boolean fromUser){updateValue(value,progress);if(seekBar==brightness&&lightPower!=null&&!readingLight){readingLight=true;lightPower.setChecked(progress>0);readingLight=false;}}
            @Override public void onStartTrackingTouch(SeekBar seekBar){adjustingLight=true;}
            @Override public void onStopTrackingTouch(SeekBar seekBar){adjustingLight=false;applyCurrentLight();}
        });return section;
    }
    private void applyCurrentLight(){apply(brightness.getProgress(),warmth.getProgress());}
    private android.widget.Switch toggle(String text){
        android.widget.Switch view=new android.widget.Switch(this);view.setText(text);view.setTextSize(18);view.setTextColor(android.graphics.Color.BLACK);view.setMinHeight(dp(48));view.setSwitchMinWidth(dp(60));view.setSwitchPadding(dp(16));
        android.graphics.drawable.StateListDrawable track=new android.graphics.drawable.StateListDrawable(),thumb=new android.graphics.drawable.StateListDrawable();
        track.addState(new int[]{android.R.attr.state_checked},switchShape(60,34,0xff222222));track.addState(new int[]{},switchShape(60,34,0xffeeeeee));
        thumb.addState(new int[]{android.R.attr.state_checked},switchShape(28,28,0xffffffff));thumb.addState(new int[]{},switchShape(28,28,0xff222222));
        view.setTrackTintList(null);view.setThumbTintList(null);view.setTrackDrawable(track);view.setThumbDrawable(thumb);return view;
    }
    private android.graphics.drawable.GradientDrawable switchShape(int width,int height,int color){
        android.graphics.drawable.GradientDrawable shape=new android.graphics.drawable.GradientDrawable();shape.setColor(color);shape.setCornerRadius(dp(height/2));shape.setSize(dp(width),dp(height));shape.setStroke(dp(1),0xff222222);return shape;
    }
    private LinearLayout column(){LinearLayout layout=new LinearLayout(this);layout.setOrientation(LinearLayout.VERTICAL);return layout;}
    private LinearLayout row(){LinearLayout layout=new LinearLayout(this);layout.setGravity(Gravity.CENTER_VERTICAL);return layout;}
    private void rule(LinearLayout root,boolean vertical){android.view.View line=new android.view.View(this);line.setBackgroundColor(LINE);root.addView(line,new LinearLayout.LayoutParams(vertical?dp(1):-1,vertical?-1:dp(1)));}
    private void navigationStyle(Button button,boolean selected){
        selected(button,selected);android.graphics.drawable.GradientDrawable background=new android.graphics.drawable.GradientDrawable();background.setColor(selected?INK:PAPER);background.setCornerRadius(dp(8));button.setBackground(background);
        android.graphics.drawable.Drawable icon=button.getCompoundDrawablesRelative()[0];if(icon instanceof NavigationIcon)((NavigationIcon)icon).color(selected?PAPER:INK);
    }
    private final class NavigationIcon extends android.graphics.drawable.Drawable {
        private final int kind;private int color=INK;private final android.graphics.Paint paint=new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);
        NavigationIcon(int kind){this.kind=kind;}
        void color(int value){color=value;invalidateSelf();}
        @Override public void draw(android.graphics.Canvas canvas){
            canvas.save();canvas.translate(getBounds().left,getBounds().top);canvas.scale(getBounds().width()/19f,getBounds().height()/19f);paint.setColor(color);paint.setStrokeWidth(1.5f);paint.setStyle(android.graphics.Paint.Style.STROKE);
            if(kind==0){canvas.drawCircle(9.5f,9.5f,3.5f,paint);for(int i=0;i<8;i++){double a=i*Math.PI/4;canvas.drawLine(9.5f+(float)Math.cos(a)*6,9.5f+(float)Math.sin(a)*6,9.5f+(float)Math.cos(a)*8.5f,9.5f+(float)Math.sin(a)*8.5f,paint);}}
            else if(kind==1){android.graphics.Path palette=new android.graphics.Path();palette.moveTo(10,2);palette.cubicTo(0,1,0,17,9,17);palette.cubicTo(13,17,10,12,14,12);palette.cubicTo(20,12,18,3,10,2);palette.close();canvas.drawPath(palette,paint);paint.setStyle(android.graphics.Paint.Style.FILL);canvas.drawCircle(5,8,1,paint);canvas.drawCircle(7,5,1,paint);canvas.drawCircle(11,5,1,paint);canvas.drawCircle(14,8,1,paint);}
            else if(kind==2){android.graphics.Path moon=new android.graphics.Path(),cut=new android.graphics.Path();moon.addCircle(9,10,7,android.graphics.Path.Direction.CW);cut.addCircle(12,7,6.5f,android.graphics.Path.Direction.CW);moon.op(cut,android.graphics.Path.Op.DIFFERENCE);paint.setStyle(android.graphics.Paint.Style.FILL);canvas.drawPath(moon,paint);}
            else{paint.setStyle(android.graphics.Paint.Style.FILL);for(int i=0;i<3;i++)canvas.drawCircle(4+i*5.5f,9.5f,1.1f,paint);}canvas.restore();
        }
        @Override public void setAlpha(int alpha){paint.setAlpha(alpha);}
        @Override public void setColorFilter(android.graphics.ColorFilter filter){paint.setColorFilter(filter);}
        @Override public int getOpacity(){return android.graphics.PixelFormat.TRANSLUCENT;}
    }
    private void space(LinearLayout root,int height){android.view.View gap=new android.view.View(this);root.addView(gap,new LinearLayout.LayoutParams(1,dp(height)));}
    private void separator(LinearLayout root){space(root,16);android.view.View line=new android.view.View(this);line.setBackgroundColor(0xffcccccc);root.addView(line,new LinearLayout.LayoutParams(-1,dp(1)));space(root,12);}
    private TextView label(String value){TextView view=new TextView(this);view.setText(value);view.setTextColor(0xff222222);view.setTextSize(16);view.setPadding(0,dp(6),0,dp(6));return view;}
    private TextView heading(String value){TextView view=label(value);view.setTextSize(SECTION_LABEL_SP);view.setAccessibilityHeading(true);return view;}
    private void setInlineStatus(TextView view,String text){view.setText(text);view.setVisibility(text.isEmpty()?android.view.View.GONE:android.view.View.VISIBLE);}
    private TextView note(String value){TextView view=label(value);view.setTextSize(14);view.setTextColor(0xff555555);return view;}
    private SeekBar slider(int value){SeekBar bar=new SeekBar(this);bar.setMax(100);bar.setProgress(value);bar.setMinimumHeight(dp(CONTROL_HEIGHT_DP));bar.setProgressTintList(android.content.res.ColorStateList.valueOf(0xff222222));bar.setThumbTintList(android.content.res.ColorStateList.valueOf(0xff222222));return bar;}
    private Button adjustmentButton(String text){return actionButton(text);}
    private Button actionButton(String text){Button button=new Button(this);button.setText(text);button.setAllCaps(false);button.setStateListAnimator(null);button.setElevation(0);button.setTextSize(BUTTON_SP);button.setMinimumHeight(dp(ACTION_HEIGHT_DP));button.setMinHeight(dp(48));button.setMinWidth(dp(48));button.setMinimumWidth(dp(48));button.setPadding(dp(10),dp(8),dp(10),dp(8));selected(button,false);button.setStateDescription(null);return button;}
    private void selected(Button button,boolean selected){
        button.setSelected(selected);button.setActivated(selected);button.setStateDescription(selected?"Selected":"Not selected");
        android.graphics.drawable.GradientDrawable background=new android.graphics.drawable.GradientDrawable();background.setColor(selected?INK:PAPER);background.setCornerRadius(dp(7));background.setStroke(dp(1),selected?INK:LINE);button.setBackground(background);button.setTextColor(new android.content.res.ColorStateList(new int[][]{new int[]{android.R.attr.state_enabled},new int[]{}},new int[]{selected?android.graphics.Color.WHITE:0xff222222,0xff888888}));
    }
    private void adjust(SeekBar bar,int delta){bar.setProgress(Math.max(0,Math.min(100,bar.getProgress()+delta)));applyCurrentLight();}
    private void updateValue(TextView value,int percent){value.setText(percent+"%");}
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}

    private void chooseSleepImage() {
        if (catalogGeneration <= 0) {
            setInlineStatus(catalogStatus,"Wait for the catalog to finish loading");
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
            setInlineStatus(sleepModeStatus,"Sleep behavior service is unavailable");
            return;
        }
        final Parcel data = Parcel.obtain();
        final Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(SLEEP_IMAGE_DESCRIPTOR);
            data.writeInt(mode);
            if (!service.transact(SET_SLEEP_IMAGE_MODE, data, reply, 0)) {
                setInlineStatus(sleepModeStatus,"Sleep behavior was not changed");
                return;
            }
            reply.readException();
            if (reply.readInt() != SLEEP_IMAGE_MODE_QUEUED) {
                setInlineStatus(sleepModeStatus,"Sleep behavior was not changed");
                return;
            }
            sleepPicker.setText(sleepModeName(mode));
            setInlineStatus(sleepModeStatus,"Change queued");
        } catch (RemoteException | RuntimeException ignored) {
            setInlineStatus(sleepModeStatus,"Sleep behavior was not changed");
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    private void updateSleepImageMode() {
        final int mode = Settings.Global.getInt(getContentResolver(), SLEEP_IMAGE_MODE,
                MODE_SLEEP_IMAGE);
        sleepPicker.setText(sleepModeName(mode));
        setInlineStatus(sleepModeStatus,"");
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
        catalogGeneration=catalog.generation;visibleCatalog=catalog;
        catalogDiagnostics.setText("Catalog generation "+catalog.generation+" · "+catalog.items.size()+" images");
        renderCatalog();
    }
    private void renderCatalog(){
        if(visibleCatalog==null)return;
        SleepImageCatalogService.CatalogView catalog=visibleCatalog;
        int columns=getResources().getConfiguration().screenWidthDp>=600?4:3,perPage=columns*2;
        int pageCount=Math.max(1,(catalog.items.size()+perPage-1)/perPage);catalogPage=Math.max(0,Math.min(catalogPage,pageCount-1));
        previousImages.setEnabled(catalogPage>0);nextImages.setEnabled(catalogPage+1<pageCount);imagePage.setText((catalogPage+1)+" / "+pageCount);
        catalogItems.removeAllViews();catalogActions.removeAllViews();
        SleepImageCatalogService.CatalogItem selectedItem=null;
        for(SleepImageCatalogService.CatalogItem item:catalog.items)if(item.id.equals(selectedImageId))selectedItem=item;
        if(selectedItem==null)selectedImageId=null;
        int start=catalogPage*perPage,end=Math.min(catalog.items.size(),start+perPage);
        for(int r=0;r<2;r++){
            LinearLayout row=row();row.setPadding(0,dp(4),0,dp(4));
            for(int c=0;c<columns;c++){
                int index=start+r*columns+c;
                LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,-2,1);lp.setMargins(dp(3),0,dp(3),0);
                if(index>=end){android.view.View empty=new android.view.View(this);empty.setMinimumHeight(dp(108));lp.height=dp(108);row.addView(empty,lp);continue;}
                SleepImageCatalogService.CatalogItem item=catalog.items.get(index);
                LinearLayout tile=column();tile.setMinimumHeight(dp(108));tile.setPadding(dp(4),dp(4),dp(4),dp(4));tile.setGravity(Gravity.CENTER);tile.setBackgroundColor(item.id.equals(selectedImageId)?0xffdddddd:0xfff5f5f5);
                Bitmap bitmap=Bitmap.createBitmap(item.thumbnail,THUMBNAIL_WIDTH,THUMBNAIL_HEIGHT,Bitmap.Config.ARGB_8888);
                ImageView image=new ImageView(this);image.setImageBitmap(bitmap);image.setScaleType(ImageView.ScaleType.FIT_CENTER);image.setImportantForAccessibility(android.view.View.IMPORTANT_FOR_ACCESSIBILITY_NO);tile.addView(image,new LinearLayout.LayoutParams(-1,dp(64)));
                TextView name=note(item.builtIn?"Built-in":"Image "+(index+1));name.setSingleLine(true);name.setGravity(Gravity.CENTER);tile.addView(name);
                tile.setContentDescription((item.builtIn?"Built-in fallback":"Imported image "+(index+1))+", select image actions");tile.setFocusable(true);tile.setClickable(true);tile.setSelected(item.id.equals(selectedImageId));tile.setOnClickListener(v->{selectedImageId=item.id;renderCatalog();});row.addView(tile,lp);
            }catalogItems.addView(row);
        }
        if(selectedItem!=null){
            final SleepImageCatalogService.CatalogItem item=selectedItem;
            Button action=actionButton(item.builtIn?(item.rotationEnabled?"Exclude from rotation":"Include in rotation"):"Remove image");
            boolean removable=!item.builtIn&&catalog.items.size()>1;action.setEnabled(item.builtIn||removable);
            action.setContentDescription(item.builtIn?(item.rotationEnabled?"Exclude built-in fallback from random rotation":"Include built-in fallback in random rotation"):removable?"Remove this sleep image":"Final sleep image cannot be removed");
            action.setOnClickListener(v->{if(item.builtIn)setBuiltInRotation(catalog.generation,!item.rotationEnabled);else confirmRemoval(item.id,catalog.generation);});catalogActions.addView(action);
        }else catalogActions.addView(note("Select an image to manage it. Sleep images rotate automatically."));
    }

    private void setBuiltInRotation(long expectedGeneration, boolean enabled) {
        setInlineStatus(catalogStatus,enabled
                ? "Including built-in fallback in random rotation…"
                : "Excluding built-in fallback from random rotation…");
        if (catalogService != null) {
            catalogService.setBuiltInRotation(expectedGeneration, enabled, catalogCallback);
        } else {
            setInlineStatus(catalogStatus,"Catalog service is unavailable");
        }
    }

    private void confirmRemoval(String id, long expectedGeneration) {
        new AlertDialog.Builder(this)
                .setTitle("Remove sleep image?")
                .setMessage("The catalog changes only after durable removal succeeds.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Remove", (dialog, which) -> {
                    setInlineStatus(catalogStatus,"Removing from durable catalog…");
                    if (catalogService != null) {
                        catalogService.remove(expectedGeneration, id, catalogCallback);
                    } else {
                        setInlineStatus(catalogStatus,"Catalog service is unavailable");
                    }
                })
                .show();
    }

    @Override protected void onDestroy() {
        alive=false;lightWorker.shutdown();
        if (catalogBound) unbindService(catalogConnection);
        super.onDestroy();
    }

    private void apply(int brightnessValue,int warmthValue){
        final int request=++lightRequest;
        lightWorker.execute(()->{
            IBinder service=ServiceManager.getService(MANAGER_SERVICE);String descriptor=MANAGER_DESCRIPTOR;
            if(service==null){service=ServiceManager.getService(DIRECT_SERVICE);descriptor=DIRECT_DESCRIPTOR;}
            int result=3;
            if(service!=null){Parcel data=Parcel.obtain(),reply=Parcel.obtain();
                try{data.writeInterfaceToken(descriptor);data.writeInt(brightnessValue);data.writeInt(warmthValue);
                    if(service.transact(APPLY,data,reply,0)){reply.readException();result=reply.readInt();}else result=5;
                }catch(RemoteException|RuntimeException ignored){result=5;}finally{reply.recycle();data.recycle();}}
            final int outcome=result;
            runOnUiThread(()->{
                if(!alive)return;
                if(outcome>=0&&outcome<=2){
                    if(brightnessValue>0)lastNonzero=brightnessValue;
                    getSharedPreferences("light_requests",MODE_PRIVATE).edit().putInt("brightness",brightnessValue).putInt("warmth",warmthValue).putInt("lastNonzero",lastNonzero).apply();
                }
                if(request==lightRequest){setInlineStatus(lightStatus,outcome<=2?"":outcome==7?"Change queued":"Light was not changed — try again");status.setText("Last light request: "+brightnessValue+"% / "+warmthValue+"% · "+outcome(outcome));
                    new android.os.Handler(getMainLooper()).postDelayed(this::readCurrentLight,outcome==7?150:0);}
            });
        });
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

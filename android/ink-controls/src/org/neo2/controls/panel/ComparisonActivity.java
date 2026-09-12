package org.neo2.controls.panel;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.*;
import android.webkit.*;
import org.json.JSONObject;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Native controls exposed only to the bundled comparison document. */
public final class ComparisonActivity extends Activity {
    private WebView web;
    private ValueCallback<Uri[]> photo;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private volatile boolean closed;
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        web=new WebView(this);
        web.getSettings().setJavaScriptEnabled(true);
        web.getSettings().setDomStorageEnabled(true);
        web.getSettings().setAllowFileAccess(false);
        web.getSettings().setAllowContentAccess(true);
        web.getSettings().setBlockNetworkLoads(true);
        web.setWebViewClient(new WebViewClient(){
            @Override public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest request){return true;}
        });
        web.setWebChromeClient(new WebChromeClient(){
            @Override public boolean onShowFileChooser(WebView view,ValueCallback<Uri[]> callback,FileChooserParams params){
                if(photo!=null)photo.onReceiveValue(null);
                photo=callback;
                Intent intent=new Intent(Intent.ACTION_GET_CONTENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE);
                try {startActivityForResult(intent,1);} catch(android.content.ActivityNotFoundException e){photo.onReceiveValue(null);photo=null;}
                return true;
            }
        });
        web.addJavascriptInterface(new Object(){
            @JavascriptInterface public void saveProfiles(String text){
                if(text==null||text.length()>512||closed)return;
                worker.execute(()->{try{
                    JSONObject input=new JSONObject(text),valid=new JSONObject();
                    for(String name:new String[]{"speed","hd"})if(input.has(name)){
                        JSONObject p=input.getJSONObject(name);int v=p.getInt("vivid"),q=p.getInt("quality"),g=p.getInt("gray");
                        if(v<0||v>1||q<0||q>1||g<0||g>2)return;
                        JSONObject item=new JSONObject();item.put("vivid",v);item.put("quality",q);item.put("gray",g);valid.put(name,item);
                    }
                    android.provider.Settings.Global.putString(getContentResolver(),"neo2_display_profiles",valid.toString());
                }catch(Exception ignored){}execute(DisplayCommand.parse("state"));});
            }
            @JavascriptInterface public void close(){runOnUiThread(()->{if(!closed)finish();});}
            @JavascriptInterface public void command(String action){
                DisplayCommand command=DisplayCommand.parse(action);
                if(command==null||closed)return;
                worker.execute(()->execute(command));
            }
        },"EinkDisplay");
        setContentView(web);
        web.loadUrl("file:///android_asset/panel/vivid-color.html");
    }
    private int configure(int offset,int requested,int maximum) throws RemoteException {
        Parcel data=Parcel.obtain(),reply=Parcel.obtain();
        try {
            data.writeInterfaceToken("org.neo2.controls.IFrontLightControl");data.writeInt(requested);
            transact(offset,data,reply);
            int mode=reply.readInt();
            if(mode<0||mode>maximum||(requested>=0&&mode!=requested))throw new IllegalStateException("Setting unavailable");
            if(offset==4||offset==6){int redraw=reply.readInt();if(redraw<0||redraw>1)throw new IllegalStateException();}
            return mode;
        }finally{data.recycle();reply.recycle();}
    }
    private void transact(int offset,Parcel data,Parcel reply) throws RemoteException {
        IBinder binder=ServiceManager.checkService("neo2_frontlight_controls");
        if(binder==null||!binder.transact(IBinder.FIRST_CALL_TRANSACTION+offset,data,reply,0))throw new IllegalStateException("Service unavailable");
        reply.readException();
    }
    private void execute(DisplayCommand command){
        long start=SystemClock.elapsedRealtimeNanos();
        JSONObject result=new JSONObject();String message="Settings read";boolean success=true;
        try {
            switch(command.kind){
                case "vivid": configure(6,command.values[0],1);message="Vivid applied";break;
                case "quality": configure(2,command.values[0],1);message="Color quality applied";break;
                case "gray": configure(4,command.values[0],2);message="Gray preference applied";break;
                case "config":
                    configure(2,command.values[1],1);configure(4,command.values[2],2);configure(6,command.values[0],1);
                    message="Combination applied — let the screen settle before starting";break;
                case "refresh": case "white": case "cleanup": case "light": {
                    Parcel data=Parcel.obtain(),reply=Parcel.obtain();
                    try {
                        data.writeInterfaceToken("org.neo2.controls.IFrontLightControl");
                        if(command.kind.equals("light")){
                            data.writeInt(command.values[0]);data.writeInt(command.values[1]);transact(0,data,reply);
                            int outcome=reply.readInt();success=outcome>=0&&outcome<=2;
                            message=success?"Light applied"+(outcome==1?" (capped)":""):"Light not applied (result "+outcome+")";
                        }else{
                            transact(command.kind.equals("refresh")?5:command.kind.equals("white")?1:3,data,reply);
                            message=reply.readString();success=message!=null&&message.startsWith("refresh queued id=");
                        }
                    }finally{data.recycle();reply.recycle();}
                    break;
                }
            }
        }catch(RemoteException|RuntimeException e){success=false;message="Control failed; settings may be partly applied. Readback follows; no automatic retry.";}
        try {
            result.put("vivid",configure(6,-1,1));result.put("quality",configure(2,-1,1));result.put("gray",configure(4,-1,2));
        }catch(Exception e){success=false;message+=" Readback unavailable.";}
        try {
            String profiles=android.provider.Settings.Global.getString(getContentResolver(),"neo2_display_profiles");
            result.put("profiles",profiles==null?new JSONObject():new JSONObject(profiles));
            result.put("success",success);result.put("message",message);
            result.put("controlMs",(SystemClock.elapsedRealtimeNanos()-start)/1000000.0);
        }catch(org.json.JSONException ignored){}
        runOnUiThread(()->{if(!closed)web.evaluateJavascript("window.displayResult("+result.toString()+")",null);});
    }
    @Override protected void onPause(){
        web.evaluateJavascript("window.stopTrial && window.stopTrial('App left foreground')",null);
        super.onPause();
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(request==1&&photo!=null){photo.onReceiveValue(result==RESULT_OK&&data!=null&&data.getData()!=null?new Uri[]{data.getData()}:null);photo=null;}
    }
    @Override protected void onDestroy(){closed=true;if(photo!=null)photo.onReceiveValue(null);web.removeJavascriptInterface("EinkDisplay");web.destroy();worker.shutdown();super.onDestroy();}
}

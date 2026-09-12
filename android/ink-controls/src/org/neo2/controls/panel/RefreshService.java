package org.neo2.controls.panel;

import android.app.*;
import android.content.*;
import android.os.*;
import java.util.List;

public final class RefreshService extends Service {
    static final String START="start", STOP="stop", CONFIG="config";
    static volatile boolean running;
    private HandlerThread thread;
    private Handler worker;
    private SwitchPolicy policy;
    private PowerManager power;
    private boolean registered;
    private final TaskStackListener listener = new TaskStackListener() {
        @Override public void onTaskStackChanged() { worker.post(RefreshService.this::observe); }
        @Override public void onTaskMovedToFront(ActivityManager.RunningTaskInfo task) {
            worker.post(RefreshService.this::observe);
        }
    };
    private final BroadcastReceiver screen = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) {
            worker.post(() -> { worker.removeCallbacks(fire); policy.reset();
                log("Screen state changed; pending cleanup cancelled"); });
        }
    };
    private final Runnable fire = () -> {
        if (!running || !power.isInteractive()) { policy.reset(); return; }
        // Query the actual foreground again, rather than trusting an old callback.
        observe();
        long now=SystemClock.elapsedRealtime();
        String target=policy.take(now);
        if (target == null) return;
        Parcel data=Parcel.obtain(), reply=Parcel.obtain();
        try {
            IBinder binder=ServiceManager.checkService("neo2_frontlight_controls");
            if (binder == null) { log("Unavailable: refresh service"); return; }
            data.writeInterfaceToken("org.neo2.controls.IFrontLightControl");
            if (!binder.transact(IBinder.FIRST_CALL_TRANSACTION+5,data,reply,0)) {
                log("Unavailable: refresh transaction"); return;
            }
            reply.readException();
            String receipt=reply.readString();
            if (receipt != null && receipt.startsWith("refresh queued id=")) {
                policy.queued(now);
                log("QUEUED " + target + " | " + receipt.trim());
            } else log("REJECTED " + target + " | " + receipt);
        } catch (RemoteException | RuntimeException e) {
            log("Refresh error: " + e.getClass().getSimpleName() + "; no retry");
        } finally { data.recycle(); reply.recycle(); }
    };
    static android.content.SharedPreferences prefs(Context c) {
        return c.getSharedPreferences("poc",MODE_PRIVATE);
    }
    private void log(String value) {
        String line=SystemClock.elapsedRealtime()+"ms "+value+"\n";
        String previous=prefs(this).getString("log","");
        if (previous.length()>10000) previous=previous.substring(previous.indexOf('\n',2000)+1);
        prefs(this).edit().putString("log",previous+line).apply();
    }
    private void observe() {
        if (!running || !power.isInteractive()) { policy.reset(); worker.removeCallbacks(fire); return; }
        try {
            List<ActivityManager.RunningTaskInfo> tasks=ActivityTaskManager.getService()
                    .getTasks(1,false,false,0);
            String name=tasks.isEmpty() || tasks.get(0).topActivity==null ? null
                    : tasks.get(0).topActivity.getPackageName();
            if (policy.observe(name,SystemClock.elapsedRealtime())) log("Switch → "+name);
            worker.removeCallbacks(fire);
            long due=policy.dueAt();
            if (due>=0) worker.postDelayed(fire,Math.max(1,due-SystemClock.elapsedRealtime()));
        } catch (RemoteException | RuntimeException e) {
            policy.reset(); worker.removeCallbacks(fire);
            log("Observer error: "+e.getClass().getSimpleName());
        }
    }
    @Override public void onCreate() {
        super.onCreate();
        NotificationManager nm=getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel("poc","App-switch cleanup",NotificationManager.IMPORTANCE_LOW));
        PendingIntent stop=PendingIntent.getService(this,0,new Intent(this,RefreshService.class).setAction(STOP),PendingIntent.FLAG_IMMUTABLE);
        PendingIntent open=PendingIntent.getActivity(this,1,new Intent(this,RefreshDiagnosticsActivity.class),PendingIntent.FLAG_IMMUTABLE);
        // ic_popup_sync animates in System UI even while this service is idle.
        // Each icon frame becomes display work and can interrupt e-ink settling.
        startForeground(1,new Notification.Builder(this,"poc").setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentTitle("App-switch cleanup enabled").setContentText("Tap to configure; Stop disables it")
                .setContentIntent(open).setOngoing(true).addAction(new Notification.Action.Builder(null,"Stop",stop).build()).build());
        power=getSystemService(PowerManager.class);
        thread=new HandlerThread("AppSwitchRefresh");thread.start();worker=new Handler(thread.getLooper());
        policy=new SwitchPolicy(getPackageName(),prefs(this).getInt("delay",900),3000);
        registerReceiver(screen,new IntentFilter(Intent.ACTION_SCREEN_OFF),Context.RECEIVER_NOT_EXPORTED);
        registerReceiver(screen,new IntentFilter(Intent.ACTION_SCREEN_ON),Context.RECEIVER_NOT_EXPORTED);
    }
    @Override public int onStartCommand(Intent intent,int flags,int startId) {
        if (intent!=null && STOP.equals(intent.getAction())) {
            running=false;
            worker.post(() -> { policy.reset(); worker.removeCallbacks(fire); log("Stopped"); stopSelf(); });
            return START_NOT_STICKY;
        }
        worker.post(() -> {
            policy.configure(prefs(this).getInt("delay",900),SystemClock.elapsedRealtime());
            if (!registered) {
                try {
                    ActivityTaskManager.getService().registerTaskStackListener(listener);
                    registered=true;running=true;log("Started; delay="+prefs(this).getInt("delay",900)+"ms cooldown=3000ms");
                } catch (RemoteException | RuntimeException e) {
                    log("Cannot register observer: "+e.getClass().getSimpleName());stopSelf();return;
                }
            }
            observe();
        });
        return START_NOT_STICKY;
    }
    @Override public void onDestroy() {
        running=false;
        unregisterReceiver(screen);
        worker.post(() -> {
            worker.removeCallbacks(fire);policy.reset();
            if (registered) try { ActivityTaskManager.getService().unregisterTaskStackListener(listener); }
                catch (RemoteException ignored) {}
            thread.quitSafely();
        });
        stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}

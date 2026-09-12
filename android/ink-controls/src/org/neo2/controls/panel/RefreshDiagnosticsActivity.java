package org.neo2.controls.panel;

import android.app.Activity;
import android.content.Intent;
import android.os.*;
import android.widget.*;

public final class RefreshDiagnosticsActivity extends Activity {
    private final Handler handler=new Handler(Looper.getMainLooper());
    private TextView status,log;
    private String last="";
    private final Runnable update=new Runnable() {
        @Override public void run() {
            String state=RefreshService.running ? "Enabled — watching app switches" : "Stopped";
            if (!state.contentEquals(status.getText())) status.setText(state);
            String value=RefreshService.prefs(RefreshDiagnosticsActivity.this).getString("log","");
            if (!value.equals(last)) { last=value; log.setText(value); }
            handler.postDelayed(this,1000);
        }
    };
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        ScrollView scroll=new ScrollView(this);LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);root.setPadding(40,70,40,50);scroll.addView(root);
        TextView title=new TextView(this);title.setText("App-switch refresh");title.setTextSize(30);root.addView(title);
        Button compare=new Button(this);compare.setText("Display workbench — color & speed");compare.setOnClickListener(v -> startActivity(new Intent(this,ComparisonActivity.class)));root.addView(compare);
        status=new TextView(this);status.setTextSize(24);root.addView(status);
        TextView detail=new TextView(this);detail.setText("One full-waveform cleanup after switching apps. No sleep/wake cleanup or automatic start after reboot. Opening this controller cancels a pending cleanup.");detail.setTextSize(20);root.addView(detail);
        Button start=new Button(this);start.setText("Enable app-switch cleanup");start.setOnClickListener(v -> startForegroundService(new Intent(this,RefreshService.class).setAction(RefreshService.START)));root.addView(start);
        Button stop=new Button(this);stop.setText("Stop cleanup");stop.setOnClickListener(v -> { if (RefreshService.running) startService(new Intent(this,RefreshService.class).setAction(RefreshService.STOP)); });root.addView(stop);
        TextView label=new TextView(this);label.setText("Settling delay (minimum 3 seconds between cleanups)");label.setTextSize(20);root.addView(label);
        int[] delays={400,900,1500,2500};Spinner spinner=new Spinner(this);
        spinner.setAdapter(new ArrayAdapter<String>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"400 ms","900 ms","1500 ms","2500 ms"}));
        int selected=1;for(int i=0;i<delays.length;i++)if(delays[i]==RefreshService.prefs(this).getInt("delay",900))selected=i;
        spinner.setSelection(selected);root.addView(spinner);
        spinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener(){
            @Override public void onNothingSelected(android.widget.AdapterView<?> p) {}
            @Override public void onItemSelected(android.widget.AdapterView<?> p,android.view.View v,int position,long id) {
                int value=delays[position];if(value==RefreshService.prefs(RefreshDiagnosticsActivity.this).getInt("delay",900))return;
                RefreshService.prefs(RefreshDiagnosticsActivity.this).edit().putInt("delay",value).apply();
                if(RefreshService.running)startService(new Intent(RefreshDiagnosticsActivity.this,RefreshService.class).setAction(RefreshService.CONFIG));
            }
        });
        TextView heading=new TextView(this);heading.setText("Local trigger log — queued does not prove panel completion");heading.setTextSize(19);root.addView(heading);
        log=new TextView(this);log.setTextSize(16);root.addView(log);setContentView(scroll);
    }
    @Override public void onResume(){super.onResume();handler.post(update);}
    @Override public void onPause(){handler.removeCallbacks(update);super.onPause();}
}

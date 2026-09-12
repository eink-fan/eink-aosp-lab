'use strict';
const $=id=>document.getElementById(id), M=TrialModel, bridge=window.EinkDisplay;
let state=null,busy=false,running=false,awaitingSettle=false,trial=null,timer=null;
let lastAction='',pageRefreshPending=false,refreshFrame=null,refreshRetries=0,refreshRetryTimer=null,pageSettleTimer=null;
let callbackTimes=[],rafHandles=new Set(),pendingStart=false,requestStarted=0,lightRequest=null;
const storageKey='eink-display-workbench-v1';
let resultsPage=0,preferredProfile='speed',profileMigrationAttempted=false;
let saved={records:[],profiles:{}};
try{const parsed=JSON.parse(localStorage.getItem(storageKey));if(parsed&&Array.isArray(parsed.records)&&parsed.profiles){saved.records=parsed.records.slice(-200);for(const name of ['speed','hd'])if(M.valid(parsed.profiles[name]))saved.profiles[name]=parsed.profiles[name];}}catch(e){$('storage-status').textContent='Local storage unavailable; results last only for this page session.';}
function persist(){try{localStorage.setItem(storageKey,JSON.stringify(saved));}catch(e){$('storage-status').textContent='Could not save locally. Export the results before leaving.';}}
function profile(name){return saved.profiles[name]||{vivid:1,quality:name==='hd'?1:0,gray:1};}
function profileSelection(){if(!M.valid(state))return;const matches=['speed','hd'].filter(name=>M.key(state)===M.key(profile(name)));const selected=matches.includes(preferredProfile)?preferredProfile:matches[0];for(const name of ['speed','hd'])$('apply-'+name).setAttribute('aria-pressed',String(name===selected));}
function profiles(){ $('profile-status').textContent=['speed','hd'].map(name=>`${name==='hd'?'HD':'Speed'} ${saved.profiles[name]?'saved':'starting candidate'}: ${M.label(profile(name))}`).join(' | ');profileSelection(); }
function locks(){
 const locked=busy||running||awaitingSettle;
 for(const element of document.querySelectorAll('#native-controls button,#native-controls select,#native-controls input,[data-command],#save-speed,#save-hd,#apply-light,#light-off,#combo,#apply-combo,#next-combo,#trial-type,#trial-interval'))element.disabled=element.id!=='close-panel'&&(locked||(!state&&element.classList&&element.classList.contains('option-switch')));
 $('run-trial').disabled=locked||!M.valid(state)||!bridge;
 $('stop-trial').disabled=!(running||awaitingSettle);
 $('settled').disabled=!awaitingSettle;
 $('save-result').disabled=!trial||!trial.complete||!trial.settled||trial.saved;
}
function command(action){
 if(!bridge||busy||running||awaitingSettle)return;
 lastAction=action;busy=true;requestStarted=performance.now();locks();bridge.command(action);
}
window.displayResult=result=>{
 if(result.profiles){
  const shared={};for(const name of ['speed','hd'])if(M.valid(result.profiles[name]))shared[name]=result.profiles[name];
  if(Object.keys(shared).length){saved.profiles=shared;persist();profiles();}
  else if(!profileMigrationAttempted&&Object.keys(saved.profiles).length&&bridge&&bridge.saveProfiles){profileMigrationAttempted=true;bridge.saveProfiles(JSON.stringify(saved.profiles));}
 }
 const appliedSetting=/^(config|vivid|quality|gray):/.test(lastAction)&&result.success;
 const rejectedRefresh=lastAction==='refresh'&&!result.success&&result.message==='refresh rejected: unavailable, asleep, disabled, or pending\n';
 if(appliedSetting)refreshRetries=0;
 if(rejectedRefresh&&refreshRetries<3){
  const delay=[250,500,1000][refreshRetries++];
  refreshRetryTimer=setTimeout(()=>{refreshRetryTimer=null;pageRefreshPending=true;pumpPageRefresh();},delay);
 }else if(lastAction==='refresh'&&result.success)refreshRetries=0;
 busy=false;
 state=M.valid(result)?{vivid:result.vivid,quality:result.quality,gray:result.gray}:null;
 if(state){
  $('vivid-toggle').setAttribute('aria-checked',String(state.vivid===1));
  $('gray').value=state.gray;$('quality').value=state.quality;
  for(let i=0;i<3;i++)$('gray-'+i).setAttribute('aria-pressed',String(state.gray===i));
  $('quality-toggle').setAttribute('aria-checked',String(state.quality===1));profileSelection();$('combo').value=M.key(state);
 }else{for(const id of ['apply-speed','apply-hd','gray-0','gray-1','gray-2'])$(id).setAttribute('aria-pressed','false');}
 const roundtrip=Math.round(performance.now()-requestStarted);
 $('control-diagnostics').value=(state?M.label(state)+' · ':'')+result.message+` · control round-trip ${roundtrip} ms (not panel time)`;
 const feedback=result.success&&state?'Ready':rejectedRefresh?(refreshRetryTimer!==null?'Refresh waiting':'Refresh unavailable'):'Control failed — see Diagnostics';
 if($('display-status').textContent!==feedback)$('display-status').textContent=feedback;
 if(lightRequest){$('light-status').textContent=result.success?`Last applied request: brightness ${lightRequest[0]}%, warmth ${lightRequest[1]}%. Hardware may cap output.`:'Light request failed; previous lighting is not known.';lightRequest=null;}
 locks();
 if(appliedSetting)window.refreshPage();
 pumpPageRefresh();
 if(pendingStart){pendingStart=false;if(result.success&&state)startTrial();else $('trial-hint').textContent='Cannot run: current display settings could not be read.';}
};
// Coalesce page changes and wait for WebView to draw the new content before capture.
function pumpPageRefresh(){
 if(!pageRefreshPending||refreshFrame!==null||refreshRetryTimer!==null||pageSettleTimer!==null||window.panelImageLoading||busy||running||awaitingSettle||!bridge)return;
 refreshFrame=requestAnimationFrame(()=>{refreshFrame=requestAnimationFrame(()=>{
  refreshFrame=null;
  if(busy||running||awaitingSettle)return;
  pageRefreshPending=false;command('refresh');
 });});
}
// WebView frame callbacks precede compositor/panel capture; allow the latest page
// a quiet interval as well. Rapid navigation restarts it instead of refreshing old content.
window.refreshPage=()=>{
 pageRefreshPending=true;
 if(refreshFrame!==null){cancelAnimationFrame(refreshFrame);refreshFrame=null;}
 if(pageSettleTimer!==null)clearTimeout(pageSettleTimer);
 pageSettleTimer=setTimeout(()=>{pageSettleTimer=null;pumpPageRefresh();},500);
};
$('close-panel').onclick=()=>{if(window.stopTrial)window.stopTrial('Panel closed');if(bridge&&bridge.close)bridge.close();};
for(const button of document.querySelectorAll('[data-command]'))button.onclick=()=>command(button.dataset.command);
for(const name of ['vivid','quality'])$(''+name+'-toggle').onclick=()=>{if(state)command(name+':'+(state[name]?0:1));};
$('gray').onchange=()=>command('gray:'+$('gray').value);
$('quality').onchange=()=>command('quality:'+$('quality').value);
function config(c){command('config:'+M.key(c));}
for(const name of ['speed','hd']){
 $('apply-'+name).onclick=()=>{preferredProfile=name;config(profile(name));};
 $('save-'+name).onclick=()=>{if(!M.valid(state))return;saved.profiles[name]={...state};persist();profiles();if(bridge&&bridge.saveProfiles)bridge.saveProfiles(JSON.stringify(saved.profiles));};
}
function light(off){
 const b=off?0:Number($('brightness').value),w=Number($('warmth').value);
 if(!Number.isInteger(b)||!Number.isInteger(w)||b<0||b>100||w<0||w>100){$('light-status').textContent='Enter whole percentages from 0 to 100.';return;}
 lightRequest=[b,w];command(`light:${b}:${w}`);
}
$('apply-light').onclick=()=>light(false);$('light-off').onclick=()=>light(true);
for(const c of M.combos){const option=document.createElement('option');option.value=M.key(c);option.textContent=M.label(c);$('combo').append(option);}
function selectedCombo(){return M.combos.find(c=>M.key(c)===$('combo').value);}
$('apply-combo').onclick=()=>config(selectedCombo());
$('next-combo').onclick=()=>{const index=M.combos.findIndex(c=>M.key(c)===$('combo').value);const c=M.combos[(index+1)%M.combos.length];$('combo').value=M.key(c);config(c);};
function results(){
 $('results').replaceChildren();
 $('results-page-number').textContent=`${resultsPage+1} / 3`;
 $('results-prev').disabled=resultsPage===0;$('results-next').disabled=resultsPage===2;
 for(const c of M.combos.slice(resultsPage*4,resultsPage*4+4)){
  const records=saved.records.filter(r=>r.config&&M.valid(r.config)&&M.key(r.config)===M.key(c)),last=records.at(-1);
  const tr=document.createElement('tr');
  for(const text of [M.label(c),String(records.length),last?`${last.type}, ${last.intervalMs} ms: ${last.speed} / ${last.detail}`:'Not tried']){const td=document.createElement('td');td.textContent=text;tr.append(td);}
  $('results').append(tr);
 }
 $('combo-status').textContent=`${M.combos.filter(c=>saved.records.some(r=>M.valid(r.config)&&M.key(r.config)===M.key(c))).length} / 12 combinations have observations. Compare matching trial types and intervals.`;
}
$('results-prev').onclick=()=>{if(resultsPage>0){resultsPage--;results();}};
$('results-next').onclick=()=>{if(resultsPage<2){resultsPage++;results();}};
const paragraphs=[
 'The garden gate opened onto a narrow path. Beyond the wall, the sea held the pale blue of the morning sky.',
 'A row of terracotta pots caught the afternoon light. The sage leaves cast small, crisp shadows across the stone.',
 'By evening the wind had softened. A reader turned another page, looking for the sentence left unfinished yesterday.',
 'The harbor lights appeared one by one. On the table lay a book, a pencil, and a folded map of the coast.'
];
function paint(step){
 const stage=$('trial-stage');stage.replaceChildren();
 if(trial.type==='pages'){
  const heading=document.createElement('h2');heading.textContent=`Page ${step} / 8`;stage.append(heading);
  for(let i=0;i<4;i++){const p=document.createElement('p');p.textContent=paragraphs[(step+i-1)%4];stage.append(p);}
 }else if(trial.type==='scroll'){
  const h=document.createElement('h2');h.textContent=`Scroll ${step} / 8`;stage.append(h);
  const list=document.createElement('div');list.className='moving-list';
  for(let i=0;i<12;i++){const row=document.createElement('div');row.textContent=`${step*2+i}. ${['Coastal paths','Reading list','Garden notes','Weekend plans'][(step*2+i)%4]}`;row.style.background=i%2?'#eee':'#fff';list.append(row);}stage.append(list);
 }else{
  const cards=document.createElement('div');cards.className='color-cards';
  const palette=['#ce8e88','#91ac87','#879ecc','#e1ad80','#b3a0c3','#81b2aa'];
  for(let i=0;i<6;i++){const card=document.createElement('div');card.className='color-card';card.style.background=palette[(step+i-1)%6];card.textContent=`${step} / 8`;cards.append(card);}stage.append(cards);
 }
}
function step(number){
 if(!running)return;
 const at=performance.now();paint(number);
 trial.updates.push({step:number,atMs:at-trial.started});
 const handle=requestAnimationFrame(()=>{rafHandles.delete(handle);callbackTimes.push({rafMs:performance.now()-at});});rafHandles.add(handle);
 if(number===8){
  running=false;awaitingSettle=true;trial.complete=true;trial.finalAt=at;
  $('trial-hint').textContent='Step 8 sent. Tap “Looks settled now” when the panel looks stable.';locks();
 }else timer=setTimeout(()=>step(number+1),trial.intervalMs);
}
function startTrial(){
 if(busy||running||awaitingSettle||!M.valid(state))return;
 const interval=Number($('trial-interval').value),type=$('trial-type').value;
 if(![250,500,1000,2000].includes(interval)||!['pages','scroll','color'].includes(type))return;
 trial={config:{...state},type,intervalMs:interval,date:new Date().toISOString(),started:performance.now(),updates:[],complete:false,settled:false,saved:false};
 callbackTimes=[];running=true;$('rating-speed').value='';$('rating-detail').value='';
 $('trial-hint').textContent='Running 8 updates. Watch the numbered content; Stop ends the run.';
 $('trial-result').textContent='Timing appears after you mark the final image settled. No live clock is drawn.';
 locks();step(1);
}
$('run-trial').onclick=()=>{if(busy||running||awaitingSettle)return;pageRefreshPending=false;if(refreshFrame!==null){cancelAnimationFrame(refreshFrame);refreshFrame=null;}pendingStart=true;command('state');};
window.stopTrial=reason=>{
 if(!(running||awaitingSettle))return;
 clearTimeout(timer);for(const handle of rafHandles)cancelAnimationFrame(handle);rafHandles.clear();
 running=false;awaitingSettle=false;trial=null;
 $('trial-hint').textContent=`Stopped: ${reason||'user request'}. Incomplete run was not saved.`;locks();
};
$('stop-trial').onclick=()=>window.stopTrial('user request');
document.addEventListener('visibilitychange',()=>{if(document.hidden)window.stopTrial('page hidden');});
$('settled').onclick=()=>{
 if(!awaitingSettle||!trial)return;
 trial.observedSettleMs=Math.round(performance.now()-trial.finalAt);trial.settled=true;awaitingSettle=false;
 trial.scheduling=M.summarize(callbackTimes);
 const measured=trial.scheduling;
 $('trial-result').textContent=`Observed final settling + reaction: ${trial.observedSettleMs} ms. ${measured?`Browser callback scheduling median ${measured.rafMedianMs.toFixed(1)} ms, max ${measured.rafMaxMs.toFixed(1)} ms (${measured.updates}/8 callbacks).`: 'Browser callback timing unavailable.'} Neither is measured panel latency. Rate speed and detail, then save.`;
 $('trial-hint').textContent='Finished. Save an observation or try the next combination.';locks();
 if(window.showPage)window.showPage('review');
};
$('save-result').onclick=()=>{
 if(!trial||!trial.settled||trial.saved)return;
 const speed=$('rating-speed').value,detail=$('rating-detail').value;
 if(!speed||!detail){$('trial-result').textContent='Choose both Speed and Detail ratings before saving.';return;}
 const {started,finalAt,complete,settled,...record}=trial;
 record.speed=speed;record.detail=detail;record.saved=true;
 saved.records.push(record);saved.records=saved.records.slice(-200);trial.saved=true;persist();results();locks();
 $('trial-result').textContent='Observation saved for '+M.label(trial.config)+'.';
};
$('export-results').onclick=()=>{
 if(window.showPage)window.showPage('export');$('export-data').hidden=false;$('export-data').value=JSON.stringify({schema:1,measurement:'Browser scheduling and subjective final settling including reaction time; no physical panel latency/FPS measurement',...saved},null,2);
};
profiles();results();locks();
if(bridge){$('native-controls').hidden=false;document.querySelector('header>p').hidden=true;command('state');}
else{$('trial-hint').textContent='Open Display workbench in the refresh APK to run trials with verified native settings.';}

let settingsPage=0;
function settingsCarousel(){
 for(const panel of document.querySelectorAll('[data-setting-panel]'))panel.hidden=Number(panel.dataset.settingPanel)!==settingsPage;
 $('settings-page-label').textContent=['Refresh','Presets','Lighting','Diagnostics'][settingsPage]+` · ${settingsPage+1} / 4`;
 window.refreshPage();
}
$('settings-prev').onclick=()=>{settingsPage=(settingsPage+3)%4;settingsCarousel();};
$('settings-next').onclick=()=>{settingsPage=(settingsPage+1)%4;settingsCarousel();};
function qualitySample(pattern){
 const palette=['#ce8e88','#91ac87','#879ecc','#e1ad80','#b3a0c3','#aaaaaa'];
 $('quality-stage').replaceChildren();
 palette.forEach((color,i)=>{const row=document.createElement('div');row.style.background=pattern?`repeating-linear-gradient(90deg,${palette[(i+2)%palette.length]} 0%,${palette[(i+2)%palette.length]} 12.5%,#fff 12.5%,#fff 25%)`:color;$('quality-stage').append(row);});
 $('quality-test-status').textContent=pattern?'Pattern — allow it to settle, then tap Flat target.':'Flat target — look for residual vertical stripes.';
}
$('quality-pattern').onclick=()=>qualitySample(true);$('quality-flat').onclick=()=>qualitySample(false);

$('trial-type').onchange=()=>window.refreshPage();

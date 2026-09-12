const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=process.env.INK_PANEL_ASSET_DIR||path.join(__dirname,'..'),model=require(path.join(root,'trial-model.js'));
assert.equal(new Set(model.combos.map(model.key)).size,12);
assert.throws(()=>model.key({vivid:2,quality:0,gray:0}));
assert.equal(model.summarize([]),null);
assert.deepEqual(model.summarize([{rafMs:40},{rafMs:5},{rafMs:10}]),{updates:3,rafMedianMs:10,rafMaxMs:40});
function harness(){
 let now=0,serial=0;const timers=new Map(),elements=new Map(),listeners={},commands=[],store={};
 function element(id){if(!elements.has(id))elements.set(id,{id,value:'',textContent:'',dataset:{},style:{},children:[],disabled:false,append(...a){this.children.push(...a);},replaceChildren(){this.children=[];},setAttribute(k,v){this[k]=v;}});return elements.get(id);}
 const document={hidden:false,getElementById:element,createElement:tag=>({tag,style:{},children:[],append(...a){this.children.push(...a);}}),querySelector:()=>element('intro'),querySelectorAll:()=>[],addEventListener:(e,cb)=>listeners[e]=cb};
 element('trial-type').value='pages';element('trial-interval').value='500';
 const set=(fn,delay)=>{const id=++serial;timers.set(id,{fn,at:now+delay});return id;};
 const context={document,TrialModel:model,console,localStorage:{getItem:k=>store[k]||null,setItem:(k,v)=>store[k]=v},performance:{now:()=>now},setTimeout:set,clearTimeout:id=>timers.delete(id),requestAnimationFrame:fn=>set(fn,16),cancelAnimationFrame:id=>timers.delete(id)};
 context.window=context;context.EinkDisplay={command:a=>commands.push(a)};vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(root,'comparison.js'),'utf8'),context);
 function tick(ms){const end=now+ms;while(true){let next;for(const [id,t] of timers)if(t.at<=end&&(!next||t.at<next[1].at))next=[id,t];if(!next)break;now=next[1].at;timers.delete(next[0]);next[1].fn();}now=end;}
 const reply=(overrides={})=>context.displayResult({vivid:1,quality:0,gray:1,success:true,message:'Settings read',...overrides});
 return {element,context,tick,commands,reply,timers,listeners,store};
}
{
 const h=harness();assert.equal(h.commands[0],'state');h.reply();
 h.element('run-trial').onclick();assert.equal(h.commands.at(-1),'state');h.reply();h.tick(3516);
 assert.equal(vm.runInContext('trial.updates.length',h.context),8);assert.equal(h.element('settled').disabled,false);
 h.tick(500);h.element('settled').onclick();
 assert.equal(vm.runInContext('trial.observedSettleMs',h.context),516);
 h.element('rating-speed').value='Comfortable';h.element('rating-detail').value='Clean';h.element('save-result').onclick();h.element('save-result').onclick();
 assert.equal(JSON.parse(Object.values(h.store)[0]).records.length,1);
 h.element('save-speed').onclick();assert.equal(JSON.parse(Object.values(h.store)[0]).profiles.speed.quality,0);
 h.element('apply-hd').onclick();assert.equal(h.commands.at(-1),'config:1:1:1');
 assert.equal(h.timers.size,0);
}
{
 const h=harness();h.reply();h.element('run-trial').onclick();h.reply();h.tick(600);
 h.context.stopTrial('test stop');const n=h.commands.length;h.tick(20000);
 assert.equal(vm.runInContext('trial',h.context),null);assert.equal(h.timers.size,0);assert.equal(h.commands.length,n);assert.equal(h.element('save-result').disabled,true);
 h.element('run-trial').onclick();h.reply({success:false});assert.equal(vm.runInContext('running',h.context),false);
}
{
 const h=harness();h.reply();h.element('run-trial').onclick();h.reply();h.context.document.hidden=true;h.listeners.visibilitychange();h.tick(20000);assert.equal(h.timers.size,0);
}
console.log('comparison.test.js PASS: 12 combinations, readback gate, bounded run, subjective timing, save once, presets, stop/background cancellation');
{
 const h=harness();h.reply();
 h.context.refreshPage();h.context.refreshPage();h.tick(531);assert.equal(h.commands.filter(x=>x==='refresh').length,0);
 h.tick(1);assert.equal(h.commands.filter(x=>x==='refresh').length,1);h.reply();h.tick(100);assert.equal(h.commands.filter(x=>x==='refresh').length,1);
 h.element('apply-hd').onclick();h.reply({quality:1});h.tick(532);assert.equal(h.commands.filter(x=>x==='refresh').length,2);h.reply({quality:1});
 h.element('run-trial').onclick();h.reply({quality:1});h.tick(5000);assert.equal(h.commands.filter(x=>x==='refresh').length,2);
 assert.equal(vm.runInContext('trial.updates.length',h.context),8);
 h.context.stopTrial('test complete');
}
console.log('Auto-refresh PASS: page changes coalesce after draw, profile application refreshes, eight-step trial adds no refreshes');

{
 const h=harness();h.reply({vivid:1,quality:0});
 h.element('vivid-toggle').onclick();assert.equal(h.commands.at(-1),'vivid:0');
 h.reply({vivid:0,quality:0,success:false});
 h.element('quality-toggle').onclick();assert.equal(h.commands.at(-1),'quality:1');
}
console.log('Switches PASS: invert native readback rather than optimistic UI state');

{
 const h=harness();h.reply();h.element('apply-hd').onclick();h.reply({quality:1});h.tick(532);
 const rejection={quality:1,success:false,message:'refresh rejected: unavailable, asleep, disabled, or pending\n'};
 h.reply(rejection);assert.equal(h.element('display-status').textContent,'Refresh waiting');
 for(const delay of [250,500,1000]){h.tick(delay+32);h.reply(rejection);}
 const count=h.commands.length;h.tick(5000);assert.equal(h.commands.length,count);
 assert.equal(h.commands.filter(x=>x.startsWith('config:')).length,1);
 assert.equal(h.element('display-status').textContent,'Refresh unavailable');
}
console.log('Profile refresh PASS: bounded rejected-refresh retries never reapply profile');

{
 const h=harness();h.reply();h.context.refreshPage();h.tick(400);
 h.context.refreshPage();h.tick(499);assert.equal(h.commands.filter(x=>x==='refresh').length,0);
 h.context.panelImageLoading=true;h.tick(1000);assert.equal(h.commands.filter(x=>x==='refresh').length,0);
 h.context.panelImageLoading=false;h.context.refreshPage();h.tick(531);
 assert.equal(h.commands.filter(x=>x==='refresh').length,0);h.tick(1);
 assert.equal(h.commands.filter(x=>x==='refresh').length,1);
}
console.log('Page settling PASS: navigation debounces and loading images defer capture');

{
 const h=harness();h.reply({profiles:{speed:{vivid:0,quality:0,gray:2},hd:{vivid:1,quality:1,gray:0}}});
 h.element('apply-hd').onclick();assert.equal(h.commands.at(-1),'config:1:1:0');h.reply();
 const writes=[];h.context.EinkDisplay.saveProfiles=text=>writes.push(JSON.parse(text));
 h.element('save-speed').onclick();assert.deepEqual(writes[0].speed,{vivid:1,quality:0,gray:1});
}
console.log('Shared presets PASS: native definitions drive selection and saves publish both profiles');

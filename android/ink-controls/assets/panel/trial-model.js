/* Trial bookkeeping: these are software timestamps, never panel-completion telemetry. */
(function(root){
 const combos=[];
 for(let vivid=0;vivid<2;vivid++)for(let quality=0;quality<2;quality++)for(let gray=0;gray<3;gray++)combos.push({vivid,quality,gray});
 function valid(c){return c&&[0,1].includes(c.vivid)&&[0,1].includes(c.quality)&&[0,1,2].includes(c.gray);}
 function key(c){if(!valid(c))throw Error('Unknown settings');return `${c.vivid}:${c.quality}:${c.gray}`;}
 function label(c){return `Vivid ${c.vivid?'on':'off'} · Quality ${c.quality?'on':'off'} · ${['Original','Balanced','Stronger'][c.gray]}`;}
 function summarize(samples){
  if(!samples.length)return null;
  const sorted=samples.map(s=>s.rafMs).sort((a,b)=>a-b);
  return {updates:samples.length,rafMedianMs:sorted[Math.floor(sorted.length/2)],rafMaxMs:sorted.at(-1)};
 }
 const api={combos,valid,key,label,summarize};
 if(typeof module!=='undefined')module.exports=api;else root.TrialModel=api;
})(typeof window==='undefined'?globalThis:window);

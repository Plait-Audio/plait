/**
 * GET /api/dashboard?token=SECRET&days=30
 *
 * Token-gated analytics dashboard. Unlike a static page, this returns 401 (and
 * no UI) unless the ANALYTICS_STATS_TOKEN matches — so the dashboard itself is
 * not publicly viewable. The page reads its token from the URL and calls
 * /api/stats for the data (which is independently token-gated).
 */

function page() {
  // Note: ${'$'}{...} escaping — the client JS uses template literals, so we
  // build this HTML with plain string concatenation, not a tagged template.
  return `<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex,nofollow"><title>ISO Drums · Analytics</title>
<style>
:root{--bg:#0d0d0e;--panel:#151517;--panel2:#1c1c1f;--border:#2a2a2e;--text:#ececee;--muted:#8a8a90;--accent:#C9A96E;--accent2:#6ea0c9;--good:#5aa96e}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
header{display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:space-between;padding:20px 24px;border-bottom:1px solid var(--border)}
h1{font-size:16px;margin:0;font-weight:600}h1 span{color:var(--accent)}
select,button{background:var(--panel2);color:var(--text);border:1px solid var(--border);border-radius:8px;padding:8px 10px;font-size:13px;cursor:pointer}
main{padding:24px;max-width:1100px;margin:0 auto}.msg{color:var(--muted);padding:40px 0;text-align:center}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:14px;margin-bottom:24px}
.card{background:var(--panel);border:1px solid var(--border);border-radius:12px;padding:16px 18px}
.card .k{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.06em}
.card .v{font-size:30px;font-weight:600;margin-top:6px;font-variant-numeric:tabular-nums}.card .s{color:var(--muted);font-size:12px;margin-top:2px}
.grid{display:grid;grid-template-columns:2fr 1fr;gap:16px}@media(max-width:760px){.grid{grid-template-columns:1fr}}
.panel{background:var(--panel);border:1px solid var(--border);border-radius:12px;padding:18px}
.panel h2{font-size:13px;font-weight:600;margin:0 0 14px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;display:flex;justify-content:space-between;align-items:center}
.legend{display:flex;gap:16px;font-size:12px;color:var(--muted);margin-top:10px}.dot{display:inline-block;width:9px;height:9px;border-radius:2px;margin-right:5px}
svg{display:block;width:100%;height:auto}.foot{color:var(--muted);font-size:12px;margin-top:24px;text-align:center}
.bar-row{display:flex;align-items:center;gap:10px;margin:9px 0;font-size:13px}.bar-row .lbl{width:64px;color:var(--muted);flex:none}
.bar-track{flex:1;background:var(--panel2);border-radius:6px;height:20px;overflow:hidden}.bar-fill{height:100%;border-radius:6px}
.bar-row .num{width:56px;text-align:right;font-variant-numeric:tabular-nums;flex:none}
.subs{margin-top:16px}.subs .list{max-height:220px;overflow:auto;border:1px solid var(--border);border-radius:8px;background:var(--panel2)}
.srow{display:flex;justify-content:space-between;gap:12px;padding:7px 12px;font:12px/1.5 ui-monospace,monospace;border-bottom:1px solid var(--border)}
.srow:last-child{border-bottom:none}.srow .sdate{color:var(--muted);flex:none}
.copy{font-size:12px;padding:4px 10px}
</style></head><body>
<header><h1>ISO Drums <span>· Analytics</span></h1>
<div><select id="days"><option value="7">7 days</option><option value="30" selected>30 days</option><option value="90">90 days</option></select>
<button id="go">Refresh</button></div></header>
<main>
<div id="status" class="msg">Loading…</div>
<div id="dash" style="display:none">
  <div class="cards" id="cards"></div>
  <div class="grid">
    <div class="panel"><h2>Activity over time</h2><div id="chart"></div>
      <div class="legend"><span><span class="dot" style="background:var(--accent)"></span>Separations</span><span><span class="dot" style="background:var(--accent2)"></span>Launches</span></div></div>
    <div class="panel"><h2>Exports &amp; input length</h2><div id="bars"></div></div>
  </div>
  <div class="panel subs" style="margin-top:16px"><h2>Email signups <button class="copy" id="copy">Copy all</button></h2>
    <div id="subcount" style="color:var(--muted);font-size:12px;margin-bottom:8px"></div>
    <div id="subs" class="list"></div></div>
  <div class="foot">Anonymous, opt-in usage data · emails are only those users chose to give · aggregates only</div>
</div></main>
<script>
const $=s=>document.querySelector(s);
const params=new URLSearchParams(location.search);const TOKEN=params.get('token')||'';let LAST=[];
const fmt=n=>(n>=1000?(n/1000).toFixed(n>=10000?0:1)+'k':String(n));
function card(k,v,s){return '<div class="card"><div class="k">'+k+'</div><div class="v">'+v+'</div>'+(s?'<div class="s">'+s+'</div>':'')+'</div>';}
function lineChart(dates,series){const W=640,H=200,P=24,n=dates.length;const max=Math.max(1,...series.flatMap(s=>s.data));
 const x=i=>P+(W-2*P)*(n<=1?0.5:i/(n-1)),y=v=>H-P-(H-2*P)*(v/max);let g='<svg viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none">';
 for(let t=0;t<=2;t++){const gy=P+(H-2*P)*t/2;g+='<line x1="'+P+'" y1="'+gy+'" x2="'+(W-P)+'" y2="'+gy+'" stroke="#2a2a2e"/>';}
 g+='<text x="'+P+'" y="14" fill="#8a8a90" font-size="11">'+max+'</text>';
 for(const s of series){const pts=s.data.map((v,i)=>x(i)+','+y(v)).join(' ');g+='<polyline points="'+pts+'" fill="none" stroke="'+s.color+'" stroke-width="2"/>';s.data.forEach((v,i)=>{g+='<circle cx="'+x(i)+'" cy="'+y(v)+'" r="2.5" fill="'+s.color+'"/>';});}
 const every=Math.ceil(n/6);dates.forEach((d,i)=>{if(i%every===0||i===n-1)g+='<text x="'+x(i)+'" y="'+(H-6)+'" fill="#8a8a90" font-size="10" text-anchor="middle">'+d.slice(5)+'</text>';});return g+'</svg>';}
function barRow(l,v,mx,c){const w=mx>0?Math.max(2,100*v/mx):0;return '<div class="bar-row"><span class="lbl">'+l+'</span><span class="bar-track"><span class="bar-fill" style="width:'+w+'%;background:'+c+'"></span></span><span class="num">'+fmt(v)+'</span></div>';}
function render(data){$('#status').style.display='none';$('#dash').style.display='block';const t=data.totals;
 const rate=(t.sep_ok+t.sep_fail)>0?Math.round(100*t.sep_ok/(t.sep_ok+t.sep_fail)):100;
 const peak=Math.max(0,...Object.values(data.daily).map(r=>r.installs));
 $('#cards').innerHTML=card('Separations',fmt(t.separate),rate+'% success')+card('Launches',fmt(t.launch))+card('WAV exports',fmt(t.export_wav))+card('MIDI exports',fmt(t.export_midi))+card('Peak daily installs',fmt(peak),'over '+data.days+' days')+card('Email signups',fmt(data.subscribersTotal||0),'+'+fmt(t.subscribe||0)+' this period');
 $('#chart').innerHTML=lineChart(data.dates,[{data:data.dates.map(d=>data.daily[d].separate),color:'#C9A96E'},{data:data.dates.map(d=>data.daily[d].launch),color:'#6ea0c9'}]);
 const exMax=Math.max(1,t.export_wav,t.export_midi);const db=data.durBuckets||{};const dbMax=Math.max(1,...Object.values(db));
 $('#bars').innerHTML='<div style="color:var(--muted);font-size:12px;margin-bottom:6px">Export type</div>'+barRow('WAV',t.export_wav,exMax,'var(--accent)')+barRow('MIDI',t.export_midi,exMax,'var(--accent2)')+'<div style="color:var(--muted);font-size:12px;margin:14px 0 6px">Input length</div>'+Object.entries(db).map(([b,v])=>barRow(b,v,dbMax,'var(--good)')).join('');
 const subs=data.subscribers||[];LAST=subs;$('#subcount').textContent=subs.length+' subscriber'+(subs.length===1?'':'s');
 $('#subs').innerHTML=subs.map(s=>{const d=s.ts?new Date(s.ts).toLocaleDateString(undefined,{year:'numeric',month:'short',day:'numeric'}):'—';return '<div class="srow"><span>'+s.email+'</span><span class="sdate">'+d+'</span></div>';}).join('')||'<div class="srow" style="color:var(--muted)">No signups yet</div>';}
async function load(){$('#status').style.display='block';$('#dash').style.display='none';$('#status').textContent='Loading…';
 try{const r=await fetch('/api/stats?token='+encodeURIComponent(TOKEN)+'&days='+$('#days').value);
 if(r.status===401){$('#status').textContent='Unauthorized.';return;}const data=await r.json();
 if(!data.ok){$('#status').textContent='Error: '+(data.message||'unknown');return;}render(data);}
 catch(e){$('#status').textContent='Request failed: '+e.message;}}
$('#go').onclick=load;$('#days').onchange=load;
$('#copy').onclick=async()=>{try{await navigator.clipboard.writeText(LAST.map(s=>s.email).join('\\n'));}catch(e){}$('#copy').textContent='Copied!';setTimeout(()=>$('#copy').textContent='Copy all',1200);};
load();
</script></body></html>`;
}

export default async function handler(req, res) {
  const token = req.query?.token;
  const expected = process.env.ANALYTICS_STATS_TOKEN;
  res.setHeader('X-Robots-Tag', 'noindex, nofollow');
  res.setHeader('Content-Type', 'text/html; charset=utf-8');
  if (!expected || token !== expected)
    return res.status(401).send('<!doctype html><meta charset=utf-8><body style="background:#0d0d0e;color:#8a8a90;font-family:system-ui;padding:60px;text-align:center">401 · Unauthorized</body>');
  return res.status(200).send(page());
}

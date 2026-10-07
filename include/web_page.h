#pragma once

// Single-page local UI. Polls /api/status every second; settings post form fields.
static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Power Strip</title>
<style>
:root{--bg:#eef0ec;--card:#fbfcfa;--ink:#18201c;--mut:#5a665f;--line:#d3d8d1;--acc:#0e6b62;--on:#1f8a4c;--off:#8a948e;--warn:#c07a00}
@media(prefers-color-scheme:dark){:root{--bg:#0f1412;--card:#161c19;--ink:#e3e9e5;--mut:#97a39c;--line:#2b3430;--acc:#53c2b4;--on:#4fcf83;--off:#5d6862;--warn:#ffb040}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,sans-serif}
main{max-width:900px;margin:0 auto;padding:16px}h1{font-size:1.5rem;margin:0}h2{font-size:1.1rem;margin:24px 0 8px}
.bar{display:flex;flex-wrap:wrap;gap:8px 18px;color:var(--mut);font-size:.9rem;margin:6px 0 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px}
.big{font:600 1.6rem ui-monospace,monospace}.mut{color:var(--mut);font-size:.85rem}
.row{display:flex;justify-content:space-between;align-items:center;gap:8px}
.kv{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-top:10px;font-family:ui-monospace,monospace}
.kv b{display:block;font-size:1.05rem}.kv span{color:var(--mut);font-size:.75rem}
button{font:inherit;border:0;border-radius:6px;padding:8px 14px;cursor:pointer;background:var(--acc);color:#fff}
button.sw{min-width:88px;font-weight:700}.sw.on{background:var(--on)}.sw.off{background:var(--off)}
.pend{color:var(--warn);font-size:.85rem}
.alert{background:#c0341d;color:#fff;border-radius:8px;padding:12px 14px;margin-bottom:12px;font-weight:600}
.prow{display:grid;grid-template-columns:1fr auto auto auto;gap:8px;align-items:center;padding:6px 0;border-bottom:1px solid var(--line)}
.prow .mono{font:12px ui-monospace,monospace;color:var(--mut)}
button.ghost{background:none;color:var(--acc);border:1px solid var(--line)}
details{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px 14px;margin-top:10px}
summary{cursor:pointer;font-weight:600}form{display:grid;gap:8px;margin-top:10px}
label{display:grid;gap:2px;font-size:.85rem;color:var(--mut)}label.ck{display:flex;gap:8px;align-items:center}
input,select{font:inherit;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--ink)}
table{width:100%;border-collapse:collapse;font-size:.9rem}td{padding:4px 6px;border-bottom:1px solid var(--line)}
.lt{display:flex;align-items:center;gap:6px}
.info{display:inline-grid;place-items:center;width:18px;height:18px;border-radius:50%;border:1.5px solid var(--acc);color:var(--acc);font:italic 700 12px/1 Georgia,serif;cursor:pointer;flex:none}
.info:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
textarea{font:12px ui-monospace,monospace;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--ink);min-height:110px;resize:vertical}
.help{margin:-2px 0 4px;padding:8px 10px;background:var(--bg);border-left:3px solid var(--acc);border-radius:4px;font-size:.85rem;color:var(--ink)}
#msg{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:var(--ink);color:var(--bg);padding:8px 14px;border-radius:6px}
</style></head><body><main>
<div class="row"><h1>Power Strip</h1><span id="cloud" class="mut"></span></div>
<div class="bar" id="bar"></div><div class="pend" id="link"></div>
<div class="alert" id="therm" hidden></div>
<div class="grid">
<div class="card"><div class="mut">Mains</div><div class="big" id="v">-</div><div class="mut" id="f"></div></div>
<div class="card"><div class="mut">Total</div><div class="big" id="tp">-</div><div class="mut" id="ti"></div></div>
<div class="card"><div class="mut">Hottest point</div><div class="big" id="tc">-</div><div class="mut" id="tcs"></div></div>
</div>
<h2>Outlets</h2><div class="grid" id="outs"></div>
<h2>Recent events</h2><div class="card"><table id="ev"></table></div>
<h2>Settings</h2>
<details><summary>Outlets</summary><div id="ocfg"></div></details>
<details><summary>WiFi</summary><form data-api="/api/wifi">
<label data-help="Your home WiFi name. The strip joins it to reach ThingsBoard, and you can then open this page at powerstrip.local."><span class="lt">Network name (SSID)</span><input name="ssid" id="ssid"></label>
<label data-help="Your home WiFi password. Leave blank to keep the saved one. Saving restarts the strip."><span class="lt">Password <span id="pwset"></span></span><input name="password" type="password" placeholder="leave blank to keep"></label>
<button>Save and restart</button></form></details>
<details><summary>ThingsBoard</summary><form data-api="/api/tb">
<label data-help="Your ThingsBoard server address for MQTT, without http://. If your main domain is behind Cloudflare, use the DNS-only MQTT name."><span class="lt">MQTT host</span><input name="host" id="tbhost"></label>
<label data-help="1883 for plain MQTT, 8883 with TLS."><span class="lt">MQTT port</span><input name="port" id="tbport" type="number"></label>
<label class="ck" data-help="Encrypts the connection to ThingsBoard and checks the server is really yours. Needs the TLS port (usually 8883). Firmware downloads still use plain HTTP, but are checksum-verified."><input type="checkbox" name="use_tls" id="tbtls"><span class="lt">Use secure connection (TLS)</span></label>
<label data-help="The root certificate that signs your MQTT server certificate. Leave blank to use the built-in Let&#39;s Encrypt root (ISRG Root X1), which fits most servers."><span class="lt">MQTT CA certificate (PEM, optional)</span><textarea name="ca_cert" id="tbca" placeholder="blank = built-in ISRG Root X1"></textarea></label>
<label data-help="The access token of this device in ThingsBoard (open the device, then Copy access token). Leave blank to keep the saved one."><span class="lt">Device access token <span id="tkset"></span></span><input name="token" type="password" placeholder="leave blank to keep"></label>
<label data-help="Where firmware updates are downloaded from: your ThingsBoard web address, without http://. Leave blank to use the MQTT host."><span class="lt">OTA HTTP host (blank = MQTT host)</span><input name="http_host" id="tbhh"></label>
<label data-help="Port for firmware downloads: 80 if ThingsBoard is behind Cloudflare, 8080 for a direct install. Downloads use plain HTTP, but the firmware checksum is still checked before installing."><span class="lt">OTA HTTP port</span><input name="http_port" id="tbhp" type="number"></label>
<button>Save and restart</button></form></details>
<details><summary>Overheat protection</summary><form data-api="/api/thermal">
<label data-help="If any probe reaches this temperature, every outlet switches off and stays locked off. 70 &deg;C is well above normal but below where terminals and wire insulation get damaged."><span class="lt">Switch everything off at (&deg;C)</span><input name="trip_c" id="trip" type="number" min="40" max="110" step="1"></label>
<label data-help="After a trip, the outlets can be turned on again only once every probe has cooled below this. They never turn back on by themselves."><span class="lt">Allow turning on again below (&deg;C)</span><input name="reset_c" id="reset" type="number" min="20" max="105" step="1"></label>
<button>Save limits</button></form>
<h3 style="margin:16px 0 4px;font-size:1rem">Probes</h3>
<p class="mut">New probes on GPIO 32 appear here within 30 seconds. Warm one in your hand to see which is which, then name it. If a named probe stops answering, everything switches off; use Forget after removing or replacing one.</p>
<div id="probes"><p class="mut">No probes connected. Protection is off.</p></div></details>
<details><summary>Calibration</summary><form data-api="/api/calib">
<label data-help="Turns the voltage sensor signal into volts. Measure the socket with a multimeter, then: new gain = old gain &times; (meter &divide; shown)."><span class="lt">Voltage gain (V per mV)</span><input name="v_gain" id="vg" step="any" type="number"></label>
<label data-help="Turns the Out 1 current sensor signal into amps. The default suits a 30 A ACS712. Correct it with a clamp meter the same way as the voltage."><span class="lt">Out 1 current gain (A per mV)</span><input name="i1_gain" id="i1g" step="any" type="number"></label>
<label data-help="Same as Out 1, for the Out 2 current sensor."><span class="lt">Out 2 current gain (A per mV)</span><input name="i2_gain" id="i2g" step="any" type="number"></label>
<label data-help="Cancels the time shift of the voltage sensor, which otherwise makes real power and kWh read low (a kettle shows PF below 1.00). Use Auto-calibrate below rather than typing it."><span class="lt">Phase correction (degrees)</span><input name="phase_deg" id="phd" step="any" type="number" min="-60" max="60"></label>
<p class="mut">New gain = old gain &times; (meter reading &divide; shown reading).</p>
<button>Save</button></form>
<p class="mut" style="margin-top:12px">Auto-calibrate phase: switch on a kettle, heater or iron (not a motor, charger or LED lamp) drawing at least 2 A on either outlet, wait a few seconds, then click.</p>
<button id="phcal">Auto-calibrate phase</button></details>
<details><summary>Firmware updates</summary><p id="fwst"></p><button id="fwinst" hidden>Install now</button>
<form data-api="/api/update-config">
<label class="ck" data-help="When on, a new version from ThingsBoard installs by itself during the hours below. Installing restarts the strip: both outlets switch off for about a second, and outlets with compressor protection then stay off for their minimum off time. Turn this off if something plugged in must never lose power unexpectedly, such as medical equipment. The strip then never restarts by itself or on a remote command, and an update waits until you click Install now here."><input type="checkbox" name="auto_update" id="auto"><span class="lt">Install updates automatically</span></label>
<label data-help="Local time, 0-23. Automatic updates only install from the start hour until the end hour, e.g. 3 and 5 means between 03:00 and 04:59. The same start and end means any time."><span class="lt">Install from (hour)</span><input name="window_start_h" id="wst" type="number" min="0" max="23"></label>
<label data-help="The window closes at this hour. A window can cross midnight, e.g. 23 to 2."><span class="lt">Until (hour)</span><input name="window_end_h" id="wen" type="number" min="0" max="23"></label>
<button>Save</button></form></details>
<details><summary>Device</summary><p class="mut" id="dev"></p><button id="reboot">Restart</button></details>
</main><div id="msg" hidden></div>
<script>
const $=id=>document.getElementById(id);
const fx=(x,d)=>Number(x).toFixed(d);
function toast(t){const m=$('msg');m.textContent=t;m.hidden=false;setTimeout(()=>m.hidden=true,2500)}
async function post(url,data){const r=await fetch(url,{method:'POST',body:new URLSearchParams(data)});return r.json()}
function renderOutlets(o){
  const box=$('outs');
  if(box.children.length!==o.length){box.innerHTML=o.map((_,i)=>`<div class="card"><div class="row"><div><div id="on${i}" style="font-weight:700"></div><div class="pend" id="pd${i}"></div></div><button class="sw" id="b${i}"></button></div><div class="kv"><div><b id="a${i}"></b><span>A</span></div><div><b id="w${i}"></b><span>W</span></div><div><b id="pf${i}"></b><span>PF</span></div><div><b id="k${i}"></b><span>kWh</span></div></div></div>`).join('');
    o.forEach((_,i)=>$('b'+i).onclick=()=>toggle(i));}
  last=o;
  o.forEach((x,i)=>{$('on'+i).textContent=x.name;const b=$('b'+i);b.textContent=x.on?'ON':(x.pending_s?'WAIT':'OFF');b.className='sw '+(x.on?'on':'off');
    $('pd'+i).textContent=x.pending_s?`turning on in ${Math.floor(x.pending_s/60)}:${String(x.pending_s%60).padStart(2,'0')}`:'';
    $('a'+i).textContent=fx(x.current,2);$('w'+i).textContent=fx(x.power,0);$('pf'+i).textContent=fx(x.pf,2);$('k'+i).textContent=fx(x.kwh,3);});
}
let sock=null,last=[],evs=[];
// Optimistic flip, then the strip's own push (within ~50 ms) confirms or corrects it.
function toggle(i){const x=last[i];if(!x)return;x.on=!(x.on||x.pending_s);renderOutlets(last);
  if(sock&&sock.readyState===1)sock.send(JSON.stringify({cmd:'outlet',n:i+1,state:'toggle'}));
  else post('/api/outlet',{n:i+1,state:'toggle'});}
// A strip that restarts (e.g. a firmware update) never closes the socket, so the browser
// would keep it "open" with frozen values. Status arrives every second: after 5 s of
// silence, drop the socket and dial again.
let lastMsg=Date.now(),pageFw=null;
setInterval(()=>{if(sock&&Date.now()-lastMsg>5000){const s=sock;sock=null;s.onclose=null;s.close();
  $('link').textContent='Reconnecting to strip…';lastMsg=Date.now();connect()}},1000);
function connect(){sock=new WebSocket(`ws://${location.host}/ws`);
  sock.onopen=()=>{$('link').textContent='';events()};
  sock.onmessage=e=>{lastMsg=Date.now();let m;try{m=JSON.parse(e.data)}catch(_){return}
    if(m.type==='status')apply(m);else if(m.type==='event'){evs.unshift(m);renderEvents()}};
  sock.onclose=()=>{sock=null;$('link').textContent='Reconnecting to strip…';setTimeout(()=>{lastMsg=Date.now();connect()},2000)};}
function apply(s){
  // New firmware may bring a new page: load it rather than run the old one.
  if(pageFw===null)pageFw=s.fw;else if(s.fw!==pageFw){location.reload();return}
  $('v').textContent=fx(s.voltage,1)+' V';$('f').textContent=fx(s.frequency,2)+' Hz';
  $('tp').textContent=fx(s.total_power,0)+' W';$('ti').textContent=fx(s.total_current,2)+' A';
  $('cloud').textContent=s.cloud?'ThingsBoard connected':'ThingsBoard offline';
  $('bar').textContent='';[s.wifi?`WiFi ${s.wifi} (${s.rssi} dBm)`:'WiFi not connected',s.ip,s.time,`fw ${s.fw}`+(s.fw_waiting?` (${s.fw_waiting} ready)`:'')].forEach(t=>{const e=document.createElement('span');e.textContent=t;$('bar').appendChild(e)});
  renderOutlets(s.outlets);renderThermal(s.thermal);
  $('fwst').textContent=s.fw_waiting?`Version ${s.fw_waiting} is ready to install (running ${s.fw}).`:`Running ${s.fw}. No update waiting.`;
  $('fwinst').hidden=!s.fw_waiting;}
const THERM_MSG={overheat:'Overheat! All outlets switched off. They can be turned on again once every probe is below ',
  probe_fault:'A temperature probe stopped answering, so all outlets were switched off. Check its wiring, or Forget it in Settings if it was removed.'};
let probeSig='',limitsSet=false;
function renderThermal(t){if(!t)return;
  const ok=t.probes.filter(p=>p.ok),max=ok.length?Math.max(...ok.map(p=>p.c)):null;
  $('tc').textContent=max===null?'-':fx(max,1)+' °C';
  $('tcs').textContent=t.probes.length?t.probes.map(p=>`${p.name}: ${p.ok?fx(p.c,1)+'°':'no reading'}`).join(' · '):'No probes, protection off';
  const a=$('therm'),msg=THERM_MSG[t.state];a.hidden=!msg;if(msg)a.textContent=msg+(t.state==='overheat'?t.reset_c+' °C.':'');
  if(!limitsSet){$('trip').value=t.trip_c;$('reset').value=t.reset_c;limitsSet=true}
  const sig=t.probes.map(p=>p.id+p.name).join();if(sig===probeSig)return;probeSig=sig;
  $('probes').innerHTML=t.probes.length?'':'<p class="mut">No probes connected. Protection is off.</p>';
  t.probes.forEach(p=>{const r=document.createElement('div');r.className='prow';
    r.innerHTML='<input maxlength="16"><button class="ghost">Save name</button><button class="ghost">Forget</button><span class="mono"></span>';
    r.children[0].value=p.name;r.children[3].textContent=p.id;
    r.children[1].onclick=()=>post('/api/probe',{id:p.id,name:r.children[0].value}).then(()=>toast('Saved'));
    r.children[2].onclick=()=>{if(r.children[2].dataset.armed){post('/api/probe',{id:p.id,forget:'1'}).then(()=>toast('Probe forgotten'))}
      else{r.children[2].dataset.armed=1;r.children[2].textContent='Click again';setTimeout(()=>{delete r.children[2].dataset.armed;r.children[2].textContent='Forget'},4000)}};
    $('probes').appendChild(r)});}
async function events(){try{evs=await(await fetch('/api/events')).json();renderEvents()}catch(e){}}
function renderEvents(){const ev=evs.slice(0,15);
  $('ev').innerHTML=ev.length?'':'<tr><td class="mut">No events yet</td></tr>';
  ev.forEach(e=>{const tr=$('ev').insertRow();const t=e.synced?new Date(e.t*1000).toLocaleString():`+${e.t}s after boot`;
    [t,'Out '+e.outlet,e.on?'ON':'OFF',e.source].forEach(x=>tr.insertCell().textContent=x)});}
async function loadCfg(){const c=await(await fetch('/api/config')).json();
  $('ssid').value=c.wifi.ssid;$('pwset').textContent=c.wifi.password_set?'(saved)':'';
  $('tbhost').value=c.tb.host;$('tbport').value=c.tb.port;$('tkset').textContent=c.tb.token_set?'(saved)':'';
  $('tbhh').value=c.tb.http_host;$('tbtls').checked=c.tb.use_tls;$('tbca').value=c.tb.ca_cert;$('tbhp').value=c.tb.http_port;
  $('auto').checked=c.update.auto_update;$('wst').value=c.update.window_start_h;$('wen').value=c.update.window_end_h;
  $('vg').value=c.calib.v_gain;$('i1g').value=c.calib.i1_gain;$('i2g').value=c.calib.i2_gain;$('phd').value=c.calib.phase_deg;
  $('dev').textContent=`${c.fw_title} ${c.fw} - setup WiFi: ${c.ap_ssid}`;
  $('ocfg').innerHTML=c.outlets.map((o,i)=>`<form data-api="/api/outlet-config" data-n="${i+1}"><h3>Outlet ${i+1}</h3>
<label data-help="Shown on this page and in ThingsBoard, e.g. Fridge. It does not change how the outlet behaves."><span class="lt">Name</span><input name="name" maxlength="24"></label>
<label data-help="What this outlet does after a power cut or restart. Restore last state puts it back the way it was."><span class="lt">After power returns</span><select name="power_on"><option value="0">Stay off</option><option value="1">Turn on</option><option value="2">Restore last state</option></select></label>
<label data-help="What is fitted next to this socket. Rocker switch: any flip turns the outlet on or off. Touch button: hold a finger on the pad to toggle. Disabled: only this page and ThingsBoard control it, e.g. so nobody turns the fridge off by accident."><span class="lt">Physical control</span><select name="input"><option value="1">Rocker switch</option><option value="2">Touch button</option><option value="0">Disabled</option></select></label>
<label data-help="Touch button only. The finger must stay on the pad this long before the outlet toggles, so a brush with a cloth or a wet hand does nothing. The relay click tells you it worked; lift your finger before the next toggle."><span class="lt">Touch hold time (seconds)</span><input name="touch_hold_s" type="number" min="0" max="10" step="0.5"></label>
<label class="ck" data-help="For fridges, freezers and ACs. Stops the outlet turning back on too soon after it went off, which can stall and overheat the compressor."><input type="checkbox" name="compressor_protect"><span class="lt">Compressor protection (fridge / AC)</span></label>
<label data-help="Only used when Compressor protection is ticked. After the outlet goes off for any reason (including a power cut), it waits at least this long before turning on again. 180 s (3 min) suits most fridges."><span class="lt">Minimum off time (seconds)</span><input name="min_off_s" type="number" min="0" max="1800"></label>
<button>Save</button></form>`).join('');
  c.outlets.forEach((o,i)=>{const f=$('ocfg').querySelectorAll('form')[i];const e=f.elements;e['name'].value=o.name;e['power_on'].value=o.power_on;
    e['input'].value=o.input;e['touch_hold_s'].value=o.touch_hold_s;e['compressor_protect'].checked=o.compressor_protect;e['min_off_s'].value=o.min_off_s;});
  bindForms();}
function addHelp(){document.querySelectorAll('[data-help]').forEach(l=>{if(l.helped)return;l.helped=1;
  const i=document.createElement('span');i.className='info';i.textContent='i';i.tabIndex=0;i.setAttribute('role','button');i.setAttribute('aria-label','What is this?');i.setAttribute('aria-expanded','false');
  const p=document.createElement('p');p.className='help';p.hidden=true;p.textContent=l.dataset.help;
  const t=()=>{p.hidden=!p.hidden;i.setAttribute('aria-expanded',String(!p.hidden))};
  i.onclick=e=>{e.preventDefault();e.stopPropagation();t()};i.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();t()}};
  l.querySelector('.lt').appendChild(i);l.after(p);})}
function bindForms(){addHelp();document.querySelectorAll('form[data-api]').forEach(f=>{if(f.bound)return;f.bound=true;f.onsubmit=async ev=>{ev.preventDefault();
  const d={};new FormData(f).forEach((v,k)=>d[k]=v);f.querySelectorAll('input[type=checkbox]').forEach(x=>d[x.name]=x.checked?'1':'0');
  if(f.dataset.n)d.n=f.dataset.n;const r=await post(f.dataset.api,d);toast(r.error||(r.restart?'Saved - restarting...':'Saved'));if(!r.restart)loadCfg();}})}
$('phcal').onclick=()=>post('/api/calib-phase',{}).then(r=>{toast(r.error||'Phase calibrated');if(!r.error)loadCfg()});
$('fwinst').onclick=()=>{if(confirm('Install the update now? The strip restarts: both outlets switch off for about a second, and outlets with compressor protection stay off for their minimum off time.'))
  post('/api/install-update',{}).then(r=>toast(r.error||'Installing - the strip restarts in about a minute'))};
$('reboot').onclick=()=>post('/api/reboot',{}).then(()=>toast('Restarting...'));
loadCfg();connect();
</script></body></html>)HTML";

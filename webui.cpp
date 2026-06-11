// ═══════════════════════════════════════════════════════════════
//  webui.cpp — HTTP server + admin rozhraní
//  ESP32-WROOM + 8-kanálové relé (Active HIGH)
// ═══════════════════════════════════════════════════════════════
#include "webui.h"
#include "storage.h"
#include "scheduler.h"
#include "weather.h"
#include "zones.h"
#include "config.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <time.h>

static WebServer server(80);

// ═══════════════════════════════════════════════════════════════
//  HTML šablona (inline PROGMEM)
// ═══════════════════════════════════════════════════════════════
static const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="cs">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Závlaha — Správce</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh}
header{background:#16213e;padding:12px 20px;display:flex;align-items:center;justify-content:space-between;border-bottom:2px solid #0f3460}
header h1{font-size:18px;color:#00d4aa}
.clock{font-size:14px;color:#aaa;font-family:monospace}
nav{background:#16213e;display:flex;gap:4px;padding:8px 16px;border-bottom:1px solid #0f3460;flex-wrap:wrap}
nav button{background:#0f3460;color:#ccc;border:none;padding:7px 14px;border-radius:6px;cursor:pointer;font-size:13px;transition:all 0.2s}
nav button.active,nav button:hover{background:#00d4aa;color:#000;font-weight:bold}
.tab{display:none;padding:16px}
.tab.active{display:block}
.card{background:#16213e;border-radius:10px;padding:16px;margin-bottom:14px;border:1px solid #0f3460}
.card h3{color:#00d4aa;margin-bottom:10px;font-size:15px}
.zone-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}
.zone-card{background:#0f3460;border-radius:8px;padding:12px;text-align:center;border:2px solid #0f3460;transition:all 0.3s}
.zone-card.running{border-color:#00d4aa;background:#003d2e;animation:pulse 1.5s infinite}
.zone-card.disabled{opacity:0.4}
@keyframes pulse{0%,100%{box-shadow:0 0 0 0 #00d4aa44}50%{box-shadow:0 0 0 8px #00d4aa00}}
.zone-name{font-weight:bold;font-size:13px;margin-bottom:6px}
.zone-status{font-size:11px;color:#aaa;margin-bottom:8px}
.zone-status.on{color:#00d4aa}
.btn{display:inline-block;padding:6px 12px;border-radius:5px;border:none;cursor:pointer;font-size:12px;font-weight:bold;transition:all 0.2s}
.btn-green{background:#00d4aa;color:#000}.btn-green:hover{background:#00ffcc}
.btn-red{background:#e74c3c;color:#fff}.btn-red:hover{background:#ff6b6b}
.btn-blue{background:#2980b9;color:#fff}.btn-blue:hover{background:#3498db}
.btn-orange{background:#e67e22;color:#fff}.btn-orange:hover{background:#f39c12}
.btn-gray{background:#555;color:#fff}.btn-gray:hover{background:#777}
.btn-sm{padding:4px 8px;font-size:11px}
input,select{background:#0f3460;color:#eee;border:1px solid #2980b9;border-radius:5px;padding:6px 8px;font-size:13px;width:100%}
input[type=checkbox]{width:auto}
label{font-size:12px;color:#aaa;display:block;margin-bottom:3px;margin-top:8px}
.row{display:flex;gap:8px;align-items:flex-end;flex-wrap:wrap}
.row>div{flex:1;min-width:80px}
.prog-block{background:#0a2240;border-radius:8px;padding:12px;margin-bottom:8px;border:1px solid #1a4a7a}
.day-row{display:flex;gap:4px;flex-wrap:wrap;margin:6px 0}
.day-btn{background:#1a3a5c;color:#aaa;border:1px solid #2a5a8c;border-radius:4px;padding:4px 8px;cursor:pointer;font-size:11px;font-weight:bold;transition:all 0.2s;user-select:none}
.day-btn.sel{background:#00d4aa;color:#000;border-color:#00d4aa}
.status-bar{display:flex;gap:12px;flex-wrap:wrap}
.stat{background:#0f3460;border-radius:8px;padding:10px 14px;flex:1;min-width:120px;text-align:center}
.stat-val{font-size:20px;font-weight:bold;color:#00d4aa}
.stat-lbl{font-size:11px;color:#888;margin-top:2px}
.log-table{width:100%;border-collapse:collapse;font-size:12px}
.log-table th{background:#0f3460;color:#aaa;padding:6px 8px;text-align:left}
.log-table td{padding:5px 8px;border-bottom:1px solid #0f3460}
.log-table tr:hover td{background:#0f3460}
.weather-box{display:flex;gap:12px;flex-wrap:wrap}
.w-stat{flex:1;min-width:120px;background:#0f3460;border-radius:8px;padding:12px;text-align:center}
.w-val{font-size:22px;font-weight:bold;color:#00d4aa}
.w-lbl{font-size:11px;color:#888;margin-top:4px}
.skip-badge{background:#e74c3c;color:white;padding:2px 8px;border-radius:12px;font-size:11px;font-weight:bold}
.ok-badge{background:#27ae60;color:white;padding:2px 8px;border-radius:12px;font-size:11px;font-weight:bold}
.info{color:#aaa;font-size:12px;margin-top:6px}
.separator{border:none;border-top:1px solid #0f3460;margin:12px 0}
.running-banner{background:#003d2e;border:2px solid #00d4aa;border-radius:10px;padding:14px;text-align:center;margin-bottom:14px;display:none}
.running-banner.show{display:block}
.running-banner h2{color:#00d4aa;font-size:16px}
.progress-bar{background:#0f3460;border-radius:10px;height:10px;margin:8px 0;overflow:hidden}
.progress-fill{background:#00d4aa;height:100%;border-radius:10px;transition:width 1s}
.alert{padding:10px 14px;border-radius:8px;margin-bottom:10px;font-size:13px}
.alert-warn{background:#7a4a00;border:1px solid #f39c12;color:#f39c12}
.alert-ok{background:#003d2e;border:1px solid #00d4aa;color:#00d4aa}
</style>
</head>
<body>
<header>
  <h1>&#128167; Závlaha &#8212; Správce</h1>
  <div class="clock" id="clk">--:--:--</div>
</header>
<nav>
  <button class="active" onclick="showTab('dashboard',this)">&#128202; Dashboard</button>
  <button onclick="showTab('zones',this)">&#127807; Zóny &amp; Rozvrhy</button>
  <button onclick="showTab('manual',this)">&#9889; Manuální</button>
  <button onclick="showTab('weather',this)">&#127783; Počasí</button>
  <button onclick="showTab('settings',this)">&#9881; Nastavení</button>
  <button onclick="showTab('log',this)">&#128203; Log</button>
</nav>

<!-- ── DASHBOARD ──────────────────────────────────────────────── -->
<div class="tab active" id="tab-dashboard">
  <div class="running-banner" id="running-banner">
    <h2 id="rb-title">&#9654; Zálivka běží</h2>
    <div id="rb-detail" style="color:#aaa;font-size:12px;margin-top:4px"></div>
    <div class="progress-bar"><div class="progress-fill" id="rb-progress" style="width:0%"></div></div>
    <div id="rb-time" style="color:#aaa;font-size:12px"></div>
    <br>
    <button class="btn btn-red" onclick="stopZone()">&#9209; Zastavit vše</button>
  </div>

  <div class="card">
    <h3>Přehled zón</h3>
    <div class="zone-grid" id="zone-grid">Načítám...</div>
  </div>

  <div class="card">
    <h3>Stav systému</h3>
    <div class="status-bar">
      <div class="stat"><div class="stat-val" id="st-date">--</div><div class="stat-lbl">Datum</div></div>
      <div class="stat"><div class="stat-val" id="st-time">--:--</div><div class="stat-lbl">Čas</div></div>
      <div class="stat"><div class="stat-val" id="st-next" style="font-size:13px">--</div><div class="stat-lbl">Příští zálivka</div></div>
      <div class="stat"><div class="stat-val" id="st-rain">--</div><div class="stat-lbl">Srážky 24h</div></div>
      <div class="stat"><div class="stat-val" id="st-temp">--</div><div class="stat-lbl">Teplota</div></div>
    </div>
  </div>

  <div class="card">
    <h3>Počasí &amp; zálivka</h3>
    <div id="weather-alert"></div>
    <div class="info" id="weather-detail">Načítám...</div>
  </div>

  <div class="card" id="pause-card">
    <h3>&#128683; Pozastavit zálivku</h3>
    <div id="pause-status" style="margin-bottom:10px"></div>
    <div class="row" id="pause-form">
      <div>
        <label>Pozastavit do (včetně)</label>
        <input type="date" id="pause-date">
      </div>
      <div style="flex:0">
        <button class="btn btn-orange" onclick="setPause()" style="margin-top:20px">&#128683; Pozastavit</button>
      </div>
    </div>
    <button class="btn btn-gray btn-sm" id="pause-cancel-btn" onclick="clearPause()" style="display:none;margin-top:8px">&#10003; Zrušit pauzu — obnovit zálivku</button>
  </div>
</div>

<!-- ── ZÓNY ───────────────────────────────────────────────────── -->
<div class="tab" id="tab-zones">
  <div id="zones-container">Načítám...</div>
  <div style="margin-top:10px">
    <button class="btn btn-green" onclick="saveAllZones()">&#128190; Uložit všechny zóny</button>
  </div>
</div>

<!-- ── MANUÁLNÍ ────────────────────────────────────────────────── -->
<div class="tab" id="tab-manual">
  <div class="card">
    <h3>&#9889; Manuální spuštění zóny</h3>
    <div class="row">
      <div>
        <label>Zóna</label>
        <select id="man-zone"></select>
      </div>
      <div>
        <label>Délka (min)</label>
        <input type="number" id="man-dur" value="5" min="1" max="120">
      </div>
      <div style="flex:0">
        <button class="btn btn-green" onclick="manualRun()" style="margin-top:20px">&#9654; Spustit</button>
      </div>
    </div>
    <label style="margin-top:10px"><input type="checkbox" id="man-parallel"> Paralelní spuštění (vedle případně běžící zóny)</label>
    <p class="info" style="margin-top:6px">&#9888; Bez paralelního módu se aktuální zálivka zastaví. Počasí se ignoruje.</p>
  </div>

  <div class="card">
    <h3>&#128260; Sekvenční zálivka</h3>
    <p class="info" style="margin-bottom:10px">Zóny poběží jedna po druhé automaticky. Master ventil zůstane otevřený po celou dobu.</p>
    <div id="seq-list" style="margin-bottom:10px;min-height:30px">
      <div style="color:#888;font-size:12px">Zatím žádné zóny — přidej níže</div>
    </div>
    <div class="row" style="margin-bottom:8px">
      <div>
        <label>Zóna</label>
        <select id="seq-zone"></select>
      </div>
      <div>
        <label>Délka (min)</label>
        <input type="number" id="seq-dur" value="10" min="1" max="120">
      </div>
      <div style="flex:0">
        <button class="btn btn-blue btn-sm" onclick="seqAdd()" style="margin-top:20px">+ Přidat</button>
      </div>
    </div>
    <div style="display:flex;gap:8px">
      <button class="btn btn-green" onclick="runSequence()">&#9654; Spustit sekvenci</button>
      <button class="btn btn-gray btn-sm" onclick="seqClear()">Vymazat</button>
    </div>
    <div id="seq-status" style="margin-top:8px;font-size:12px;color:#aaa"></div>
  </div>

  <div class="card">
    <h3>&#128721; Zastavit vše</h3>
    <button class="btn btn-red" onclick="stopZone()">&#9209; Zastavit okamžitě</button>
    <p class="info" style="margin-top:8px">Zastaví všechny běžící zóny, vymaže frontu a zavře master ventil.</p>
  </div>

  <div class="card">
    <h3>&#128295; Test relé</h3>
    <p class="info" style="margin-bottom:10px">Spustí každou zónu na ~3 sekundy — pro ověření zapojení.</p>
    <button class="btn btn-orange" onclick="runTest()">&#9654; Spustit test všech zón</button>
    <div id="test-status" style="margin-top:8px;font-size:12px;color:#aaa"></div>
  </div>
</div>

<!-- ── POČASÍ ──────────────────────────────────────────────────── -->
<div class="tab" id="tab-weather">
  <div class="card">
    <h3>&#127783; Aktuální data (Open-Meteo)</h3>
    <div class="weather-box">
      <div class="w-stat"><div class="w-val" id="w-past">--</div><div class="w-lbl">Srážky — posledních 24h (mm)</div></div>
      <div class="w-stat"><div class="w-val" id="w-next">--</div><div class="w-lbl">Předpověď srážek — příštích 24h (mm)</div></div>
      <div class="w-stat"><div class="w-val" id="w-temp2">--</div><div class="w-lbl">Aktuální teplota (°C)</div></div>
    </div>
    <div class="info" id="w-updated" style="margin-top:10px"></div>
    <div id="w-skip-status" style="margin-top:8px"></div>
  </div>

  <div class="card">
    <h3>&#9881; Nastavení počasí</h3>
    <div class="row">
      <div>
        <label>Zeměpisná šířka</label>
        <input type="number" id="w-lat" step="0.0001">
      </div>
      <div>
        <label>Zeměpisná délka</label>
        <input type="number" id="w-lon" step="0.0001">
      </div>
    </div>
    <div class="row" style="margin-top:10px">
      <div>
        <label>Práh — pršelo (mm za 24h)</label>
        <input type="number" id="w-past-thr" step="0.5" min="0">
      </div>
      <div>
        <label>Práh — předpověď (mm)</label>
        <input type="number" id="w-fore-thr" step="0.5" min="0">
      </div>
    </div>
    <label style="margin-top:12px"><input type="checkbox" id="w-skip-past"> Přeskočit zálivku pokud pršelo (posledních 24h)</label>
    <label style="margin-top:6px"><input type="checkbox" id="w-skip-fore"> Přeskočit zálivku při předpovědi deště</label>
    <br>
    <button class="btn btn-blue" onclick="saveWeather()" style="margin-top:12px">&#128190; Uložit nastavení</button>
    <button class="btn btn-gray btn-sm" onclick="fetchWeather()" style="margin-left:8px">&#128260; Aktualizovat data</button>
  </div>
</div>

<!-- ── NASTAVENÍ ────────────────────────────────────────────────── -->
<div class="tab" id="tab-settings">
  <div class="card">
    <h3>&#128336; Ruční nastavení času</h3>
    <p class="info" style="margin-bottom:10px">Pokud ESP32 nemá přístup k internetu (AP mód), čas lze nastavit ručně. Uloží se do RAM — po restartu je třeba nastavit znovu.</p>
    <button class="btn btn-blue" onclick="setTimeFromBrowser()">&#128336; Synchronizovat čas z prohlížeče</button>
    <hr class="separator">
    <div class="row" style="margin-top:4px">
      <div>
        <label>Nebo zadej čas ručně</label>
        <input type="datetime-local" id="manual-time">
      </div>
      <div style="flex:0">
        <button class="btn btn-gray" onclick="setTimeManual()" style="margin-top:20px">Nastavit</button>
      </div>
    </div>
    <div id="time-set-status" style="margin-top:8px;font-size:12px"></div>
  </div>

  <div class="card">
    <h3>&#128295; Systémové nastavení</h3>
    <label><input type="checkbox" id="ss-master"> Použít master ventil / čerpadlo (Relay 7)</label>
    <div class="row" style="margin-top:10px">
      <div>
        <label>Prodleva před zónou (s)</label>
        <input type="number" id="ss-pre" min="0" max="30">
      </div>
      <div>
        <label>Prodleva po zóně (s)</label>
        <input type="number" id="ss-post" min="0" max="30">
      </div>
    </div>
    <hr class="separator">
    <label>NTP server</label>
    <input type="text" id="ss-ntp">
    <br>
    <button class="btn btn-blue" onclick="saveSystem()" style="margin-top:12px">&#128190; Uložit nastavení</button>
  </div>

  <div class="card">
    <h3>&#8505; O systému</h3>
    <div class="info">
      Verze FW: <strong id="fw-ver">--</strong><br>
      IP adresa: <strong id="sys-ip">--</strong><br>
      WiFi SSID: <strong id="sys-wifi">--</strong><br>
      Uptime: <strong id="sys-uptime">--</strong>
    </div>
  </div>

  <div class="card">
    <h3>&#128465; Reset</h3>
    <button class="btn btn-red btn-sm" onclick="if(confirm('Opravdu vymazat log?')) clearLog()">Vymazat log</button>
    <button class="btn btn-red btn-sm" onclick="if(confirm('Restartovat ESP32?')) restartESP()" style="margin-left:8px">Restartovat ESP</button>
  </div>
</div>

<!-- ── LOG ─────────────────────────────────────────────────────── -->
<div class="tab" id="tab-log">
  <div class="card">
    <h3>&#128203; Záznamy zálivky</h3>
    <button class="btn btn-gray btn-sm" onclick="loadLog()" style="margin-bottom:10px">&#128260; Obnovit</button>
    <table class="log-table">
      <thead><tr><th>Datum &amp; čas</th><th>Zóna</th><th>Délka</th><th>Typ</th><th>Poznámka</th></tr></thead>
      <tbody id="log-body"><tr><td colspan="5">Načítám...</td></tr></tbody>
    </table>
  </div>
</div>

<script>
// ── Tabs ─────────────────────────────────────────────────────────
function showTab(id, btn) {
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  document.querySelectorAll('nav button').forEach(b => b.classList.remove('active'));
  document.getElementById('tab-'+id).classList.add('active');
  if(btn) btn.classList.add('active');
  if(id==='zones') loadZones();
  if(id==='weather') loadWeatherSettings();
  if(id==='settings') { loadSystem(); prefillManualTime(); }
  if(id==='log') loadLog();
  if(id==='manual') loadManualZones();
}

// ── Hodiny (lokální čas z browseru) ──────────────────────────────
function updateClock() {
  document.getElementById('clk').textContent = new Date().toLocaleTimeString('cs-CZ');
}
setInterval(updateClock, 1000); updateClock();

// ── API helper ───────────────────────────────────────────────────
async function api(path, method='GET', body=null) {
  const opts = {method, headers:{'Content-Type':'application/json'}};
  if(body) opts.body = JSON.stringify(body);
  try {
    const r = await fetch(path, opts);
    return await r.json();
  } catch(e) { return {error: e.toString()}; }
}

// ── Dashboard refresh ────────────────────────────────────────────
async function refreshDashboard() {
  const d = await api('/api/status');
  if(d.error) return;

  document.getElementById('st-date').textContent = d.date || '--';
  document.getElementById('st-time').textContent = d.time || '--:--';
  document.getElementById('st-next').textContent = d.nextRun || '--';
  document.getElementById('st-rain').textContent = d.rain24h !== undefined ? d.rain24h.toFixed(1)+' mm' : '--';
  document.getElementById('st-temp').textContent = d.tempC !== undefined ? d.tempC.toFixed(1)+'°C' : '--';

  const wa = document.getElementById('weather-alert');
  const wd = document.getElementById('weather-detail');
  if(d.weatherSkip) {
    wa.innerHTML = '<div class="alert alert-warn">&#9888; Zálivka dnes PŘESKOČENA kvůli srážkám</div>';
  } else {
    wa.innerHTML = '<div class="alert alert-ok">&#10003; Počasí OK — zálivka povolena</div>';
  }
  wd.textContent = d.weatherStatus || '';

  // Banner — podporuje více běžících zón + frontu
  const banner = document.getElementById('running-banner');
  const rz = d.runningZones || [];
  if(rz.length > 0) {
    banner.classList.add('show');
    if(rz.length === 1) {
      document.getElementById('rb-title').innerHTML = '&#9654; Zóna '+rz[0].zone+' — '+rz[0].name+' běží';
      const q = d.queueCount > 0 ? ' (fronta: '+d.queueCount+' zón)' : '';
      document.getElementById('rb-detail').textContent = q ? 'Sekvenční mód'+q : '';
      const pct = rz[0].duration > 0 ? Math.min(100, rz[0].elapsed / rz[0].duration * 100) : 0;
      document.getElementById('rb-progress').style.width = pct.toFixed(0)+'%';
      document.getElementById('rb-time').textContent = rz[0].elapsed+' / '+rz[0].duration+' min';
    } else {
      document.getElementById('rb-title').innerHTML = '&#9654; '+rz.length+' zóny běží paralelně';
      document.getElementById('rb-detail').textContent = rz.map(z => 'Zóna '+z.zone+' ('+z.elapsed+'/'+z.duration+' min)').join('  ·  ');
      document.getElementById('rb-progress').style.width = '60%';
      document.getElementById('rb-time').textContent = '';
    }
  } else {
    banner.classList.remove('show');
  }

  const grid = document.getElementById('zone-grid');
  if(d.zones) {
    grid.innerHTML = d.zones.map(z =>
      `<div class="zone-card ${z.running?'running':''} ${!z.enabled?'disabled':''}">
        <div class="zone-name">${z.name}</div>
        <div class="zone-status ${z.running?'on':''}">${z.running?'&#9654; Zálivka':'&#9679; Klidový stav'}</div>
        <button class="btn btn-green btn-sm" onclick="quickRun(${z.id})">&#9654; 5 min</button>
      </div>`
    ).join('');
  }
}
setInterval(refreshDashboard, 3000);
refreshDashboard();
loadPauseStatus();

// ── Pauza zálivky ──────────────────────────────────────────────
async function loadPauseStatus() {
  const d = await api('/api/pause');
  const statusEl = document.getElementById('pause-status');
  const formEl   = document.getElementById('pause-form');
  const cancelEl = document.getElementById('pause-cancel-btn');
  if(d.active) {
    const dt = new Date(d.until * 1000);
    statusEl.innerHTML = `<div class="alert alert-warn">&#128683; Zálivka pozastavena do <strong>${dt.toLocaleDateString('cs-CZ')}</strong></div>`;
    formEl.style.display   = 'none';
    cancelEl.style.display = 'inline-block';
  } else {
    statusEl.innerHTML = '<div class="info">Zálivka běží normálně.</div>';
    formEl.style.display   = 'flex';
    cancelEl.style.display = 'none';
    // Nastav výchozí datum na konec aktuálního měsíce
    if(!document.getElementById('pause-date').value) {
      const now = new Date();
      const lastDay = new Date(now.getFullYear(), now.getMonth()+1, 0);
      document.getElementById('pause-date').value = lastDay.toISOString().split('T')[0];
    }
  }
}

async function setPause() {
  const val = document.getElementById('pause-date').value;
  if(!val) { alert('Vyber datum'); return; }
  const d = new Date(val);
  d.setHours(23, 59, 59, 0);
  const until = Math.floor(d.getTime() / 1000);
  const r = await api('/api/pause', 'POST', {until});
  if(r.ok) { await loadPauseStatus(); }
  else alert('&#10007; Chyba: '+(r.error||''));
}

async function clearPause() {
  const r = await api('/api/pause', 'POST', {until: 0});
  if(r.ok) { await loadPauseStatus(); }
}

// ── Zóny (konfigurace) ───────────────────────────────────────────
// bit0=Po, bit1=Út, bit2=St, bit3=Čt, bit4=Pá, bit5=So, bit6=Ne
const DAY_LABELS = ['Po','Út','St','Čt','Pá','So','Ne'];

function dayCheckboxes(progIdx, zoneIdx, days) {
  return DAY_LABELS.map((d,i) =>
    `<span class="day-btn ${(days>>i)&1?'sel':''}" data-z="${zoneIdx}" data-p="${progIdx}" data-d="${i}" onclick="this.classList.toggle('sel')">${d}</span>`
  ).join('');
}

function getDayMask(zoneIdx, progIdx) {
  let mask = 0;
  document.querySelectorAll(`.day-btn[data-z="${zoneIdx}"][data-p="${progIdx}"]`).forEach(el => {
    if(el.classList.contains('sel')) mask |= (1 << parseInt(el.dataset.d));
  });
  return mask;
}

async function loadZones() {
  const d = await api('/api/zones');
  if(!d.zones) return;
  const container = document.getElementById('zones-container');
  container.innerHTML = d.zones.map((z,zi) => {
    const zi1 = zi+1;
    const progs = z.programs.map((p,pi) => `
      <div class="prog-block">
        <div class="row">
          <div style="flex:0">
            <label>Program ${pi+1}</label>
            <label><input type="checkbox" id="p-en-${zi1}-${pi}" ${p.enabled?'checked':''}> Aktivní</label>
          </div>
          <div>
            <label>Start</label>
            <input type="time" id="p-time-${zi1}-${pi}" value="${String(p.startHour).padStart(2,'0')}:${String(p.startMinute).padStart(2,'0')}">
          </div>
          <div>
            <label>Délka (min)</label>
            <input type="number" id="p-dur-${zi1}-${pi}" value="${p.durationMin}" min="1" max="120">
          </div>
        </div>
        <div style="margin-top:6px;font-size:11px;color:#aaa">Dny:</div>
        <div class="day-row">${dayCheckboxes(pi, zi1, p.days)}</div>
      </div>`).join('');
    return `<div class="card">
      <div class="row">
        <div><h3>Zóna ${zi1}</h3></div>
        <div style="flex:0"><label style="display:flex;align-items:center;gap:6px"><input type="checkbox" id="z-en-${zi1}" ${z.enabled?'checked':''}> Aktivní</label></div>
      </div>
      <label>Název zóny</label>
      <input type="text" id="z-name-${zi1}" value="${z.name}" maxlength="31">
      ${progs}
    </div>`;
  }).join('');
}

async function saveAllZones() {
  const zones = [];
  for(let zi1=1; zi1<=6; zi1++) {
    const programs = [];
    for(let pi=0; pi<3; pi++) {
      const timeVal = document.getElementById(`p-time-${zi1}-${pi}`)?.value || '06:00';
      const [h,m] = timeVal.split(':').map(Number);
      programs.push({
        enabled: document.getElementById(`p-en-${zi1}-${pi}`)?.checked || false,
        days: getDayMask(zi1, pi),
        startHour: h, startMinute: m,
        durationMin: parseInt(document.getElementById(`p-dur-${zi1}-${pi}`)?.value || 10)
      });
    }
    zones.push({
      id: zi1,
      name: document.getElementById(`z-name-${zi1}`)?.value || `Zona ${zi1}`,
      enabled: document.getElementById(`z-en-${zi1}`)?.checked || false,
      programs
    });
  }
  const r = await api('/api/zones', 'POST', {zones});
  alert(r.ok ? '&#10003; Uloženo!' : '&#10007; Chyba při ukládání');
}

// ── Manuální ──────────────────────────────────────────────────────
async function loadManualZones() {
  const d = await api('/api/zones');
  if(!d.zones) return;
  [document.getElementById('man-zone'), document.getElementById('seq-zone')].forEach(sel => {
    sel.innerHTML = '';
    d.zones.forEach((z,i) => {
      const o = document.createElement('option');
      o.value = i+1; o.textContent = `Zóna ${i+1} — ${z.name}`;
      sel.appendChild(o);
    });
  });
}

async function manualRun() {
  const zone     = parseInt(document.getElementById('man-zone').value);
  const dur      = parseInt(document.getElementById('man-dur').value);
  const parallel = document.getElementById('man-parallel').checked;
  if(!zone || !dur) return;
  const r = await api('/api/run', 'POST', {zone, minutes: dur, parallel});
  alert(r.ok
    ? `&#10003; Zóna ${zone} spuštěna na ${dur} min${parallel?' (paralelně)':''}`
    : '&#10007; Chyba: '+r.error);
  refreshDashboard();
}

async function quickRun(zone) {
  const r = await api('/api/run', 'POST', {zone, minutes: 5, parallel: false});
  if(r.ok) refreshDashboard();
}

// ── Sekvenční zálivka ─────────────────────────────────────────────
let _seqList = [];

function seqAdd() {
  const zone = parseInt(document.getElementById('seq-zone').value);
  const mins = parseInt(document.getElementById('seq-dur').value);
  if(!zone || !mins || mins < 1 || mins > 120) return;
  _seqList.push({zone, minutes: mins});
  renderSeqList();
}

function seqRemove(idx) {
  _seqList.splice(idx, 1);
  renderSeqList();
}

function seqClear() {
  _seqList = [];
  renderSeqList();
  document.getElementById('seq-status').textContent = '';
}

function renderSeqList() {
  const el = document.getElementById('seq-list');
  if(_seqList.length === 0) {
    el.innerHTML = '<div style="color:#888;font-size:12px">Zatím žádné zóny — přidej níže</div>';
    return;
  }
  const total = _seqList.reduce((s,e) => s + e.minutes, 0);
  el.innerHTML = _seqList.map((e,i) =>
    `<div style="display:flex;justify-content:space-between;align-items:center;padding:5px 0;border-bottom:1px solid #0f3460">
      <span style="font-size:12px">${i+1}. Zóna ${e.zone} — ${e.minutes} min</span>
      <button onclick="seqRemove(${i})" style="background:#e74c3c;color:#fff;border:none;border-radius:3px;padding:2px 7px;font-size:10px;cursor:pointer">&#10005;</button>
    </div>`
  ).join('') +
  `<div style="font-size:11px;color:#aaa;margin-top:5px">Celkem: ${_seqList.length} zón · ${total} min</div>`;
}

async function runSequence() {
  if(_seqList.length === 0) { alert('Přidej aspoň jednu zónu'); return; }
  const r = await api('/api/run-sequence', 'POST', {sequence: _seqList});
  if(r.ok) {
    document.getElementById('seq-status').textContent = '&#10003; Sekvence spuštěna — '+r.count+' zón';
    _seqList = [];
    renderSeqList();
    refreshDashboard();
  } else {
    document.getElementById('seq-status').textContent = '&#10007; Chyba: '+(r.error||'');
  }
}

async function stopZone() {
  await api('/api/stop', 'POST', {});
  refreshDashboard();
}

async function runTest() {
  document.getElementById('test-status').textContent = 'Test probíhá... (každá zóna ~3 s)';
  const r = await api('/api/test', 'POST', {});
  document.getElementById('test-status').textContent = r.ok ? '&#10003; Test dokončen' : '&#10007; Chyba: '+r.error;
}

// ── Počasí ─────────────────────────────────────────────────────────
async function loadWeatherSettings() {
  const d = await api('/api/weather');
  if(d.settings) {
    document.getElementById('w-lat').value = d.settings.lat;
    document.getElementById('w-lon').value = d.settings.lon;
    document.getElementById('w-past-thr').value = d.settings.pastThr;
    document.getElementById('w-fore-thr').value = d.settings.foreThr;
    document.getElementById('w-skip-past').checked = d.settings.skipPast;
    document.getElementById('w-skip-fore').checked = d.settings.skipFore;
  }
  if(d.data) {
    document.getElementById('w-past').textContent  = d.data.past24 !== undefined ? d.data.past24.toFixed(1)+' mm' : '--';
    document.getElementById('w-next').textContent  = d.data.next24 !== undefined ? d.data.next24.toFixed(1)+' mm' : '--';
    document.getElementById('w-temp2').textContent = d.data.temp   !== undefined ? d.data.temp.toFixed(1)+'°C'   : '--';
    if(d.data.lastUpdate) {
      const dt = new Date(d.data.lastUpdate * 1000);
      document.getElementById('w-updated').textContent = 'Poslední aktualizace: ' + dt.toLocaleString('cs-CZ');
    }
    document.getElementById('w-skip-status').innerHTML = d.data.shouldSkip
      ? '<span class="skip-badge">&#9940; Zálivka přeskočena</span>'
      : '<span class="ok-badge">&#10003; Zálivka povolena</span>';
  }
}

async function saveWeather() {
  const r = await api('/api/weather', 'POST', {
    lat:     parseFloat(document.getElementById('w-lat').value),
    lon:     parseFloat(document.getElementById('w-lon').value),
    pastThr: parseFloat(document.getElementById('w-past-thr').value),
    foreThr: parseFloat(document.getElementById('w-fore-thr').value),
    skipPast: document.getElementById('w-skip-past').checked,
    skipFore: document.getElementById('w-skip-fore').checked
  });
  alert(r.ok ? '&#10003; Uloženo!' : '&#10007; Chyba');
}

async function fetchWeather() {
  document.getElementById('w-updated').textContent = 'Aktualizuji...';
  await api('/api/weather/refresh', 'POST', {});
  setTimeout(loadWeatherSettings, 4000);
}

// ── Ruční čas ──────────────────────────────────────────────────────
function prefillManualTime() {
  const now = new Date();
  const pad = n => String(n).padStart(2,'0');
  const local = `${now.getFullYear()}-${pad(now.getMonth()+1)}-${pad(now.getDate())}T${pad(now.getHours())}:${pad(now.getMinutes())}`;
  document.getElementById('manual-time').value = local;
}

async function setTimeFromBrowser() {
  const epoch = Math.floor(Date.now() / 1000);
  const r = await api('/api/time', 'POST', {epoch});
  const el = document.getElementById('time-set-status');
  if(r.ok) {
    el.innerHTML = '&#10003; Čas nastaven: ' + new Date().toLocaleString('cs-CZ');
    el.style.color = '#00d4aa';
  } else {
    el.textContent = '&#10007; Chyba: ' + (r.error || '');
    el.style.color = '#e74c3c';
  }
}

async function setTimeManual() {
  const val = document.getElementById('manual-time').value;
  if(!val) { alert('Vyber datum a čas'); return; }
  const epoch = Math.floor(new Date(val).getTime() / 1000);
  const r = await api('/api/time', 'POST', {epoch});
  const el = document.getElementById('time-set-status');
  if(r.ok) {
    el.innerHTML = '&#10003; Čas nastaven: ' + new Date(val).toLocaleString('cs-CZ');
    el.style.color = '#00d4aa';
  } else {
    el.textContent = '&#10007; Chyba: ' + (r.error || '');
    el.style.color = '#e74c3c';
  }
}

// ── Systém ──────────────────────────────────────────────────────────
async function loadSystem() {
  const d = await api('/api/system');
  if(d.settings) {
    document.getElementById('ss-master').checked = d.settings.masterEnabled;
    document.getElementById('ss-pre').value      = d.settings.preDelay;
    document.getElementById('ss-post').value     = d.settings.postDelay;
    document.getElementById('ss-ntp').value      = d.settings.ntp;
  }
  if(d.info) {
    document.getElementById('fw-ver').textContent    = d.info.version;
    document.getElementById('sys-ip').textContent    = d.info.ip;
    document.getElementById('sys-wifi').textContent  = d.info.wifi;
    document.getElementById('sys-uptime').textContent = d.info.uptime;
  }
}

async function saveSystem() {
  const r = await api('/api/system', 'POST', {
    masterEnabled: document.getElementById('ss-master').checked,
    preDelay:  parseInt(document.getElementById('ss-pre').value),
    postDelay: parseInt(document.getElementById('ss-post').value),
    ntp: document.getElementById('ss-ntp').value
  });
  alert(r.ok ? '&#10003; Uloženo!' : '&#10007; Chyba');
}

async function clearLog() {
  await api('/api/log/clear', 'POST', {});
  loadLog();
}

async function restartESP() {
  await api('/api/restart', 'POST', {});
  setTimeout(() => location.reload(), 8000);
}

// ── Log ───────────────────────────────────────────────────────────
async function loadLog() {
  const d = await api('/api/log');
  const tbody = document.getElementById('log-body');
  if(!d.entries || d.entries.length === 0) {
    tbody.innerHTML = '<tr><td colspan="5" style="color:#888;text-align:center">Žádné záznamy</td></tr>';
    return;
  }
  const trigLabels = ['Manuálně','Program','Test'];
  tbody.innerHTML = d.entries.map(e => {
    const dt = new Date(e.timestamp * 1000);
    return `<tr>
      <td>${dt.toLocaleString('cs-CZ')}</td>
      <td>${e.zone > 0 ? 'Zóna '+e.zone : '—'}</td>
      <td>${e.durationMin > 0 ? e.durationMin+' min' : '—'}</td>
      <td>${trigLabels[e.trigger] || '?'}</td>
      <td>${e.note}</td>
    </tr>`;
  }).join('');
}
</script>
</body>
</html>
)rawhtml";

// ═══════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════

static void sendJson(const String &json) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

static void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

// ── Extern helpers (definovány v irrigation.ino) ─────────────────
extern String getSystemIP(void);
extern String getWiFiSSID(void);
extern bool   isWeatherRefreshNeeded;
extern void   triggerWeatherUpdate(void);

// ═══════════════════════════════════════════════════════════════
//  GET /api/status
// ═══════════════════════════════════════════════════════════════
static void handleStatus() {
  WeatherData wd = Weather_GetData();

  struct tm ti;
  bool timeOk = getLocalTime(&ti);
  char dateBuf[16] = "--";
  char timeBuf[8]  = "--:--";
  if (timeOk) {
    strftime(dateBuf, sizeof(dateBuf), "%d.%m.%Y", &ti);
    strftime(timeBuf, sizeof(timeBuf), "%H:%M",    &ti);
  }

  JsonDocument doc;
  doc["date"]          = dateBuf;
  doc["time"]          = timeBuf;
  doc["nextRun"]       = Scheduler_NextRunString();
  doc["running"]       = Zones_AnyRunning();
  doc["runningCount"]  = Zones_RunningCount();
  doc["queueCount"]    = Queue_Count();
  doc["weatherSkip"]   = Weather_ShouldSkip();
  doc["weatherStatus"] = Weather_StatusString();
  doc["rain24h"]       = wd.past24hRainMm;
  doc["tempC"]         = wd.currentTempC;

  // Běžící zóny (může jich být více při paralelním módu)
  JsonArray running = doc["runningZones"].to<JsonArray>();
  for (uint8_t z = 1; z <= ZONE_COUNT; z++) {
    ZoneRunState zrs = Zone_GetState(z);
    if (!zrs.running) continue;
    ZoneConfig zc = Storage_GetZone(z);
    JsonObject rz = running.add<JsonObject>();
    rz["zone"]     = z;
    rz["name"]     = zc.name;
    rz["duration"] = zrs.durationMin;
    rz["elapsed"]  = (uint16_t)((millis() - zrs.startMs) / 60000UL);
  }

  // Všechny zóny pro grid
  JsonArray zones = doc["zones"].to<JsonArray>();
  for (uint8_t z = 1; z <= ZONE_COUNT; z++) {
    ZoneConfig zc    = Storage_GetZone(z);
    ZoneRunState zrs = Zone_GetState(z);
    JsonObject zo = zones.add<JsonObject>();
    zo["id"]      = z;
    zo["name"]    = zc.name;
    zo["enabled"] = zc.enabled;
    zo["running"] = zrs.running;
  }

  String out; serializeJson(doc, out);
  sendJson(out);
}

// ═══════════════════════════════════════════════════════════════
//  GET /api/zones
// ═══════════════════════════════════════════════════════════════
static void handleGetZones() {
  JsonDocument doc;
  JsonArray zones = doc["zones"].to<JsonArray>();
  for (uint8_t z = 1; z <= ZONE_COUNT; z++) {
    ZoneConfig zc = Storage_GetZone(z);
    JsonObject zo = zones.add<JsonObject>();
    zo["id"]      = z;
    zo["name"]    = zc.name;
    zo["enabled"] = zc.enabled;
    JsonArray progs = zo["programs"].to<JsonArray>();
    for (int p = 0; p < MAX_PROGRAMS_PER_ZONE; p++) {
      JsonObject po = progs.add<JsonObject>();
      po["enabled"]     = zc.programs[p].enabled;
      po["days"]        = zc.programs[p].days;
      po["startHour"]   = zc.programs[p].startHour;
      po["startMinute"] = zc.programs[p].startMinute;
      po["durationMin"] = zc.programs[p].durationMin;
    }
  }
  String out; serializeJson(doc, out);
  sendJson(out);
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/zones
// ═══════════════════════════════════════════════════════════════
static void handlePostZones() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON parse error\"}");
    return;
  }
  JsonArray zones = doc["zones"];
  for (JsonObject zo : zones) {
    uint8_t z = zo["id"].as<uint8_t>();
    if (z < 1 || z > ZONE_COUNT) continue;
    ZoneConfig zc = Storage_GetZone(z);
    const char *nm = zo["name"].as<const char*>();
    if (nm) strlcpy(zc.name, nm, sizeof(zc.name));
    zc.enabled = zo["enabled"].as<bool>();
    JsonArray progs = zo["programs"];
    for (int p = 0; p < MAX_PROGRAMS_PER_ZONE && p < (int)progs.size(); p++) {
      zc.programs[p].enabled     = progs[p]["enabled"].as<bool>();
      zc.programs[p].days        = progs[p]["days"].as<uint8_t>();
      zc.programs[p].startHour   = progs[p]["startHour"].as<uint8_t>();
      zc.programs[p].startMinute = progs[p]["startMinute"].as<uint8_t>();
      zc.programs[p].durationMin = progs[p]["durationMin"].as<uint16_t>();
    }
    Storage_SetZone(z, zc);
  }
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/run
//  Body: {"zone":1,"minutes":5,"parallel":false}
// ═══════════════════════════════════════════════════════════════
static void handleRun() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  uint8_t  zone     = doc["zone"].as<uint8_t>();
  uint16_t mins     = doc["minutes"].as<uint16_t>();
  bool     parallel = doc["parallel"] | false;
  bool ok = Zone_Start(zone, mins, RUN_MANUAL, parallel);
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Neplatné parametry\"}");
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/run-sequence
//  Body: {"sequence":[{"zone":1,"minutes":20},{"zone":2,"minutes":15}]}
// ═══════════════════════════════════════════════════════════════
static void handleRunSequence() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON error\"}");
    return;
  }
  JsonArray seq = doc["sequence"];
  if (!seq || seq.size() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Prázdná sekvence\"}");
    return;
  }

  Zone_StopAll();   // zastaví vše a vymaže frontu

  bool first = true;
  int  count = 0;
  for (JsonObject entry : seq) {
    uint8_t  z = entry["zone"].as<uint8_t>();
    uint16_t m = entry["minutes"].as<uint16_t>();
    if (z < 1 || z > ZONE_COUNT || m == 0 || m > 120) continue;
    if (first) {
      Zone_Start(z, m, RUN_SEQUENCE, false);
      first = false;
    } else {
      Queue_Add(z, m);
    }
    count++;
  }

  if (count == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Žádné platné zóny\"}");
    return;
  }
  char buf[48];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"count\":%d}", count);
  sendJson(String(buf));
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/stop
// ═══════════════════════════════════════════════════════════════
static void handleStop() {
  Zone_StopAll();
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/test — každá zóna ~3 s
// ═══════════════════════════════════════════════════════════════
static void handleTest() {
  Zone_StopAll();
  for (uint8_t z = 1; z <= ZONE_COUNT; z++) {
    Zone_Start(z, 1, RUN_TEST, false);
    delay(3000);
    Zone_Stop(z);
    delay(300);
  }
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  GET /api/weather
// ═══════════════════════════════════════════════════════════════
static void handleGetWeather() {
  WeatherSettings ws = Storage_GetWeather();
  WeatherData wd     = Weather_GetData();
  JsonDocument doc;
  doc["settings"]["lat"]      = ws.latitude;
  doc["settings"]["lon"]      = ws.longitude;
  doc["settings"]["pastThr"]  = ws.pastRainThreshMm;
  doc["settings"]["foreThr"]  = ws.forecastRainThreshMm;
  doc["settings"]["skipPast"] = ws.rainSkipEnabled;
  doc["settings"]["skipFore"] = ws.forecastSkipEnabled;
  doc["data"]["past24"]       = wd.past24hRainMm;
  doc["data"]["next24"]       = wd.next24hRainMm;
  doc["data"]["temp"]         = wd.currentTempC;
  doc["data"]["lastUpdate"]   = (long)wd.lastUpdate;
  doc["data"]["shouldSkip"]   = Weather_ShouldSkip();
  String out; serializeJson(doc, out);
  sendJson(out);
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/weather
// ═══════════════════════════════════════════════════════════════
static void handlePostWeather() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  WeatherSettings ws = Storage_GetWeather();
  ws.latitude             = doc["lat"].as<float>();
  ws.longitude            = doc["lon"].as<float>();
  ws.pastRainThreshMm     = doc["pastThr"].as<float>();
  ws.forecastRainThreshMm = doc["foreThr"].as<float>();
  ws.rainSkipEnabled      = doc["skipPast"].as<bool>();
  ws.forecastSkipEnabled  = doc["skipFore"].as<bool>();
  Storage_SetWeather(ws);
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  GET /api/pause
// ═══════════════════════════════════════════════════════════════
static void handleGetPause() {
  time_t pauseUntil = Storage_GetPauseUntil();
  time_t now        = time(nullptr);
  bool   active     = (pauseUntil > 0 && now < pauseUntil);
  // Pokud vypršela, automaticky vymaž
  if (pauseUntil > 0 && now >= pauseUntil) {
    Storage_SetPauseUntil(0);
    pauseUntil = 0;
    active = false;
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"active\":%s,\"until\":%ld}",
           active ? "true" : "false", (long)pauseUntil);
  sendJson(String(buf));
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/pause
//  Body: {"until": 1234567890}  — unix timestamp; 0 = zrušit pauzu
// ═══════════════════════════════════════════════════════════════
static void handleSetPause() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  time_t until = (time_t)doc["until"].as<long>();
  Storage_SetPauseUntil(until);
  if (until > 0) {
    struct tm *ti = localtime(&until);
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y", ti);
    Serial.printf("[WEB] Zálivka pozastavena do %s\n", buf);
  } else {
    Serial.println("[WEB] Pauza zálivky zrušena");
  }
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/weather/refresh
// ═══════════════════════════════════════════════════════════════
static void handleWeatherRefresh() {
  triggerWeatherUpdate();
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  GET /api/system
// ═══════════════════════════════════════════════════════════════
static void handleGetSystem() {
  SystemSettings ss = Storage_GetSystem();
  JsonDocument doc;
  doc["settings"]["masterEnabled"] = ss.masterValveEnabled;
  doc["settings"]["preDelay"]      = ss.masterPreDelay;
  doc["settings"]["postDelay"]     = ss.masterPostDelay;
  doc["settings"]["ntp"]           = ss.ntpServer;

  doc["info"]["version"] = FW_VERSION;
  doc["info"]["ip"]      = getSystemIP();
  doc["info"]["wifi"]    = getWiFiSSID();
  doc["info"]["uptime"]  = String(millis() / 1000) + " s";

  String out; serializeJson(doc, out);
  sendJson(out);
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/system
// ═══════════════════════════════════════════════════════════════
static void handlePostSystem() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  SystemSettings ss = Storage_GetSystem();
  ss.masterValveEnabled = doc["masterEnabled"].as<bool>();
  ss.masterPreDelay     = doc["preDelay"].as<uint8_t>();
  ss.masterPostDelay    = doc["postDelay"].as<uint8_t>();
  const char *ntp = doc["ntp"].as<const char*>();
  if (ntp) strlcpy(ss.ntpServer, ntp, sizeof(ss.ntpServer));
  Storage_SetSystem(ss);
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  GET /api/log
// ═══════════════════════════════════════════════════════════════
static void handleGetLog() {
  int cnt = Log_Count();
  JsonDocument doc;
  JsonArray arr = doc["entries"].to<JsonArray>();
  for (int i = 0; i < cnt; i++) {
    LogEntry e = Log_Get(i);
    JsonObject eo = arr.add<JsonObject>();
    eo["timestamp"]   = (long)e.timestamp;
    eo["zone"]        = e.zone;
    eo["trigger"]     = e.trigger;
    eo["durationMin"] = e.durationMin;
    eo["note"]        = e.note;
  }
  String out; serializeJson(doc, out);
  sendJson(out);
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/log/clear
// ═══════════════════════════════════════════════════════════════
static void handleLogClear() {
  Log_Clear();
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/restart
// ═══════════════════════════════════════════════════════════════
static void handleRestart() {
  sendJson("{\"ok\":true}");
  delay(500);
  ESP.restart();
}

// ═══════════════════════════════════════════════════════════════
//  POST /api/time
//  Body: {"epoch": 1234567890}  — nastaví systémový čas (RAM, do restartu)
// ═══════════════════════════════════════════════════════════════
static void handleSetTime() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  long epoch = doc["epoch"].as<long>();
  if (epoch < 1000000000L) {   // sanity check — rok 2001+
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Neplatný epoch\"}");
    return;
  }
  struct timeval tv = { (time_t)epoch, 0 };
  settimeofday(&tv, nullptr);
  struct tm *ti = localtime((time_t*)&epoch);
  char buf[32];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", ti);
  Serial.printf("[WEB] Čas nastaven ručně: %s\n", buf);
  sendJson("{\"ok\":true}");
}

// ═══════════════════════════════════════════════════════════════
//  Init & Handle
// ═══════════════════════════════════════════════════════════════
void WebUI_Init(void) {
  server.on("/",                    HTTP_GET,  handleRoot);
  server.on("/api/status",          HTTP_GET,  handleStatus);
  server.on("/api/zones",           HTTP_GET,  handleGetZones);
  server.on("/api/zones",           HTTP_POST, handlePostZones);
  server.on("/api/run",             HTTP_POST, handleRun);
  server.on("/api/run-sequence",    HTTP_POST, handleRunSequence);
  server.on("/api/stop",            HTTP_POST, handleStop);
  server.on("/api/test",            HTTP_POST, handleTest);
  server.on("/api/weather",         HTTP_GET,  handleGetWeather);
  server.on("/api/weather",         HTTP_POST, handlePostWeather);
  server.on("/api/weather/refresh", HTTP_POST, handleWeatherRefresh);
  server.on("/api/system",          HTTP_GET,  handleGetSystem);
  server.on("/api/system",          HTTP_POST, handlePostSystem);
  server.on("/api/log",             HTTP_GET,  handleGetLog);
  server.on("/api/log/clear",       HTTP_POST, handleLogClear);
  server.on("/api/pause",           HTTP_GET,  handleGetPause);
  server.on("/api/pause",           HTTP_POST, handleSetPause);
  server.on("/api/time",            HTTP_POST, handleSetTime);
  server.on("/api/restart",         HTTP_POST, handleRestart);
  server.begin();
  Serial.println("[WEB] HTTP server spuštěn na portu 80");
}

void WebUI_Handle(void) {
  server.handleClient();
}

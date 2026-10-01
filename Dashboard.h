#pragma once

const char DASHBOARD_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ClimaNode</title>
<style>
  :root {
    color-scheme: dark;
    --bg: #0f172a;
    --card: #1e293b;
    --text: #e2e8f0;
    --muted: #94a3b8;
    --accent: #38bdf8;
    --ok: #4ade80;
    --err: #f87171;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    min-height: 100vh;
    background: var(--bg);
    color: var(--text);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 24px 16px 48px;
  }
  h1 { font-size: 1.4rem; font-weight: 600; margin: 0 0 4px; }
  .subtitle { color: var(--muted); font-size: 0.85rem; margin-bottom: 24px; }
  .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 16px; width: 100%; max-width: 480px; }
  .card { background: var(--card); border-radius: 16px; padding: 20px; text-align: center; }
  .card .value { font-size: 2.4rem; font-weight: 700; }
  .card .unit { font-size: 1.2rem; color: var(--muted); }
  .card .label { color: var(--muted); font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.05em; margin-top: 4px; }
  .clock { width: 100%; max-width: 480px; margin-bottom: 16px; background: var(--card); border-radius: 16px; padding: 20px; text-align: center; }
  .clock .time { font-size: 2.4rem; font-weight: 700; font-variant-numeric: tabular-nums; }
  .clock .date { color: var(--muted); font-size: 0.95rem; margin-top: 4px; }
  .clock .date::first-letter { text-transform: uppercase; }
  .status { width: 100%; max-width: 480px; margin-top: 16px; background: var(--card); border-radius: 16px; padding: 20px; }
  .status h2 { margin: 0 0 12px; font-size: 1rem; font-weight: 600; }
  .row { display: flex; justify-content: space-between; align-items: center; padding: 6px 0; font-size: 0.9rem; border-bottom: 1px solid rgba(148,163,184,0.15); }
  .row:last-child { border-bottom: none; }
  .row .k { color: var(--muted); }
  .dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 6px; }
  .dot.ok { background: var(--ok); }
  .dot.err { background: var(--err); }
  .bars { display: inline-flex; align-items: flex-end; gap: 2px; height: 14px; margin-right: 6px; }
  .bars span { width: 4px; background: rgba(148,163,184,0.3); border-radius: 1px; display: inline-block; }
  .bars span.on { background: var(--accent); }
  .bars span:nth-child(1) { height: 25%; }
  .bars span:nth-child(2) { height: 50%; }
  .bars span:nth-child(3) { height: 75%; }
  .bars span:nth-child(4) { height: 100%; }
  footer { margin-top: 24px; color: var(--muted); font-size: 0.75rem; }
</style>
</head>
<body>
  <h1>ClimaNode</h1>
  <div class="subtitle">Dashboard ambientale</div>

  <div class="clock">
    <div class="time" id="clockTime">--:--:--</div>
    <div class="date" id="clockDate">Ora non sincronizzata</div>
  </div>

  <div class="cards">
    <div class="card">
      <div class="value" id="temperature">--<span class="unit">&deg;C</span></div>
      <div class="label">Temperatura</div>
    </div>
    <div class="card">
      <div class="value" id="humidity">--<span class="unit">%</span></div>
      <div class="label">Umidit&agrave;</div>
    </div>
  </div>

  <div class="status">
    <h2>Stato WiFi</h2>
    <div class="row"><span class="k">Connessione</span><span id="wifiState"><span class="dot err" id="wifiDot"></span><span id="wifiStateText">--</span></span></div>
    <div class="row"><span class="k">SSID</span><span id="wifiSsid">--</span></div>
    <div class="row"><span class="k">Segnale</span><span>
      <span class="bars" id="wifiBars"><span></span><span></span><span></span><span></span></span>
      <span id="wifiRssi">--</span>
    </span></div>
    <div class="row"><span class="k">Indirizzo IP</span><span id="wifiIp">--</span></div>
    <div class="row"><span class="k">Canale</span><span id="wifiChannel">--</span></div>
  </div>

  <footer id="lastUpdate">In attesa di dati...</footer>

<script>
// Ora locale del device, rappresentata come istante UTC per non applicare
// il fuso del browser; avanza in locale tra un polling e l'altro.
let deviceTime = null;
let deviceTimeAt = 0;
let clockTimer = null;

function deviceNow() {
  return deviceTime + (performance.now() - deviceTimeAt);
}

function renderClock() {
  const timeEl = document.getElementById('clockTime');
  const dateEl = document.getElementById('clockDate');
  if (deviceTime === null) {
    timeEl.textContent = '--:--:--';
    dateEl.textContent = 'Ora non sincronizzata';
    return;
  }
  const now = new Date(deviceNow());
  timeEl.textContent = now.toLocaleTimeString('it-IT', { timeZone: 'UTC' });
  dateEl.textContent = now.toLocaleDateString('it-IT', {
    timeZone: 'UTC', weekday: 'long', day: 'numeric', month: 'long', year: 'numeric'
  });
}

// Ridisegna l'orologio e programma il prossimo aggiornamento allo scoccare
// del secondo successivo (+20 ms per non arrivare appena prima del cambio).
function tickClock() {
  clearTimeout(clockTimer);
  renderClock();
  const delay = deviceTime === null ? 500 : 1000 - (deviceNow() % 1000) + 20;
  clockTimer = setTimeout(tickClock, delay);
}

async function refresh() {
  try {
    const [climateRes, statusRes] = await Promise.all([
      fetch('/api/v1/climate'),
      fetch('/api/v1/status')
    ]);
    const climate = await climateRes.json();
    const status = await statusRes.json();

    document.getElementById('temperature').innerHTML = climate.temperature.toFixed(1) + '<span class="unit">&deg;C</span>';
    document.getElementById('humidity').innerHTML = climate.humidity.toFixed(1) + '<span class="unit">%</span>';

    if (status.time && status.time.synced) {
      // L'ora ricevuta è troncata al secondo: riallinea solo se lo scarto è
      // significativo, altrimenti l'orologio farebbe piccoli salti indietro.
      const received = Date.parse(status.time.local + 'Z');
      const estimated = deviceTime === null ? null : deviceNow();
      if (estimated === null || Math.abs(received - estimated) > 1500) {
        deviceTime = received;
        deviceTimeAt = performance.now();
      }
    } else {
      deviceTime = null;
    }
    tickClock();

    const wifi = status.wifi;
    const dot = document.getElementById('wifiDot');
    const bars = document.querySelectorAll('#wifiBars span');

    if (wifi.connected) {
      dot.className = 'dot ok';
      document.getElementById('wifiStateText').textContent = 'Connesso';
      document.getElementById('wifiSsid').textContent = wifi.ssid;
      document.getElementById('wifiIp').textContent = wifi.ip;
      document.getElementById('wifiChannel').textContent = wifi.channel;
      document.getElementById('wifiRssi').textContent = wifi.rssi + ' dBm';

      const level = wifi.rssi >= -55 ? 4 : wifi.rssi >= -65 ? 3 : wifi.rssi >= -75 ? 2 : 1;
      bars.forEach((bar, i) => bar.classList.toggle('on', i < level));
    } else {
      dot.className = 'dot err';
      document.getElementById('wifiStateText').textContent = 'Disconnesso';
      document.getElementById('wifiSsid').textContent = '--';
      document.getElementById('wifiIp').textContent = '--';
      document.getElementById('wifiChannel').textContent = '--';
      document.getElementById('wifiRssi').textContent = '--';
      bars.forEach(bar => bar.classList.remove('on'));
    }

    document.getElementById('lastUpdate').textContent = 'Ultimo aggiornamento: ' + new Date().toLocaleTimeString();
  } catch (e) {
    document.getElementById('lastUpdate').textContent = 'Errore di connessione al device';
  }
}

refresh();
setInterval(refresh, 3000);
tickClock();
</script>
</body>
</html>
)HTML";

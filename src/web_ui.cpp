#include "web_ui.h"

#include <WebServer.h>
#include <time.h>

#include "app_state.h"
#include "config.h"
#include "report.h"

static WebServer server(80);

class StringPrint : public Print {
public:
  explicit StringPrint(String &buffer) : buf(buffer) {}
  size_t write(uint8_t c) override {
    buf += (char)c;
    return 1;
  }
  size_t write(const uint8_t *buffer, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      buf += (char)buffer[i];
    }
    return size;
  }

private:
  String &buf;
};

static String buildStatusJson() {
  String json;
  json.reserve(256);
  json += "{";
  json += "\"time_valid\":";
  json += (timeValid ? "true" : "false");

  struct tm tmNow;
  if (getLocalTimeSafe(tmNow)) {
    char dateBuf[16];
    char timeBuf[16];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
             tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
             tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
    json += ",\"date\":\"";
    json += dateBuf;
    json += "\",\"time\":\"";
    json += timeBuf;
    json += "\"";
  }

  uint32_t weekSeconds = 0;
  float weekLiters = 0.0f;
  computeWeekTotals(weekSeconds, weekLiters);

  json += ",\"valve\":\"";
  json += (valveState ? "OPEN" : "CLOSED");
  json += "\"";
  json += ",\"flow_lpm\":";
  json += String(flowRateLpm, 2);
  json += ",\"total_liters\":";
  json += String(totalLiters, 3);
  json += ",\"daily_liters\":";
  json += String(dailyLiters, 3);
  json += ",\"week_seconds\":";
  json += String(weekSeconds);
  json += ",\"week_liters\":";
  json += String(weekLiters, 3);
  json += ",\"flow_active_lpm\":";
  json += String(config.flowActiveLpm, 3);
  json += ",\"report_interval_ms\":";
  json += String(config.reportIntervalMs);
  json += ",\"close_start\":\"";
  if (config.closeStartHour < 10) json += "0";
  json += String(config.closeStartHour);
  json += ":";
  if (config.closeStartMin < 10) json += "0";
  json += String(config.closeStartMin);
  json += "\"";
  json += ",\"close_end\":\"";
  if (config.closeEndHour < 10) json += "0";
  json += String(config.closeEndHour);
  json += ":";
  if (config.closeEndMin < 10) json += "0";
  json += String(config.closeEndMin);
  json += "\"";
  json += "}";
  return json;
}

static String buildConfigJson() {
  String json;
  json.reserve(256);
  json += "{";
  json += "\"flow_active_lpm\":";
  json += String(config.flowActiveLpm, 3);
  json += ",\"report_interval_ms\":";
  json += String(config.reportIntervalMs);
  json += ",\"close_start_hour\":";
  json += String(config.closeStartHour);
  json += ",\"close_start_min\":";
  json += String(config.closeStartMin);
  json += ",\"close_end_hour\":";
  json += String(config.closeEndHour);
  json += ",\"close_end_min\":";
  json += String(config.closeEndMin);
  json += ",\"pulses_per_liter\":";
  json += String(config.pulsesPerLiter, 2);
  json += ",\"tz_info\":\"";
  json += config.tzInfo;
  json += "\"";
  json += "}";
  return json;
}

static bool applyConfigFromArgs() {
  if (server.args() == 0) {
    return false;
  }

  float flow = server.arg("flow_active_lpm").toFloat();
  uint32_t reportMs = (uint32_t)server.arg("report_interval_ms").toInt();
  int csh = server.arg("close_start_hour").toInt();
  int csm = server.arg("close_start_min").toInt();
  int ceh = server.arg("close_end_hour").toInt();
  int cem = server.arg("close_end_min").toInt();
  float ppl = server.arg("pulses_per_liter").toFloat();
  String tz = server.arg("tz_info");

  if (flow <= 0.0f || flow > 100.0f) return false;
  if (reportMs < 1000 || reportMs > 3600000) return false;
  if (csh < 0 || csh > 23) return false;
  if (csm < 0 || csm > 59) return false;
  if (ceh < 0 || ceh > 23) return false;
  if (cem < 0 || cem > 59) return false;
  if (ppl <= 1.0f || ppl > 10000.0f) return false;
  if (tz.length() == 0 || tz.length() >= (int)sizeof(config.tzInfo)) return false;

  config.flowActiveLpm = flow;
  config.reportIntervalMs = reportMs;
  config.closeStartHour = csh;
  config.closeStartMin = csm;
  config.closeEndHour = ceh;
  config.closeEndMin = cem;
  config.pulsesPerLiter = ppl;
  tz.toCharArray(config.tzInfo, sizeof(config.tzInfo));
  saveConfig();
  setenv("TZ", config.tzInfo, 1);
  tzset();
  return true;
}

static void handleRoot() {
  extern const char DASHBOARD_HTML[] PROGMEM;
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

static void handleStatus() {
  server.send(200, "application/json", buildStatusJson());
}

static void handleReport() {
  String out;
  out.reserve(4096);
  StringPrint printer(out);
  printReportTo(printer);
  server.send(200, "text/plain", out);
}

static void handleReportJson() {
  server.send(200, "application/json", buildReportJson());
}

static void handleConfigGet() {
  server.send(200, "application/json", buildConfigJson());
}

static void handleConfigPost() {
  if (!applyConfigFromArgs()) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleValve() {
  String action = server.arg("action");
  action.toLowerCase();
  if (action == "open") {
    if (timeValid) {
      struct tm tmNow;
      if (getLocalTimeSafe(tmNow) && isWithinClosedWindow(tmNow.tm_hour, tmNow.tm_min)) {
        server.send(403, "application/json", "{\"ok\":false,\"reason\":\"blocked\"}");
        return;
      }
    }
    openValve();
    server.send(200, "application/json", "{\"ok\":true,\"valve\":\"OPEN\"}");
    return;
  }
  if (action == "close") {
    closeValve();
    server.send(200, "application/json", "{\"ok\":true,\"valve\":\"CLOSED\"}");
    return;
  }
  server.send(400, "application/json", "{\"ok\":false}");
}

static void handleReset() {
  resetCounters();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/report", HTTP_GET, handleReport);
  server.on("/api/report.json", HTTP_GET, handleReportJson);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/valve", HTTP_POST, handleValve);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleWebServer() {
  server.handleClient();
}

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Water Control</title>
  <style>
    :root {
      --ink: #1a1f2b;
      --muted: #5b6476;
      --card: rgba(255,255,255,0.9);
      --accent: #1f6feb;
      --accent-2: #10b981;
      --bg1: #e7efe7;
      --bg2: #dfe7f3;
      --shadow: rgba(0,0,0,0.08);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Trebuchet MS", "Verdana", sans-serif;
      color: var(--ink);
      background: radial-gradient(1200px 600px at 10% 10%, #f7f4e9 0%, var(--bg1) 40%, var(--bg2) 100%);
    }
    .wrap {
      max-width: 980px;
      margin: 28px auto 48px;
      padding: 0 18px;
    }
    .title {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 16px;
      gap: 12px;
    }
    .title h1 {
      margin: 0;
      font-size: 28px;
      letter-spacing: 0.5px;
    }
    .title-actions {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .badge {
      font-size: 12px;
      padding: 6px 10px;
      border-radius: 999px;
      background: #111827;
      color: #fff;
    }
    .icon-btn {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 36px;
      height: 36px;
      border-radius: 10px;
      border: 1px solid #d6dbe6;
      background: #fff;
      cursor: pointer;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
      gap: 14px;
    }
    .card {
      background: var(--card);
      border-radius: 16px;
      padding: 16px 18px;
      box-shadow: 0 10px 30px var(--shadow);
      border: 1px solid rgba(255,255,255,0.7);
    }
    .card h2 {
      margin: 0 0 10px;
      font-size: 16px;
      text-transform: uppercase;
      letter-spacing: 1px;
      color: var(--muted);
    }
    .stat {
      font-size: 28px;
      font-weight: 600;
      margin: 4px 0;
    }
    .sub {
      color: var(--muted);
      font-size: 12px;
    }
    .btn {
      border: 0;
      padding: 10px 14px;
      border-radius: 10px;
      font-weight: 600;
      cursor: pointer;
      color: #fff;
      background: var(--accent);
      margin-right: 8px;
    }
    .btn.secondary {
      background: #374151;
    }
    .btn.good {
      background: var(--accent-2);
    }
    .row {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      align-items: center;
    }
    label {
      display: block;
      font-size: 12px;
      color: var(--muted);
      margin-bottom: 6px;
    }
    input {
      width: 100%;
      padding: 8px 10px;
      border-radius: 10px;
      border: 1px solid #d5d8e0;
      background: #fff;
    }
    .config-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 12px;
    }
    .report-grid {
      display: grid;
      gap: 12px;
    }
    .report-summary {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-weight: 600;
      font-size: 14px;
      padding: 10px 12px;
      border-radius: 12px;
      background: #0f172a;
      color: #e2e8f0;
    }
    .day-card {
      border-radius: 14px;
      padding: 12px;
      background: #ffffff;
      border: 1px solid #e6eaf2;
    }
    .day-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 8px;
      gap: 8px;
    }
    .day-title {
      font-weight: 700;
      font-size: 15px;
    }
    .day-total {
      color: #1f2937;
      font-weight: 600;
      font-size: 13px;
    }
    .interval-row {
      display: grid;
      grid-template-columns: 70px 70px 90px 1fr;
      gap: 8px;
      padding: 6px 0;
      border-bottom: 1px dashed #e5e7eb;
      font-size: 13px;
      color: #111827;
    }
    .interval-row:last-child {
      border-bottom: 0;
    }
    .interval-header {
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.8px;
      color: #6b7280;
      padding-bottom: 6px;
    }
    .config-msg {
      margin-top: 10px;
      font-size: 12px;
      color: #0f172a;
    }
    .footer {
      margin-top: 8px;
      font-size: 12px;
      color: var(--muted);
    }
    .config-panel {
      display: none;
      margin-top: 16px;
    }
    .config-panel.open {
      display: block;
    }
    .action-msg {
      margin-top: 8px;
      font-size: 12px;
      color: #1f2937;
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="title">
      <h1>Water Control Dashboard</h1>
      <div class="title-actions">
        <div class="badge" id="timeBadge">--:--:--</div>
        <button class="icon-btn" id="configToggle" aria-label="Toggle configuration">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path d="M12 8.8a3.2 3.2 0 1 0 0 6.4 3.2 3.2 0 0 0 0-6.4Zm9.2 3.2c0-.5-.1-1-.2-1.5l2.1-1.6-2-3.4-2.5 1a7.7 7.7 0 0 0-2.6-1.5l-.4-2.7H8.4l-.4 2.7c-1 .3-1.9.8-2.6 1.5l-2.5-1-2 3.4 2.1 1.6c-.1.5-.2 1-.2 1.5s.1 1 .2 1.5l-2.1 1.6 2 3.4 2.5-1c.7.7 1.6 1.2 2.6 1.5l.4 2.7h5.2l.4-2.7c1-.3 1.9-.8 2.6-1.5l2.5 1 2-3.4-2.1-1.6c.1-.5.2-1 .2-1.5Z" stroke="#111827" stroke-width="1.5" />
          </svg>
        </button>
      </div>
    </div>

    <div class="grid">
      <div class="card">
        <h2>Live Status</h2>
        <div class="stat" id="valveState">--</div>
        <div class="sub" id="dateLabel">--</div>
        <div class="row" style="margin: 12px 0;">
          <button class="btn good" onclick="sendValve('open')">Open</button>
          <button class="btn secondary" onclick="sendValve('close')">Close</button>
          <button class="btn" onclick="sendReset()">Reset</button>
        </div>
        <div class="sub">Flow: <span id="flowRate">--</span> L/min</div>
        <div class="sub">Today: <span id="dailyLiters">--</span> L</div>
        <div class="sub">Week: <span id="weekLiters">--</span> L</div>
        <div class="action-msg" id="actionMsg"></div>
      </div>

      <div class="card">
        <h2>Blocked Window</h2>
        <div class="stat" id="scheduleLabel">--</div>
        <div class="sub">Threshold: <span id="flowThreshold">--</span> L/min</div>
        <div class="sub">Report: <span id="reportInterval">--</span> ms</div>
      </div>
    </div>

    <div class="card" style="margin-top: 16px;">
      <h2>Report</h2>
      <div id="report" class="report-grid">Loading...</div>
      <div class="footer">Auto-refreshes every 10 seconds.</div>
    </div>

    <div class="card config-panel" id="configPanel">
      <h2>Configuration</h2>
      <form id="configForm">
        <div class="config-grid">
          <div>
            <label for="flow_active_lpm">Flow Threshold (L/min)</label>
            <input id="flow_active_lpm" name="flow_active_lpm" type="number" step="0.01" min="0" required>
          </div>
          <div>
            <label for="report_interval_ms">Report Interval (ms)</label>
            <input id="report_interval_ms" name="report_interval_ms" type="number" step="1000" min="1000" required>
          </div>
          <div>
            <label for="close_start_hour">Close Start Hour</label>
            <input id="close_start_hour" name="close_start_hour" type="number" min="0" max="23" required>
          </div>
          <div>
            <label for="close_start_min">Close Start Min</label>
            <input id="close_start_min" name="close_start_min" type="number" min="0" max="59" required>
          </div>
          <div>
            <label for="close_end_hour">Close End Hour</label>
            <input id="close_end_hour" name="close_end_hour" type="number" min="0" max="23" required>
          </div>
          <div>
            <label for="close_end_min">Close End Min</label>
            <input id="close_end_min" name="close_end_min" type="number" min="0" max="59" required>
          </div>
          <div>
            <label for="pulses_per_liter">Pulses Per Liter</label>
            <input id="pulses_per_liter" name="pulses_per_liter" type="number" step="0.1" min="1" required>
          </div>
          <div>
            <label for="tz_info">TZ Info</label>
            <input id="tz_info" name="tz_info" type="text" required>
          </div>
        </div>
        <div style="margin-top: 12px;">
          <button class="btn" type="submit">Save Configuration</button>
          <div id="configMsg" class="config-msg"></div>
        </div>
      </form>
    </div>
  </div>

  <script>
    async function fetchJson(url) {
      const res = await fetch(url, {cache: 'no-store'});
      return res.json();
    }
    async function loadStatus() {
      const s = await fetchJson('/api/status?t=' + Date.now());
      document.getElementById('valveState').textContent = s.valve || '--';
      document.getElementById('flowRate').textContent = s.flow_lpm ?? '--';
      document.getElementById('dailyLiters').textContent = s.daily_liters ?? '--';
      document.getElementById('weekLiters').textContent = s.week_liters ?? '--';
      document.getElementById('flowThreshold').textContent = s.flow_active_lpm ?? '--';
      document.getElementById('reportInterval').textContent = s.report_interval_ms ?? '--';
      document.getElementById('scheduleLabel').textContent = (s.close_start || '--') + ' -> ' + (s.close_end || '--');
      document.getElementById('timeBadge').textContent = s.time || '--:--:--';
      document.getElementById('dateLabel').textContent = s.date || '--';
    }
    function formatDuration(seconds) {
      const h = String(Math.floor(seconds / 3600)).padStart(2, '0');
      const m = String(Math.floor((seconds % 3600) / 60)).padStart(2, '0');
      const s = String(seconds % 60).padStart(2, '0');
      return `${h}:${m}:${s}`;
    }
    function renderReport(data) {
      const host = document.getElementById('report');
      if (!data || !Array.isArray(data.days)) {
        host.textContent = 'No report data yet.';
        return;
      }
      let html = '';
      html += `<div class="report-summary">` +
              `<div>Week Total</div>` +
              `<div>${formatDuration(data.week_total_sec || 0)} | ${Number(data.week_total_l || 0).toFixed(3)} L</div>` +
              `</div>`;
      data.days.forEach(day => {
        const totalDur = formatDuration(day.total_sec || 0);
        const totalL = Number(day.total_l || 0).toFixed(3);
        html += `<div class="day-card">` +
                `<div class="day-header">` +
                `<div class="day-title">${day.wday} ${day.date}</div>` +
                `<div class="day-total">${totalDur} | ${totalL} L</div>` +
                `</div>`;
        if (day.intervals && day.intervals.length) {
          html += `<div class="interval-row interval-header">` +
                  `<div>From</div><div>To</div><div>Dur</div><div>Liters</div>` +
                  `</div>`;
          day.intervals.forEach(it => {
            html += `<div class="interval-row">` +
                    `<div>${it.from}</div>` +
                    `<div>${it.to}</div>` +
                    `<div>${it.dur}</div>` +
                    `<div>${Number(it.liters || 0).toFixed(3)} L</div>` +
                    `</div>`;
          });
        } else {
          html += `<div class="sub">No intervals</div>`;
        }
        html += `</div>`;
      });
      host.innerHTML = html;
    }
    async function loadReport() {
      const res = await fetch('/api/report.json?t=' + Date.now(), {cache: 'no-store'});
      renderReport(await res.json());
    }
    async function loadConfig() {
      const c = await fetchJson('/api/config?t=' + Date.now());
      for (const key in c) {
        const el = document.getElementById(key);
        if (el) el.value = c[key];
      }
    }
    async function sendValve(action) {
      const msg = document.getElementById('actionMsg');
      msg.textContent = 'Sending...';
      const res = await fetch('/api/valve', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'action=' + encodeURIComponent(action)
      });
      let data = {};
      try { data = await res.json(); } catch (_) {}
      if (!res.ok && data.reason === 'blocked') {
        msg.textContent = 'Blocked by window.';
      } else if (!res.ok) {
        msg.textContent = 'Command failed.';
      } else {
        msg.textContent = 'Done.';
      }
      loadStatus();
    }
    async function sendReset() {
      await fetch('/api/reset', {method: 'POST'});
      loadStatus();
      loadReport();
    }
    document.getElementById('configForm').addEventListener('submit', async (e) => {
      e.preventDefault();
      const msg = document.getElementById('configMsg');
      msg.textContent = 'Saving...';
      const form = new FormData(e.target);
      const body = new URLSearchParams(form);
      const res = await fetch('/api/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      });
      if (res.ok) {
        msg.textContent = 'Saved.';
        await loadStatus();
        await loadReport();
      } else {
        msg.textContent = 'Save failed. Check values.';
      }
    });
    document.getElementById('configToggle').addEventListener('click', () => {
      document.getElementById('configPanel').classList.toggle('open');
    });
    loadStatus();
    loadReport();
    loadConfig();
    setInterval(loadStatus, 2000);
    setInterval(loadReport, 10000);
  </script>
</body>
</html>
)HTML";

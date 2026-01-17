#include "web_ui.h"

#include <WebServer.h>
#include <SPIFFS.h>
#include <time.h>

#include "app_state.h"
#include "config.h"
#include "report.h"
#include "storage.h"
#include "web_ui_html.h"

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
  json += ",\"min_interval_l\":";
  json += String(config.minIntervalLiters, 3);
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
  json += ",\"pulses_per_liter\":";
  json += String(config.pulsesPerLiter, 2);
  json += ",\"tz_info\":\"";
  json += config.tzInfo;
  json += "\"";
  json += "}";
  return json;
}

static bool parseTimeArg(const String &value, int &hour, int &minute) {
  int colon = value.indexOf(':');
  if (colon <= 0) {
    return false;
  }
  hour = value.substring(0, colon).toInt();
  minute = value.substring(colon + 1).toInt();
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return false;
  }
  return true;
}

static bool applyConfigFromArgs() {
  if (server.args() == 0) {
    return false;
  }

  float flow = server.arg("flow_active_lpm").toFloat();
  float minInterval = config.minIntervalLiters;
  if (server.hasArg("min_interval_l")) {
    minInterval = server.arg("min_interval_l").toFloat();
  }
  uint32_t reportMs = (uint32_t)server.arg("report_interval_ms").toInt();
  int csh = config.closeStartHour;
  int csm = config.closeStartMin;
  int ceh = config.closeEndHour;
  int cem = config.closeEndMin;
  if (server.hasArg("close_start")) {
    if (!parseTimeArg(server.arg("close_start"), csh, csm)) return false;
  } else {
    csh = server.arg("close_start_hour").toInt();
    csm = server.arg("close_start_min").toInt();
  }
  if (server.hasArg("close_end")) {
    if (!parseTimeArg(server.arg("close_end"), ceh, cem)) return false;
  } else {
    ceh = server.arg("close_end_hour").toInt();
    cem = server.arg("close_end_min").toInt();
  }
  float ppl = server.arg("pulses_per_liter").toFloat();
  String tz = server.arg("tz_info");

  if (flow <= 0.0f || flow > 100.0f) return false;
  if (minInterval < 0.0f || minInterval > 1000.0f) return false;
  if (reportMs < 1000 || reportMs > 3600000) return false;
  if (csh < 0 || csh > 23) return false;
  if (csm < 0 || csm > 59) return false;
  if (ceh < 0 || ceh > 23) return false;
  if (cem < 0 || cem > 59) return false;
  if (ppl <= 1.0f || ppl > 10000.0f) return false;
  if (tz.length() == 0 || tz.length() >= (int)sizeof(config.tzInfo)) return false;

  config.flowActiveLpm = flow;
  config.minIntervalLiters = minInterval;
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

static void streamCsvFile(const char *path) {
  if (!storageReady()) {
    server.send(503, "text/plain", "Storage not ready.");
    return;
  }
  File file = SPIFFS.open(path, "r");
  if (!file) {
    server.send(404, "text/plain", "CSV not found.");
    return;
  }
  server.streamFile(file, "text/csv");
  file.close();
}

static void handleConfigCsv() {
  streamCsvFile(CONFIG_CSV_PATH);
}

static void handleUsageCsv() {
  streamCsvFile(USAGE_CSV_PATH);
}

static void handleIntervalsCsv() {
  streamCsvFile(INTERVALS_CSV_PATH);
}

static void handleSummaryJson() {
  String period = server.arg("period");
  period.toLowerCase();
  if (period != "week" && period != "month") {
    period = "week";
  }
  int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 12;
  server.send(200, "application/json", buildSummaryJson(period, limit));
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
    manualOverrideOpen();
    server.send(200, "application/json", "{\"ok\":true,\"valve\":\"OPEN\"}");
    return;
  }
  if (action == "close") {
    manualOverrideClose();
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
  server.on("/api/summary.json", HTTP_GET, handleSummaryJson);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/config.csv", HTTP_GET, handleConfigCsv);
  server.on("/api/usage.csv", HTTP_GET, handleUsageCsv);
  server.on("/api/intervals.csv", HTTP_GET, handleIntervalsCsv);
  server.on("/api/valve", HTTP_POST, handleValve);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleWebServer() {
  server.handleClient();
}

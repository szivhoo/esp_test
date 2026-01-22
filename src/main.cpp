#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "app_state.h"
#include "config.h"
#include "report.h"
#include "storage.h"
#include "web_ui.h"
#include "secrets.h"

#define FLOW_SENSOR_PIN 22
#define VALVE_PIN 23

volatile uint32_t pulseCount = 0;
volatile uint32_t lastPulseMicros = 0;

// Reject pulses that are "too close" (noise/ringing). Start at 300us.
static const uint32_t MIN_PULSE_US = 300;

uint32_t lastCalcMs = 0;
uint32_t lastReportMs = 0;
float flowRateLpm = 0.0f;
float totalLiters = 0.0f;
float dailyLiters = 0.0f;
bool valveState = false;

static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.nist.gov";

DayUsage weekUsage[7];
int weekIndex = 6;
int currentYear = -1;
int currentYday = -1;
bool timeValid = false;
bool flowActive = false;
int activeIntervalIndex = -1;
bool skipPersistOnNextRollover = false;
bool manualOverride = false;
bool manualOverrideStartInClosed = false;

int lastLoadedYear = 0;
int lastLoadedMonth = 0;
int lastLoadedDay = 0;

Config config;

void IRAM_ATTR pulseCounter() {
  uint32_t now = micros();
  if ((uint32_t)(now - lastPulseMicros) >= MIN_PULSE_US) {
    pulseCount++;
    lastPulseMicros = now;
  }
}

void openValve() {
  digitalWrite(VALVE_PIN, LOW);
  valveState = true;
  Serial.println(">>> VALVE OPENED <<<");
}

void closeValve() {
  digitalWrite(VALVE_PIN, HIGH);
  valveState = false;
  Serial.println(">>> VALVE CLOSED <<<");
}

static void setManualOverride(bool openState) {
  manualOverride = true;
  manualOverrideStartInClosed = false;
  if (timeValid) {
    struct tm tmNow;
    if (getLocalTimeSafe(tmNow)) {
      manualOverrideStartInClosed = isWithinClosedWindow(tmNow.tm_hour, tmNow.tm_min);
    }
  }
  if (openState) {
    openValve();
  } else {
    closeValve();
  }
}

void manualOverrideOpen() {
  setManualOverride(true);
}

void manualOverrideClose() {
  setManualOverride(false);
}

void resetCounters() {
  noInterrupts();
  pulseCount = 0;
  interrupts();
  totalLiters = 0.0f;
  flowRateLpm = 0.0f;
  Serial.println("* Counters RESET *");
}

void printStatus() {
  Serial.println("\n=== SYSTEM STATUS ===");
  Serial.print("Valve: ");
  Serial.println(valveState ? "OPEN" : "CLOSED");
  Serial.print("Flow Rate: ");
  Serial.print(flowRateLpm, 2);
  Serial.println(" L/min");
  Serial.print("Total Volume: ");
  Serial.print(totalLiters, 3);
  Serial.println(" L");
  Serial.println("====================\n");
}

void resetDayUsage(int idx, int year, int month, int day, int wday) {
  weekUsage[idx].year = year;
  weekUsage[idx].month = month;
  weekUsage[idx].day = day;
  weekUsage[idx].wday = wday;
  weekUsage[idx].totalSeconds = 0;
  weekUsage[idx].totalLiters = 0.0f;
  weekUsage[idx].intervalCount = 0;
  for (int i = 0; i < MAX_INTERVALS; i++) {
    weekUsage[idx].intervals[i].startSec = 0;
    weekUsage[idx].intervals[i].endSec = 0;
    weekUsage[idx].intervals[i].liters = 0.0f;
  }
}

bool isTimeSane() {
  time_t now = time(nullptr);
  return now >= 1609459200;
}

static bool isWithinWindow(int startMin, int endMin, int nowMin) {
  if (startMin == endMin) {
    return false;
  }
  if (startMin < endMin) {
    return (nowMin >= startMin) && (nowMin < endMin);
  }
  return (nowMin >= startMin) || (nowMin < endMin);
}

bool isWithinClosedWindow(int hour, int minute) {
  int nowMin = (hour * 60) + minute;
  for (int i = 0; i < BLOCKED_WINDOW_COUNT; i++) {
    int startMin = (config.closeStartHour[i] * 60) + config.closeStartMin[i];
    int endMin = (config.closeEndHour[i] * 60) + config.closeEndMin[i];
    if (isWithinWindow(startMin, endMin, nowMin)) {
      return true;
    }
  }
  return false;
}

void startInterval(DayUsage &day, int secOfDay) {
  if (day.intervalCount >= MAX_INTERVALS) {
    activeIntervalIndex = -1;
    return;
  }
  activeIntervalIndex = day.intervalCount;
  day.intervals[activeIntervalIndex].startSec = secOfDay;
  day.intervals[activeIntervalIndex].endSec = secOfDay;
  day.intervals[activeIntervalIndex].liters = 0.0f;
  day.intervalCount++;
}

void updateIntervalEnd(DayUsage &day, int secOfDay) {
  if (activeIntervalIndex < 0) {
    return;
  }
  day.intervals[activeIntervalIndex].endSec = secOfDay;
}

void closeInterval(DayUsage &day, int secOfDay) {
  if (activeIntervalIndex >= 0) {
    day.intervals[activeIntervalIndex].endSec = secOfDay;
  }
  activeIntervalIndex = -1;
}

void ensureDaySlot(struct tm &tmNow) {
  // Roll daily buckets and keep intervals contiguous across midnight.
  if (tmNow.tm_year == currentYear && tmNow.tm_yday == currentYday) {
    return;
  }

  int prevIndex = weekIndex;
  if (flowActive && prevIndex >= 0) {
    closeInterval(weekUsage[prevIndex], 86399);
  }
  if (prevIndex >= 0 && weekUsage[prevIndex].year >= 0) {
    if (!skipPersistOnNextRollover) {
      appendDayUsageCsv(weekUsage[prevIndex]);
    } else {
      skipPersistOnNextRollover = false;
    }
  }

  weekIndex = (weekIndex + 1) % 7;
  resetDayUsage(weekIndex, tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_wday);
  currentYear = tmNow.tm_year;
  currentYday = tmNow.tm_yday;
  dailyLiters = 0.0f;

  if (flowActive) {
    startInterval(weekUsage[weekIndex], 0);
  }
}

bool getLocalTimeSafe(struct tm &tmNow) {
  if (!timeValid) {
    return false;
  }
  time_t now = time(nullptr);
  localtime_r(&now, &tmNow);
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 15000) {
    delay(250);
  }
}

void syncTime() {
  setenv("TZ", config.tzInfo, 1);
  tzset();
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);

  uint32_t startMs = millis();
  while (!isTimeSane() && (millis() - startMs) < 15000) {
    delay(250);
  }
  timeValid = isTimeSane();
}

void setup() {
  Serial.begin(115200);

  pinMode(FLOW_SENSOR_PIN, INPUT);     // if you have external pull-up, INPUT is fine
  pinMode(VALVE_PIN, OUTPUT);
  digitalWrite(VALVE_PIN, HIGH);

  // Use ONE edge consistently
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);

  lastCalcMs = millis();
  lastReportMs = millis();

  for (int i = 0; i < 7; i++) {
    resetDayUsage(i, -1, -1, -1, -1);
  }

  initStorage();
  loadConfig();
  bool usageLoaded = loadUsageFromCsv(lastLoadedYear, lastLoadedMonth, lastLoadedDay);
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    syncTime();
    setupServer();
    Serial.print("Web UI: http://");
    Serial.println(WiFi.localIP());
  }

  if (timeValid && usageLoaded) {
    struct tm tmLast = {};
    tmLast.tm_year = lastLoadedYear - 1900;
    tmLast.tm_mon = lastLoadedMonth - 1;
    tmLast.tm_mday = lastLoadedDay;
    tmLast.tm_hour = 12;
    tmLast.tm_isdst = -1;
    time_t lastTs = mktime(&tmLast);
    localtime_r(&lastTs, &tmLast);
    currentYear = tmLast.tm_year;
    currentYday = tmLast.tm_yday;
    skipPersistOnNextRollover = true;
  }

  if (timeValid) {
    struct tm tmNow;
    time_t now = time(nullptr);
    localtime_r(&now, &tmNow);
    if (isWithinClosedWindow(tmNow.tm_hour, tmNow.tm_min)) {
      closeValve();
    } else {
      openValve();
    }
  } else {
    openValve();
  }

  Serial.println("=================================");
  Serial.println("ESP32 Water Flow Control System");
  Serial.println("Commands: OP, CL, RS, ST");
  Serial.println("=================================");
}

void loop() {
  // Calculate once per second
  uint32_t nowMs = millis();
  if (nowMs - lastCalcMs >= 1000) {
    uint32_t pulses;

    noInterrupts();
    pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    // pulses/sec over a 1s window
    float pulsesPerSec = (float)pulses;

    // L/min = (pulses/sec) * (60 sec/min) / (pulses/L)
    flowRateLpm = (pulsesPerSec * 60.0f) / config.pulsesPerLiter;

    lastCalcMs = nowMs;

    if (timeValid) {
      struct tm tmNow;
      time_t now = time(nullptr);
      localtime_r(&now, &tmNow);

      ensureDaySlot(tmNow);

      int secOfDay = (tmNow.tm_hour * 3600) + (tmNow.tm_min * 60) + tmNow.tm_sec;
      bool isActive = flowRateLpm > config.flowActiveLpm;
      if (isActive && !flowActive) {
        startInterval(weekUsage[weekIndex], secOfDay);
      } else if (!isActive && flowActive) {
        closeInterval(weekUsage[weekIndex], secOfDay);
      }

      if (isActive) {
        float litersThisSecond = flowRateLpm / 60.0f;
        weekUsage[weekIndex].totalSeconds++;
        weekUsage[weekIndex].totalLiters += litersThisSecond;
        totalLiters += litersThisSecond;
        dailyLiters += litersThisSecond;
        if (activeIntervalIndex >= 0) {
          weekUsage[weekIndex].intervals[activeIntervalIndex].liters += litersThisSecond;
          updateIntervalEnd(weekUsage[weekIndex], secOfDay);
        }
      }
      flowActive = isActive;

      bool inClosedWindow = isWithinClosedWindow(tmNow.tm_hour, tmNow.tm_min);
      if (manualOverride) {
        if (inClosedWindow != manualOverrideStartInClosed) {
          manualOverride = false;
          if (inClosedWindow && valveState) {
            closeValve();
          } else if (!inClosedWindow && !valveState) {
            openValve();
          }
        }
      } else {
        if (inClosedWindow && valveState) {
          closeValve();
        } else if (!inClosedWindow && !valveState) {
          openValve();
        }
      }
    }
  }

  if (timeValid && (nowMs - lastReportMs) >= config.reportIntervalMs) {
    printReportTo(Serial);
    lastReportMs = nowMs;
  }

  // Serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "OP") {
      manualOverrideOpen();
    }
    else if (cmd == "CL") manualOverrideClose();
    else if (cmd == "RS") resetCounters();
    else if (cmd == "ST") printReportTo(Serial);
    else Serial.println("Unknown command. Use: OP, CL, RS, ST");
  }

  if (WiFi.status() == WL_CONNECTED) {
    handleWebServer();
  }
}

#include "config.h"

#include <Preferences.h>
#include <stdio.h>

#include "app_state.h"
#include "storage.h"

static const float DEFAULT_PULSES_PER_LITER = 450.0f;
static const float DEFAULT_FLOW_ACTIVE_LPM = 0.1f;
static const float DEFAULT_MIN_INTERVAL_LITERS = 0.1f;
static const uint32_t DEFAULT_REPORT_INTERVAL_MS = 10000;
static const int DEFAULT_CLOSE_START_HOUR[BLOCKED_WINDOW_COUNT] = {19, 0, 0};
static const int DEFAULT_CLOSE_START_MIN[BLOCKED_WINDOW_COUNT] = {24, 0, 0};
static const int DEFAULT_CLOSE_END_HOUR[BLOCKED_WINDOW_COUNT] = {6, 0, 0};
static const int DEFAULT_CLOSE_END_MIN[BLOCKED_WINDOW_COUNT] = {0, 0, 0};
static const char *DEFAULT_TZ_INFO = "UTC0";

static Preferences prefs;

void loadConfig() {
  bool loaded = false;
  if (storageReady()) {
    loaded = loadConfigCsv();
  }

  if (!loaded) {
    prefs.begin("watercfg", true);
    config.flowActiveLpm = prefs.getFloat("flow_lpm", DEFAULT_FLOW_ACTIVE_LPM);
    config.minIntervalLiters = prefs.getFloat("min_int_l", DEFAULT_MIN_INTERVAL_LITERS);
    config.reportIntervalMs = prefs.getUInt("report_ms", DEFAULT_REPORT_INTERVAL_MS);
    for (int i = 0; i < BLOCKED_WINDOW_COUNT; i++) {
      if (i == 0) {
        config.closeStartHour[i] = prefs.getInt("csh", DEFAULT_CLOSE_START_HOUR[i]);
        config.closeStartMin[i] = prefs.getInt("csm", DEFAULT_CLOSE_START_MIN[i]);
        config.closeEndHour[i] = prefs.getInt("ceh", DEFAULT_CLOSE_END_HOUR[i]);
        config.closeEndMin[i] = prefs.getInt("cem", DEFAULT_CLOSE_END_MIN[i]);
      } else {
        char key[8];
        snprintf(key, sizeof(key), "csh%d", i + 1);
        config.closeStartHour[i] = prefs.getInt(key, DEFAULT_CLOSE_START_HOUR[i]);
        snprintf(key, sizeof(key), "csm%d", i + 1);
        config.closeStartMin[i] = prefs.getInt(key, DEFAULT_CLOSE_START_MIN[i]);
        snprintf(key, sizeof(key), "ceh%d", i + 1);
        config.closeEndHour[i] = prefs.getInt(key, DEFAULT_CLOSE_END_HOUR[i]);
        snprintf(key, sizeof(key), "cem%d", i + 1);
        config.closeEndMin[i] = prefs.getInt(key, DEFAULT_CLOSE_END_MIN[i]);
      }
    }
    config.pulsesPerLiter = prefs.getFloat("ppl", DEFAULT_PULSES_PER_LITER);
    String tz = prefs.getString("tz", DEFAULT_TZ_INFO);
    tz.toCharArray(config.tzInfo, sizeof(config.tzInfo));
    prefs.end();
    if (storageReady()) {
      saveConfigCsv();
    }
  }
}

void saveConfig() {
  prefs.begin("watercfg", false);
  prefs.putFloat("flow_lpm", config.flowActiveLpm);
  prefs.putFloat("min_int_l", config.minIntervalLiters);
  prefs.putUInt("report_ms", config.reportIntervalMs);
  for (int i = 0; i < BLOCKED_WINDOW_COUNT; i++) {
    if (i == 0) {
      prefs.putInt("csh", config.closeStartHour[i]);
      prefs.putInt("csm", config.closeStartMin[i]);
      prefs.putInt("ceh", config.closeEndHour[i]);
      prefs.putInt("cem", config.closeEndMin[i]);
    } else {
      char key[8];
      snprintf(key, sizeof(key), "csh%d", i + 1);
      prefs.putInt(key, config.closeStartHour[i]);
      snprintf(key, sizeof(key), "csm%d", i + 1);
      prefs.putInt(key, config.closeStartMin[i]);
      snprintf(key, sizeof(key), "ceh%d", i + 1);
      prefs.putInt(key, config.closeEndHour[i]);
      snprintf(key, sizeof(key), "cem%d", i + 1);
      prefs.putInt(key, config.closeEndMin[i]);
    }
  }
  prefs.putFloat("ppl", config.pulsesPerLiter);
  prefs.putString("tz", config.tzInfo);
  prefs.end();
  if (storageReady()) {
    saveConfigCsv();
  }
}

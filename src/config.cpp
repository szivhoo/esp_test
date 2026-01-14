#include "config.h"

#include <Preferences.h>

#include "app_state.h"

static const float DEFAULT_PULSES_PER_LITER = 450.0f;
static const float DEFAULT_FLOW_ACTIVE_LPM = 0.1f;
static const float DEFAULT_MIN_INTERVAL_LITERS = 0.1f;
static const uint32_t DEFAULT_REPORT_INTERVAL_MS = 10000;
static const int DEFAULT_CLOSE_START_HOUR = 19;
static const int DEFAULT_CLOSE_START_MIN = 24;
static const int DEFAULT_CLOSE_END_HOUR = 6;
static const int DEFAULT_CLOSE_END_MIN = 0;
static const char *DEFAULT_TZ_INFO = "UTC0";

static Preferences prefs;

void loadConfig() {
  prefs.begin("watercfg", true);
  config.flowActiveLpm = prefs.getFloat("flow_lpm", DEFAULT_FLOW_ACTIVE_LPM);
  config.minIntervalLiters = prefs.getFloat("min_int_l", DEFAULT_MIN_INTERVAL_LITERS);
  config.reportIntervalMs = prefs.getUInt("report_ms", DEFAULT_REPORT_INTERVAL_MS);
  config.closeStartHour = prefs.getInt("csh", DEFAULT_CLOSE_START_HOUR);
  config.closeStartMin = prefs.getInt("csm", DEFAULT_CLOSE_START_MIN);
  config.closeEndHour = prefs.getInt("ceh", DEFAULT_CLOSE_END_HOUR);
  config.closeEndMin = prefs.getInt("cem", DEFAULT_CLOSE_END_MIN);
  config.pulsesPerLiter = prefs.getFloat("ppl", DEFAULT_PULSES_PER_LITER);
  String tz = prefs.getString("tz", DEFAULT_TZ_INFO);
  tz.toCharArray(config.tzInfo, sizeof(config.tzInfo));
  prefs.end();
}

void saveConfig() {
  prefs.begin("watercfg", false);
  prefs.putFloat("flow_lpm", config.flowActiveLpm);
  prefs.putFloat("min_int_l", config.minIntervalLiters);
  prefs.putUInt("report_ms", config.reportIntervalMs);
  prefs.putInt("csh", config.closeStartHour);
  prefs.putInt("csm", config.closeStartMin);
  prefs.putInt("ceh", config.closeEndHour);
  prefs.putInt("cem", config.closeEndMin);
  prefs.putFloat("ppl", config.pulsesPerLiter);
  prefs.putString("tz", config.tzInfo);
  prefs.end();
}

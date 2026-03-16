#pragma once

#include <Arduino.h>
#include <time.h>

// Maximum number of flow intervals stored per day; extra intervals are dropped.
// This caps memory usage but can make day totals exceed the sum of displayed rows.
constexpr int MAX_INTERVALS = 48;
constexpr int BLOCKED_WINDOW_COUNT = 3;

struct DayInterval {
  uint32_t startSec;
  uint32_t endSec;
  float liters;
};

struct DayUsage {
  int year;
  int month;
  int day;
  int wday;
  uint32_t totalSeconds;
  float totalLiters;
  uint8_t intervalCount;
  DayInterval intervals[MAX_INTERVALS];
};

struct Config {
  float flowActiveLpm;
  // Minimum liters for an interval to be included in reports/JSON output.
  // Intervals below this are still counted in daily totals but are hidden.
  float minIntervalLiters;
  uint32_t reportIntervalMs;
  bool leakProtectionEnabled;
  float leakThresholdLiters;
  int closeStartHour[BLOCKED_WINDOW_COUNT];
  int closeStartMin[BLOCKED_WINDOW_COUNT];
  int closeEndHour[BLOCKED_WINDOW_COUNT];
  int closeEndMin[BLOCKED_WINDOW_COUNT];
  float pulsesPerLiter;
  char tzInfo[32];
};

extern Config config;
extern DayUsage weekUsage[7];
extern int weekIndex;
extern bool timeValid;
extern bool valveState;
extern float flowRateLpm;
extern float totalLiters;
extern float dailyLiters;
extern bool flowActive;
extern int activeIntervalIndex;
extern bool leakTripped;
extern float continuousLiters;
extern int currentYear;
extern int currentYday;
extern bool skipPersistOnNextRollover;

bool getLocalTimeSafe(struct tm &tmNow);
bool isWithinClosedWindow(int hour, int minute);
void openValve();
void closeValve();
void resetCounters();
void ensureDaySlot(struct tm &tmNow);
void startInterval(DayUsage &day, int secOfDay);
void updateIntervalEnd(DayUsage &day, int secOfDay);
void closeInterval(DayUsage &day, int secOfDay);
void manualOverrideOpen();
void manualOverrideClose();

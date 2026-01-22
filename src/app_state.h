#pragma once

#include <Arduino.h>
#include <time.h>

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
  float minIntervalLiters;
  uint32_t reportIntervalMs;
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

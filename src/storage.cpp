#include "storage.h"

#include <SPIFFS.h>
#include <string.h>
#include <time.h>

const char *CONFIG_CSV_PATH = "/config.csv";
const char *USAGE_CSV_PATH = "/usage.csv";
const char *INTERVALS_CSV_PATH = "/intervals.csv";

static bool storageReadyFlag = false;

static void initDayUsage(DayUsage &day, int year, int month, int dayNum, int wday) {
  day.year = year;
  day.month = month;
  day.day = dayNum;
  day.wday = wday;
  day.totalSeconds = 0;
  day.totalLiters = 0.0f;
  day.intervalCount = 0;
  for (int i = 0; i < MAX_INTERVALS; i++) {
    day.intervals[i].startSec = 0;
    day.intervals[i].endSec = 0;
    day.intervals[i].liters = 0.0f;
  }
}

bool initStorage() {
  if (storageReadyFlag) return true;
  storageReadyFlag = SPIFFS.begin(true);
  return storageReadyFlag;
}

bool storageReady() {
  return storageReadyFlag;
}

static bool parseConfigCsvLine(const char *line) {
  char buf[256];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *tokens[32];
  int count = 0;
  char *save = nullptr;
  char *tok = strtok_r(buf, ",", &save);
  while (tok && count < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
    tokens[count++] = tok;
    tok = strtok_r(nullptr, ",", &save);
  }

  const int expectedNew = 3 + (BLOCKED_WINDOW_COUNT * 4) + 2;
  if (count != 9 && count != expectedNew) return false;

  float flow = atof(tokens[0]);
  float minInterval = atof(tokens[1]);
  uint32_t reportMs = (uint32_t)strtoul(tokens[2], nullptr, 10);
  int startIndex = 3;
  float ppl = 0.0f;
  const char *tz = nullptr;

  if (count == 9) {
    ppl = atof(tokens[7]);
    tz = tokens[8];
  } else {
    ppl = atof(tokens[startIndex + (BLOCKED_WINDOW_COUNT * 4)]);
    tz = tokens[startIndex + (BLOCKED_WINDOW_COUNT * 4) + 1];
  }

  if (flow <= 0.0f || flow > 100.0f) return false;
  if (minInterval < 0.0f || minInterval > 1000.0f) return false;
  if (reportMs < 1000 || reportMs > 3600000) return false;
  if (ppl <= 1.0f || ppl > 10000.0f) return false;
  if (strlen(tz) == 0 || strlen(tz) >= sizeof(config.tzInfo)) return false;

  config.flowActiveLpm = flow;
  config.minIntervalLiters = minInterval;
  config.reportIntervalMs = reportMs;
  for (int i = 0; i < BLOCKED_WINDOW_COUNT; i++) {
    if (count == expectedNew || i == 0) {
      int idx = startIndex + (i * 4);
      int csh = atoi(tokens[idx]);
      int csm = atoi(tokens[idx + 1]);
      int ceh = atoi(tokens[idx + 2]);
      int cem = atoi(tokens[idx + 3]);
      if (csh < 0 || csh > 23) return false;
      if (csm < 0 || csm > 59) return false;
      if (ceh < 0 || ceh > 23) return false;
      if (cem < 0 || cem > 59) return false;
      config.closeStartHour[i] = csh;
      config.closeStartMin[i] = csm;
      config.closeEndHour[i] = ceh;
      config.closeEndMin[i] = cem;
    } else {
      config.closeStartHour[i] = 0;
      config.closeStartMin[i] = 0;
      config.closeEndHour[i] = 0;
      config.closeEndMin[i] = 0;
    }
  }
  config.pulsesPerLiter = ppl;
  strncpy(config.tzInfo, tz, sizeof(config.tzInfo) - 1);
  config.tzInfo[sizeof(config.tzInfo) - 1] = '\0';
  return true;
}

bool loadConfigCsv() {
  if (!storageReadyFlag) return false;
  File file = SPIFFS.open(CONFIG_CSV_PATH, "r");
  if (!file) return false;

  char line[256];
  bool loaded = false;
  while (file.available()) {
    size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[len] = '\0';
    if (len == 0) continue;
    if (strncmp(line, "flow_active_lpm", 15) == 0) {
      continue;
    }
    loaded = parseConfigCsvLine(line);
    if (loaded) break;
  }
  file.close();
  return loaded;
}

bool saveConfigCsv() {
  if (!storageReadyFlag) return false;
  File file = SPIFFS.open(CONFIG_CSV_PATH, "w");
  if (!file) return false;
  file.println("flow_active_lpm,min_interval_l,report_interval_ms,close1_start_hour,close1_start_min,close1_end_hour,close1_end_min,close2_start_hour,close2_start_min,close2_end_hour,close2_end_min,close3_start_hour,close3_start_min,close3_end_hour,close3_end_min,pulses_per_liter,tz_info");
  file.print(config.flowActiveLpm, 3);
  file.print(",");
  file.print(config.minIntervalLiters, 3);
  file.print(",");
  file.print(config.reportIntervalMs);
  for (int i = 0; i < BLOCKED_WINDOW_COUNT; i++) {
    file.print(",");
    file.print(config.closeStartHour[i]);
    file.print(",");
    file.print(config.closeStartMin[i]);
    file.print(",");
    file.print(config.closeEndHour[i]);
    file.print(",");
    file.print(config.closeEndMin[i]);
  }
  file.print(",");
  file.print(config.pulsesPerLiter, 2);
  file.print(",");
  file.println(config.tzInfo);
  file.close();
  return true;
}

static bool parseUsageLine(const char *line, int &year, int &month, int &dayNum,
                           int &wday, uint32_t &seconds, float &liters) {
  if (strncmp(line, "date", 4) == 0) return false;
  unsigned int secVal = 0;
  int matched = sscanf(line, "%4d-%2d-%2d,%d,%u,%f",
                       &year, &month, &dayNum, &wday, &secVal, &liters);
  if (matched != 6) return false;
  seconds = secVal;
  return true;
}

static bool parseIntervalLine(const char *line, int &year, int &month, int &dayNum,
                              int &wday, uint32_t &startSec, uint32_t &endSec, float &liters) {
  if (strncmp(line, "date", 4) == 0) return false;
  unsigned int startVal = 0;
  unsigned int endVal = 0;
  int matched = sscanf(line, "%4d-%2d-%2d,%d,%u,%u,%f",
                       &year, &month, &dayNum, &wday, &startVal, &endVal, &liters);
  if (matched != 7) return false;
  startSec = startVal;
  endSec = endVal;
  return true;
}

bool loadUsageFromCsv(int &lastYear, int &lastMonth, int &lastDay) {
  if (!storageReadyFlag) return false;
  File file = SPIFFS.open(USAGE_CSV_PATH, "r");
  if (!file) return false;

  DayUsage temp[7];
  int count = 0;

  char line[128];
  while (file.available()) {
    size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[len] = '\0';
    if (len == 0) continue;

    int year = 0;
    int month = 0;
    int dayNum = 0;
    int wday = 0;
    uint32_t seconds = 0;
    float liters = 0.0f;
    if (!parseUsageLine(line, year, month, dayNum, wday, seconds, liters)) {
      continue;
    }

    DayUsage day;
    initDayUsage(day, year, month, dayNum, wday);
    day.totalSeconds = seconds;
    day.totalLiters = liters;

    if (count < 7) {
      temp[count] = day;
      count++;
    } else {
      for (int i = 1; i < 7; i++) {
        temp[i - 1] = temp[i];
      }
      temp[6] = day;
      count = 7;
    }
  }
  file.close();

  if (count == 0) return false;

  for (int i = 0; i < 7; i++) {
    initDayUsage(weekUsage[i], -1, -1, -1, -1);
  }
  for (int i = 0; i < count; i++) {
    weekUsage[i] = temp[i];
  }
  weekIndex = count - 1;
  lastYear = temp[count - 1].year;
  lastMonth = temp[count - 1].month;
  lastDay = temp[count - 1].day;

  File intervals = SPIFFS.open(INTERVALS_CSV_PATH, "r");
  if (!intervals) return true;
  while (intervals.available()) {
    size_t len = intervals.readBytesUntil('\n', line, sizeof(line) - 1);
    line[len] = '\0';
    if (len == 0) continue;

    int year = 0;
    int month = 0;
    int dayNum = 0;
    int wday = 0;
    uint32_t startSec = 0;
    uint32_t endSec = 0;
    float liters = 0.0f;
    if (!parseIntervalLine(line, year, month, dayNum, wday, startSec, endSec, liters)) {
      continue;
    }
    for (int i = 0; i < count; i++) {
      if (weekUsage[i].year == year && weekUsage[i].month == month && weekUsage[i].day == dayNum) {
        if (weekUsage[i].intervalCount < MAX_INTERVALS) {
          uint8_t idx = weekUsage[i].intervalCount;
          weekUsage[i].intervals[idx].startSec = startSec;
          weekUsage[i].intervals[idx].endSec = endSec;
          weekUsage[i].intervals[idx].liters = liters;
          weekUsage[i].intervalCount++;
        }
        break;
      }
    }
  }
  intervals.close();
  return true;
}

bool appendDayUsageCsv(const DayUsage &day) {
  if (!storageReadyFlag) return false;
  if (day.year < 0) return false;

  File file = SPIFFS.open(USAGE_CSV_PATH, "a");
  if (!file) return false;
  if (file.size() == 0) {
    file.println("date,wday,total_seconds,total_liters");
  }
  file.printf("%04d-%02d-%02d,%d,%lu,%.3f\n",
              day.year, day.month, day.day, day.wday,
              (unsigned long)day.totalSeconds, day.totalLiters);
  file.close();

  File intervals = SPIFFS.open(INTERVALS_CSV_PATH, "a");
  if (!intervals) return false;
  if (intervals.size() == 0) {
    intervals.println("date,wday,start_sec,end_sec,liters");
  }
  for (int i = 0; i < day.intervalCount; i++) {
    const DayInterval &it = day.intervals[i];
    if (it.liters <= 0.0f) continue;
    intervals.printf("%04d-%02d-%02d,%d,%lu,%lu,%.3f\n",
                     day.year, day.month, day.day, day.wday,
                     (unsigned long)it.startSec, (unsigned long)it.endSec, it.liters);
  }
  intervals.close();
  return true;
}

struct SummaryEntry {
  int year;
  int key;
  uint32_t seconds;
  float liters;
};

String buildSummaryJson(const String &period, int limit) {
  String json;
  json.reserve(1024);
  if (!storageReadyFlag) {
    json = "{\"period\":\"";
    json += period;
    json += "\",\"items\":[]}";
    return json;
  }
  File file = SPIFFS.open(USAGE_CSV_PATH, "r");
  if (!file) {
    json = "{\"period\":\"";
    json += period;
    json += "\",\"items\":[]}";
    return json;
  }

  const bool byWeek = (period == "week");
  const int maxEntries = byWeek ? 104 : 60;
  if (limit < 1) limit = 1;
  if (limit > maxEntries) limit = maxEntries;

  SummaryEntry entries[104];
  int count = 0;

  char line[128];
  while (file.available()) {
    size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[len] = '\0';
    if (len == 0) continue;

    int year = 0;
    int month = 0;
    int dayNum = 0;
    int wday = 0;
    uint32_t seconds = 0;
    float liters = 0.0f;
    if (!parseUsageLine(line, year, month, dayNum, wday, seconds, liters)) {
      continue;
    }

    int key = month;
    if (byWeek) {
      struct tm tmVal = {};
      tmVal.tm_year = year - 1900;
      tmVal.tm_mon = month - 1;
      tmVal.tm_mday = dayNum;
      tmVal.tm_hour = 12;
      tmVal.tm_isdst = -1;
      time_t ts = mktime(&tmVal);
      localtime_r(&ts, &tmVal);
      key = (tmVal.tm_yday / 7) + 1;
    }

    if (count > 0 && entries[count - 1].year == year && entries[count - 1].key == key) {
      entries[count - 1].seconds += seconds;
      entries[count - 1].liters += liters;
      continue;
    }

    SummaryEntry entry = {year, key, seconds, liters};
    if (count < limit) {
      entries[count++] = entry;
    } else {
      for (int i = 1; i < limit; i++) {
        entries[i - 1] = entries[i];
      }
      entries[limit - 1] = entry;
      count = limit;
    }
  }
  file.close();

  json = "{\"period\":\"";
  json += period;
  json += "\",\"items\":[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    char label[16];
    if (byWeek) {
      snprintf(label, sizeof(label), "%04d-W%02d", entries[i].year, entries[i].key);
    } else {
      snprintf(label, sizeof(label), "%04d-%02d", entries[i].year, entries[i].key);
    }
    json += "{";
    json += "\"label\":\"";
    json += label;
    json += "\",\"total_sec\":";
    json += String(entries[i].seconds);
    json += ",\"total_l\":";
    json += String(entries[i].liters, 3);
    json += "}";
  }
  json += "]}";
  return json;
}

#include "report.h"

#include "app_state.h"

static void printTimeHM(Print &out, uint32_t secOfDay) {
  uint32_t hour = secOfDay / 3600;
  uint32_t minute = (secOfDay % 3600) / 60;
  if (hour < 10) out.print("0");
  out.print(hour);
  out.print(":");
  if (minute < 10) out.print("0");
  out.print(minute);
}

static void printDuration(Print &out, uint32_t seconds) {
  uint32_t hour = seconds / 3600;
  uint32_t minute = (seconds % 3600) / 60;
  uint32_t sec = seconds % 60;
  if (hour < 10) out.print("0");
  out.print(hour);
  out.print(":");
  if (minute < 10) out.print("0");
  out.print(minute);
  out.print(":");
  if (sec < 10) out.print("0");
  out.print(sec);
}

static void printPadding(Print &out, int count) {
  for (int i = 0; i < count; i++) {
    out.print(" ");
  }
}

static void printFloatFixed(Print &out, float value, int width, int decimals) {
  char buf[24];
  dtostrf(value, width, decimals, buf);
  out.print(buf);
}

void computeWeekTotals(uint32_t &seconds, float &liters) {
  seconds = 0;
  liters = 0.0f;
  for (int i = 0; i < 7; i++) {
    if (weekUsage[i].year >= 0) {
      seconds += weekUsage[i].totalSeconds;
      liters += weekUsage[i].totalLiters;
    }
  }
}

void printReportTo(Print &out) {
  // Print a table of all intervals, plus daily and weekly totals.
  if (!timeValid) {
    out.println("Time not synced. Report unavailable.");
    return;
  }

  static const char *DAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  uint32_t weekTotal = 0;
  float weekLiters = 0.0f;
  computeWeekTotals(weekTotal, weekLiters);

  out.println("==================================================");
  out.print("WEEK TOTAL  ");
  printDuration(out, weekTotal);
  out.print(" | ");
  printFloatFixed(out, weekLiters, 7, 3);
  out.println(" L");
  out.println("--------------------------------------------------");

  for (int i = 6; i >= 0; i--) {
    int idx = (weekIndex - i + 7) % 7;
    if (weekUsage[idx].year < 0) {
      continue;
    }

    out.print("[");
    out.print(DAY_NAMES[weekUsage[idx].wday]);
    out.print("] ");
    out.print(weekUsage[idx].year);
    out.print("-");
    if (weekUsage[idx].month < 10) out.print("0");
    out.print(weekUsage[idx].month);
    out.print("-");
    if (weekUsage[idx].day < 10) out.print("0");
    out.print(weekUsage[idx].day);
    out.print("  |  Total ");
    printDuration(out, weekUsage[idx].totalSeconds);
    out.print(" | ");
    printFloatFixed(out, weekUsage[idx].totalLiters, 7, 3);
    out.println(" L");

    out.println("  FROM   TO     DUR       L");

    for (int j = 0; j < weekUsage[idx].intervalCount; j++) {
      uint32_t startSec = weekUsage[idx].intervals[j].startSec;
      uint32_t endSec = weekUsage[idx].intervals[j].endSec;
      uint32_t duration = (endSec >= startSec) ? (endSec - startSec) : 0;
      out.print("  ");
      printTimeHM(out, startSec);
      printPadding(out, 2);
      printTimeHM(out, endSec);
      printPadding(out, 2);
      printDuration(out, duration);
      printPadding(out, 2);
      printFloatFixed(out, weekUsage[idx].intervals[j].liters, 7, 3);
      out.println();
    }
    out.println("--------------------------------------------------");
  }
}

static void formatTimeHM(char *buf, size_t size, uint32_t secOfDay) {
  uint32_t hour = secOfDay / 3600;
  uint32_t minute = (secOfDay % 3600) / 60;
  snprintf(buf, size, "%02lu:%02lu", (unsigned long)hour, (unsigned long)minute);
}

static void formatDuration(char *buf, size_t size, uint32_t seconds) {
  uint32_t hour = seconds / 3600;
  uint32_t minute = (seconds % 3600) / 60;
  uint32_t sec = seconds % 60;
  snprintf(buf, size, "%02lu:%02lu:%02lu",
           (unsigned long)hour, (unsigned long)minute, (unsigned long)sec);
}

String buildReportJson() {
  String json;
  json.reserve(4096);

  uint32_t weekSeconds = 0;
  float weekLiters = 0.0f;
  computeWeekTotals(weekSeconds, weekLiters);

  json += "{";
  json += "\"week_total_sec\":";
  json += String(weekSeconds);
  json += ",\"week_total_l\":";
  json += String(weekLiters, 3);
  json += ",\"days\":[";

  static const char *DAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  bool firstDay = true;
  for (int i = 6; i >= 0; i--) {
    int idx = (weekIndex - i + 7) % 7;
    if (weekUsage[idx].year < 0) {
      continue;
    }
    if (!firstDay) {
      json += ",";
    }
    firstDay = false;

    json += "{";
    json += "\"wday\":\"";
    json += DAY_NAMES[weekUsage[idx].wday];
    json += "\",\"date\":\"";
    json += String(weekUsage[idx].year);
    json += "-";
    if (weekUsage[idx].month < 10) json += "0";
    json += String(weekUsage[idx].month);
    json += "-";
    if (weekUsage[idx].day < 10) json += "0";
    json += String(weekUsage[idx].day);
    json += "\",\"total_sec\":";
    json += String(weekUsage[idx].totalSeconds);
    json += ",\"total_l\":";
    json += String(weekUsage[idx].totalLiters, 3);
    json += ",\"intervals\":[";

    for (int j = 0; j < weekUsage[idx].intervalCount; j++) {
      if (j > 0) json += ",";
      uint32_t startSec = weekUsage[idx].intervals[j].startSec;
      uint32_t endSec = weekUsage[idx].intervals[j].endSec;
      uint32_t duration = (endSec >= startSec) ? (endSec - startSec) : 0;
      char fromBuf[8];
      char toBuf[8];
      char durBuf[12];
      formatTimeHM(fromBuf, sizeof(fromBuf), startSec);
      formatTimeHM(toBuf, sizeof(toBuf), endSec);
      formatDuration(durBuf, sizeof(durBuf), duration);
      json += "{";
      json += "\"from\":\"";
      json += fromBuf;
      json += "\",\"to\":\"";
      json += toBuf;
      json += "\",\"dur\":\"";
      json += durBuf;
      json += "\",\"liters\":";
      json += String(weekUsage[idx].intervals[j].liters, 3);
      json += "}";
    }
    json += "]}";
  }

  json += "]}";
  return json;
}

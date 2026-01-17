#pragma once

#include <Arduino.h>

#include "app_state.h"

bool initStorage();
bool storageReady();

bool loadConfigCsv();
bool saveConfigCsv();

bool loadUsageFromCsv(int &lastYear, int &lastMonth, int &lastDay);
bool appendDayUsageCsv(const DayUsage &day);

String buildSummaryJson(const String &period, int limit);

extern const char *CONFIG_CSV_PATH;
extern const char *USAGE_CSV_PATH;
extern const char *INTERVALS_CSV_PATH;

#pragma once

#include <Arduino.h>

void printReportTo(Print &out);
void computeWeekTotals(uint32_t &seconds, float &liters);
String buildReportJson();

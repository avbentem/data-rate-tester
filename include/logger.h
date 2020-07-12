#ifndef DATA_RATE_TESTER_LOGGER_H
#define DATA_RATE_TESTER_LOGGER_H

#include <cstdarg>
#include "Arduino.h"
#include "lmic.h"

class Logger {

public:
  Logger();
  static void log(const char *text);
  static void logf(const char *format, ...);
  static void println(const char *text);
  static void println();
  static void printf(const char *format, ...);
};

__unused extern Logger logger;

#endif // DATA_RATE_TESTER_LOGGER_H

#include <sys/cdefs.h>
/**
 * A logger that, for its `log` and `logf` methods, prefixes each log entry with the LMIC ticks and
 * the number of seconds since boot.
 */
#include "logger.h"

// Global singleton instance to invoke the constructor; not used directly as all methods are static
__unused Logger logger; // NOLINT(cert-err58-cpp)

Logger::Logger() {
  Serial.begin(115200);
  while (!Serial)
    ;
}

/**
 * Log a single line with the given text, prefixed with the current timestamp and core number.
 */
void Logger::log(const char *text) {
  // Of course, one could also invoke Logger::logf with just a single parameter directly
  Logger::logf(text);
}

/**
 * Log a single line using a printf-like format and one or more token values, prefixed with the
 * current timestamp and core number.
 */
void Logger::logf(const char *format, ...) {
  char formatted[200] = {0};
  va_list args;
  va_start(args, format);
  vsnprintf(formatted, 200, format, args);
  va_end(args);

  // For `framework = arduino` logging should probably use the built-in `log_i` (or even `ESP_LOGI`,
  // though that is is actually delegated to the first for which its required `TAG` is lost; see
  // https://github.com/espressif/arduino-esp32/blob/1.0.4/cores/esp32/esp32-hal-log.h#L142-L146),
  // along with some `-D CORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_INFO`. However, a semicolon after those
  // macros will make Clang-Tidy complain about an empty statement.
  unsigned long ms = millis();
  // NOLINTNEXTLINE(cppcoreguidelines-narrowing-conversions)
  Logger::printf("[%d/%lums/%.1fs][%d] %s\n", os_getTime(), ms, ms / 1000.0, xPortGetCoreID(),
                 formatted);
}

void Logger::printf(const char *format, ...) {
  char formatted[200] = {0};
  va_list args;
  va_start(args, format);
  vsnprintf(formatted, 200, format, args);
  va_end(args);
  Serial.print(formatted);
}

void Logger::println() {
  Serial.println();
}

void Logger::println(const char *text) {
  Serial.println(text);
}

#pragma once

#include <cstring>
#include <iostream>

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/**
 * The usual practice is to retain it in the Debug version,
 * In the Release version, ASSERT is directly converted to empty code using a macro definition:
 #ifdef NDEBUG
 #define ASSERT(cond, msg) ((void)0) // It disappears directly in the Release version, truly achieving zero overhead
 #endif
 */
inline auto ASSERT(bool cond, const std::string &msg) noexcept {
  if (UNLIKELY(!cond)) {
    std::cerr << "ASSERT : " << msg << std::endl;

    exit(EXIT_FAILURE);
  }
}

/**
 * Core Intent: Without any conditional checks, once the code reaches this line,
 * it indicates that the system has reached an irrecoverable predicament
 * (e.g., missing core configuration files, inability to bind to the network card, etc.).
 * Features: It doesn't require UNLIKELY because it doesn't need to perform branch checks;
 * it simply prints and exits without any further processing.
 */
inline auto FATAL(const std::string &msg) noexcept {
  std::cerr << "FATAL : " << msg << std::endl;

  exit(EXIT_FAILURE);
}
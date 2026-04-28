#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

// enable different levels of logging
// #define ENABLE_TEST // For testing code and debugging during development, not expected to be used in production (RUNS IN IRQ HANDLER so it interferes with protocol!)
#define ENABLE_DEBUG
#define ENABLE_INFO
#define ENABLE_WARNING
#define ENABLE_ERROR

#ifdef ENABLE_TEST
#define test(...) printf("[TEST] "); printf(__VA_ARGS__)
#else
#define test(...)
#endif

#ifdef ENABLE_DEBUG
#define debug(...) printf("[DEBUG] "); printf(__VA_ARGS__)
#else
#define debug(...)
#endif

#ifdef ENABLE_INFO
#define info(...) printf("[INFO] "); printf(__VA_ARGS__)
#else
#define info(...)
#endif

#ifdef ENABLE_WARNING
#define warning(...) printf("[WARNING] "); printf(__VA_ARGS__)
#else
#define warning(...)
#endif

#ifdef ENABLE_ERROR
#define error(...) printf("[ERROR] "); printf(__VA_ARGS__)
#else
#define error(...)
#endif

#endif
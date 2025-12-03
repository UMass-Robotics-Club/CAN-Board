#include <stdio.h>

#ifdef ENABLE_DEBUG
#define debug(...) printf("[DEBUG] "); printf(__VA_ARGS__);
#else
#define debug(...)
#endif

#ifdef ENABLE_INFO
#define info(...) printf("[INFO] "); printf(__VA_ARGS__);
#else
#define info(...)
#endif

#ifdef ENABLE_WARNING
#define warning(...) printf("[WARNING] "); printf(__VA_ARGS__);
#else
#define warning(...)
#endif

#ifdef ENABLE_ERROR
#define error(...) printf("[ERROR] "); printf(__VA_ARGS__);
#else
#define error(...)
#endif
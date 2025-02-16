#ifdef ENABLE_DEBUG
#define debug(...) printf("[DEBUG] ", __VA_ARGS__);
#else
#define debug(...)
#endif

#ifdef ENABLE_INFO
#define info(...) printf("[INFO] ", __VA_ARGS__);
#else
#define info(...)
#endif

#ifdef ENABLE_WARNING
#define warning(...) printf("[WARNING] ", __VA_ARGS__);
#else
#define warning(...)
#endif

#ifdef ENABLE_ERROR
#define error(...) printf("[ERROR] ", __VA_ARGS__);
#else
#define error(...)
#endif
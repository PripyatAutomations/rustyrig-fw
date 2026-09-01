# librustyaxe Logger Subsystem

The logger subsystem provides a flexible logging framework with filtering, callbacks, and priority levels.

## Files
- `logger.h` - Header file defining the API
- `logger.c` - Main implementation

## Key Data Structures
```c
enum LogPriority {
   LOG_NONE = -1,
   LOG_CRIT,
   LOG_AUDIT,
   LOG_WARN,
   LOG_INFO,
   LOG_DEBUG,
   LOG_CRAZY,
   LOG_BLITZKREIG
};

struct log_callback {
   enum LogPriority prio;
   const char *msg;
   bool (*callback)(logpriority_t priority, const char *subsys, const char *fmt, va_list ap);
   struct log_callback *next;
};
```

## Key Functions

### Basic Logging
```c
void Log(logpriority_t priority, const char *subsys, const char *fmt, ...);
void logger_setup(void);
void logger_init(const char *logfile, bool tui_mode);
void logger_end(void);
```

### Callback Management
```c
bool log_add_callback(bool (*log_va_cb)(logpriority_t priority, const char *subsys, const char *fmt, va_list ap));
bool log_remove_callback(struct log_callback *log_callback);
```

### Filter Management
```c
bool log_add_filter(const char *pattern, logpriority_t level);
void log_clear_filters(void);
bool debug_filter(const char *subsys, logpriority_t msg_level);
```

## Usage Example
```c
logger_init("/var/log/rustyrig.log", false);
Log(LOG_INFO, "main", "Application started");
log_add_filter("radio", LOG_DEBUG);
Log(LOG_DEBUG, "radio", "Frequency set to %d Hz", freq);
```

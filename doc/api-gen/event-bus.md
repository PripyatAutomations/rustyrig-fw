# librustyaxe Event Bus Subsystem

The event bus provides a publish-subscribe mechanism for communication between different parts of the application.

## Files
- `event-bus.h` - Header file defining the API
- `event-bus.c` - Main implementation

## Key Functions
```c
bool event_bus_register(const char *event, void (*callback)(void *data));
bool event_bus_unregister(const char *event, void (*callback)(void *data));
bool event_bus_publish(const char *event, void *data);
bool event_bus_subscribe(const char *event, void (*callback)(void *data));
bool event_bus_unsubscribe(const char *event, void (*callback)(void *data));
```

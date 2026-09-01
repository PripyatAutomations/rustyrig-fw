# librustyaxe TUI Subsystem

The terminal user interface subsystem provides a curses-based UI framework.

## Files
- `tui.h` - Header file defining the API
- `tui.c` - Main implementation

## Key Functions
```c
bool tui_init(void);
void tui_end(void);
void tui_refresh(void);
int tui_getch(void);
void tui_clear(void);
void tui_mvprintw(int y, int x, const char *fmt, ...);
```

# librustyaxe CAT Subsystem

The CAT (Computer-Aided Transceiver) subsystem provides communication interfaces with radio transceivers using various protocols.

## Files
- `cat.h` - Header file defining the API
- `cat.c` - Main implementation
- `cat.kpa500.c` - KPA500 specific implementation
- `cat.kpa500.h` - KPA500 header
- `cat.yaesu.c` - Yaesu specific implementation  
- `cat.yaesu.h` - Yaesu header

## Key Functions

### Basic CAT Operations
```c
bool cat_open(const char *device, int baudrate);
void cat_close(void);
bool cat_set_frequency(uint32_t freq);
bool cat_get_frequency(uint32_t *freq);
bool cat_set_mode(int mode);
bool cat_get_mode(int *mode);
bool cat_set_vfo(int vfo);
bool cat_get_vfo(int *vfo);
```

### Protocol-Specific Functions
```c
bool cat_kpa500_init(void);
bool cat_yaesu_send_command(const char *cmd, char *response);
```

## Usage Example
```c
cat_open("/dev/ttyUSB0", 9600);
cat_set_frequency(14070000);
cat_set_mode(1); // SSB mode
cat_close();
```

## Notes
- This subsystem supports multiple radio protocols through modular design
- All functions return `true` on success, `false` on failure
- Requires proper device permissions for serial access

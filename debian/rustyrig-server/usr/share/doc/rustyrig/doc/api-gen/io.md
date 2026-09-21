# librustyaxe IO Subsystem

The IO subsystem provides generic input/output functionality including serial and socket communication.

## Files
- `io.h` - Header file defining the API
- `io.c` - Main implementation
- `io.serial.h` - Serial interface header
- `io.serial.c` - Serial implementation  
- `io.socket.h` - Socket interface header
- `io.socket.c` - Socket implementation

## Key Functions

### Basic IO Operations
```c
bool io_open(const char *device, int flags);
void io_close(int fd);
int io_read(int fd, void *buf, size_t count);
int io_write(int fd, const void *buf, size_t count);
```

### Serial Interface
```c
bool io_serial_open(const char *device, int baudrate);
bool io_serial_set_params(int fd, int baudrate, int databits, int stopbits, bool parity);
```

### Socket Interface
```c
int io_socket_connect(const char *host, int port);
int io_socket_listen(int port);
bool io_socket_send(int fd, const char *data, size_t len);
bool io_socket_recv(int fd, char *buf, size_t len);
```

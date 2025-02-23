// i2c_platform.h
#if defined(HOST_POSIX)
#include "i2c_linux.c"
#elif defined(HOST_ESP32)
#include "i2c_esp32.c"
#elif defined(HOST_STM32)
#include "i2c_stm32.c"
#else
#error "Unsupported platform"
#endif

//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if !defined(_build_config_h)
#define _build_config_h
#include "inc/autoconf.h"

#define EEPROM_READONLY
//#define	NOISY_EEPROM true
#define CAT true
#define BACKEND_HAMLIB_DEBUG	RIG_DEBUG_WARN
#define USE_MQTT
#define	USE_MONGOOSE
#define	USE_EV
#define HTTP_USE_TLS 		1
#define HTTP_TLS_KEY 		"./config/key.pem"
#define HTTP_TLS_CERT 		"./config/cert.pem"

#endif

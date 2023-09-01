/*
 * Here we deal with build time configuration issues
 */
#if	!defined(_config_h)
#define	_config_h

#define	VERSION		"23.01"

#define	CAT_KPA500	true		// Enable support for KPA-500 CAT commands for amp
#define	CAT_FT891	true		// Enable support for ft-891-style CAT commands for transceiver

// 160 80 60 40 30 20 17 15 12 10 6 2 1.25 70
#define	MAX_BANDS	14		// maximum bands supported (changing this requires a recompile and factory reset!)

///////////
// Debug //
///////////
#define	HOST_EEPROM_FILE "eeprom.bin"
#define	HOST_LOG_FILE	"firmware.log"
#define	HOST_CAT_PIPE	"cat.fifo"

#endif	// !defined(_config_h)

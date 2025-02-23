/*
 * Here we implement the Yaesu 891/991a style CAT protocol for control of the rig
 */
#include "config.h"
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "cat.h"
#include "cat_yaesu.h"
#include "state.h"

extern struct GlobalState rig;	// Global state

#if	defined(CAT_YAESU)
// Function stubs for all FT-891 CAT commands
void cat_yaesu_AC(const char *args) {} // Read/Set Clarifier state
void cat_yaesu_AG(const char *args) {} // Read/Set AF gain
void cat_yaesu_AI(const char *args) {} // Enable/Disable AI mode
void cat_yaesu_AN(const char *args) {} // Read/Set Antenna selection
void cat_yaesu_BC(const char *args) {} // Read/Set Bandwidth control
void cat_yaesu_BP(const char *args) {} // Read/Set Bandpass filter
void cat_yaesu_DN(const char *args) {} // Frequency down
void cat_yaesu_DT(const char *args) {} // Read/Set Data Mode TX/RX control
void cat_yaesu_DV(const char *args) {} // Read/Set Data Mode
void cat_yaesu_EX(const char *args) {} // Extended menu commands
void cat_yaesu_FA(const char *args) {} // Read/Set VFO A frequency
void cat_yaesu_FB(const char *args) {} // Read/Set VFO B frequency
void cat_yaesu_FQ(const char *args) {} // Read/Set Memory Channel
void cat_yaesu_FR(const char *args) {} // Read/Set VFO selection
void cat_yaesu_FT(const char *args) {} // Read/Set TX VFO selection
void cat_yaesu_IF(const char *args) {} // Get receiver status
void cat_yaesu_IS(const char *args) {} // Read/Set SQL Type
void cat_yaesu_KY(const char *args) {} // Send CW Keying
void cat_yaesu_LK(const char *args) {} // Read/Set lock status
void cat_yaesu_MC(const char *args) {} // Read/Set Memory Channel data
void cat_yaesu_MD(const char *args) {} // Read/Set operating mode
void cat_yaesu_ME(const char *args) {} // Read/Set Memory Channel name
void cat_yaesu_MN(const char *args) {} // Read/Set Menu parameter
void cat_yaesu_NB(const char *args) {} // Read/Set Noise Blanker
void cat_yaesu_NR(const char *args) {} // Read/Set Noise Reduction
void cat_yaesu_OM(const char *args) {} // Read/Set Split Mode
void cat_yaesu_PC(const char *args) {} // Read/Set RF Output Power
void cat_yaesu_PS(const char *args) {} // Read power supply voltage
void cat_yaesu_RC(const char *args) {} // Read/Set Clarifier offset
void cat_yaesu_RG(const char *args) {} // Read/Set RF gain
void cat_yaesu_RT(const char *args) {} // Read/Set RF Power
void cat_yaesu_RX(const char *args) {} // Start receiving
void cat_yaesu_SH(const char *args) {} // Read/Set Shift frequency
void cat_yaesu_SM(const char *args) {} // Read S-meter/SWR/ALC/COMP
void cat_yaesu_SQ(const char *args) {} // Read/Set squelch
void cat_yaesu_ST(const char *args) {} // Read/Set VOX/PTT status
void cat_yaesu_SV(const char *args) {} // Read/Set VOX settings
void cat_yaesu_SWR(const char *args) {} // Read SWR meter
void cat_yaesu_TQ(const char *args) {} // Read/Set CTCSS/DCS Tone mode
void cat_yaesu_TS(const char *args) {} // Read/Set Tuning step
void cat_yaesu_TX(const char *args) {} // Start transmitting
void cat_yaesu_UP(const char *args) {} // Frequency up
void cat_yaesu_XF(const char *args) {} // Read/Set transceiver filter

CATCommand cat_commands[] = {
    // cmd, min, max, callback
    { "AC", 1, 1, cat_yaesu_AC },
    { "AG", 1, 1, cat_yaesu_AG },
    { "AI", 1, 1, cat_yaesu_AI },
    { "AN", 1, 1, cat_yaesu_AN },
    { "BC", 1, 1, cat_yaesu_BC },
    { "BP", 1, 1, cat_yaesu_BP },
    { "DN", 0, 0, cat_yaesu_DN },
    { "DT", 1, 1, cat_yaesu_DT },
    { "DV", 1, 1, cat_yaesu_DV },
    { "EX", 1, 1, cat_yaesu_EX },
    { "FA", 1, 1, cat_yaesu_FA },
    { "FB", 1, 1, cat_yaesu_FB },
    { "FQ", 1, 1, cat_yaesu_FQ },
    { "FR", 1, 1, cat_yaesu_FR },
    { "FT", 1, 1, cat_yaesu_FT },
    { "IF", 0, 0, cat_yaesu_IF },
    { "IS", 1, 1, cat_yaesu_IS },
    { "KY", 1, 1, cat_yaesu_KY },
    { "LK", 1, 1, cat_yaesu_LK },
    { "MC", 1, 1, cat_yaesu_MC },
    { "MD", 1, 1, cat_yaesu_MD },
    { "ME", 1, 1, cat_yaesu_ME },
    { "MN", 1, 1, cat_yaesu_MN },
    { "NB", 1, 1, cat_yaesu_NB },
    { "NR", 1, 1, cat_yaesu_NR },
    { "OM", 1, 1, cat_yaesu_OM },
    { "PC", 1, 1, cat_yaesu_PC },
    { "PS", 0, 0, cat_yaesu_PS },
    { "RC", 1, 1, cat_yaesu_RC },
    { "RG", 1, 1, cat_yaesu_RG },
    { "RT", 1, 1, cat_yaesu_RT },
    { "RX", 0, 0, cat_yaesu_RX },
    { "SH", 1, 1, cat_yaesu_SH },
    { "SM", 1, 1, cat_yaesu_SM },
    { "SQ", 1, 1, cat_yaesu_SQ },
    { "ST", 1, 1, cat_yaesu_ST },
    { "SV", 1, 1, cat_yaesu_SV },
    { "SWR", 0, 0, cat_yaesu_SWR },
    { "TQ", 1, 1, cat_yaesu_TQ },
    { "TS", 1, 1, cat_yaesu_TS },
    { "TX", 0, 0, cat_yaesu_TX },
    { "UP", 0, 0, cat_yaesu_UP },
    { "XF", 1, 1, cat_yaesu_XF },
    { NULL, 0, 0, NULL } // Terminator
};

#endif

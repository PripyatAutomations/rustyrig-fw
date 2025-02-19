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
void handleFA(const char *args) {} // Read/Set VFO A frequency
void handleFB(const char *args) {} // Read/Set VFO B frequency
void handleFR(const char *args) {} // Read/Set VFO selection
void handleFT(const char *args) {} // Read/Set TX VFO selection
void handleMD(const char *args) {} // Read/Set operating mode
void handleIF(const char *args) {} // Get receiver status
void handleRX(const char *args) {} // Start receiving
void handleTX(const char *args) {} // Start transmitting
void handleAI(const char *args) {} // Enable/Disable AI mode
void handleAG(const char *args) {} // Read/Set AF gain
void handleRG(const char *args) {} // Read/Set RF gain
void handleSQ(const char *args) {} // Read/Set squelch
void handleIS(const char *args) {} // Read/Set SQL Type
void handleRT(const char *args) {} // Read/Set RF Power
void handlePC(const char *args) {} // Read/Set RF Output Power
void handlePS(const char *args) {} // Read power supply voltage
void handleTQ(const char *args) {} // Read/Set CTCSS/DCS Tone mode
void handleAC(const char *args) {} // Read/Set Clarifier state
void handleRC(const char *args) {} // Read/Set Clarifier offset
void handleXF(const char *args) {} // Read/Set transceiver filter
void handleKY(const char *args) {} // Send CW Keying
void handleOM(const char *args) {} // Read/Set Split Mode
void handleEX(const char *args) {} // Extended menu commands
void handleSWR(const char *args) {} // Read SWR meter
void handleSM(const char *args) {} // Read S-meter/SWR/ALC/COMP
void handleST(const char *args) {} // Read/Set VOX/PTT status
void handleLK(const char *args) {} // Read/Set lock status
void handleNB(const char *args) {} // Read/Set Noise Blanker
void handleNR(const char *args) {} // Read/Set Noise Reduction
void handleBC(const char *args) {} // Read/Set Bandwidth control
void handleAN(const char *args) {} // Read/Set Antenna selection
void handleDV(const char *args) {} // Read/Set Data Mode
void handleDT(const char *args) {} // Read/Set Data Mode TX/RX control
void handleBP(const char *args) {} // Read/Set Bandpass filter
void handleSV(const char *args) {} // Read/Set VOX settings
void handleFQ(const char *args) {} // Read/Set Memory Channel
void handleMC(const char *args) {} // Read/Set Memory Channel data
void handleME(const char *args) {} // Read/Set Memory Channel name
void handleMN(const char *args) {} // Read/Set Menu parameter
void handleSH(const char *args) {} // Read/Set Shift frequency
void handleTS(const char *args) {} // Read/Set Tuning step
void handleUP(const char *args) {} // Frequency up
void handleDN(const char *args) {} // Frequency down

// Command lookup table
typedef struct {
    const char *command;
    uint8_t min_args;
    uint8_t max_args;
    void (*handler)(const char *args);
} CATCommand;

CATCommand cat_commands[] = {
    { "FA", 1, 1, handleFA },
    { "FB", 1, 1, handleFB },
    { "FR", 1, 1, handleFR },
    { "FT", 1, 1, handleFT },
    { "MD", 1, 1, handleMD },
    { "IF", 0, 0, handleIF },
    { "RX", 0, 0, handleRX },
    { "TX", 0, 0, handleTX },
    { "AI", 1, 1, handleAI },
    { "AG", 1, 1, handleAG },
    { "RG", 1, 1, handleRG },
    { "SQ", 1, 1, handleSQ },
    { "IS", 1, 1, handleIS },
    { "RT", 1, 1, handleRT },
    { "PC", 1, 1, handlePC },
    { "PS", 0, 0, handlePS },
    { "TQ", 1, 1, handleTQ },
    { "AC", 1, 1, handleAC },
    { "RC", 1, 1, handleRC },
    { "XF", 1, 1, handleXF },
    { "KY", 1, 1, handleKY },
    { "OM", 1, 1, handleOM },
    { "EX", 1, 1, handleEX },
    { "SWR", 0, 0, handleSWR },
    { "SM", 1, 1, handleSM },
    { "ST", 1, 1, handleST },
    { "LK", 1, 1, handleLK },
    { "NB", 1, 1, handleNB },
    { "NR", 1, 1, handleNR },
    { "BC", 1, 1, handleBC },
    { "AN", 1, 1, handleAN },
    { "DV", 1, 1, handleDV },
    { "DT", 1, 1, handleDT },
    { "BP", 1, 1, handleBP },
    { "SV", 1, 1, handleSV },
    { "FQ", 1, 1, handleFQ },
    { "MC", 1, 1, handleMC },
    { "ME", 1, 1, handleME },
    { "MN", 1, 1, handleMN },
    { "SH", 1, 1, handleSH },
    { "TS", 1, 1, handleTS },
    { "UP", 0, 0, handleUP },
    { "DN", 0, 0, handleDN },
    { NULL, 0, 0, NULL } // Terminator
};

#endif

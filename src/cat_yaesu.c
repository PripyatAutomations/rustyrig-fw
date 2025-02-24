/*
 * Here we implement the Yaesu 891/991a style CAT protocol for control of the rig
 * ft891 has complete CAT enough for all uses, so it's our milestone goal
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
#include "ptt.h"
extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

#if	defined(CAT_YAESU)
// Function stubs for all FT-891 CAT commands
void cat_yaesu_vfo_a_to_b(const char *args) {}		// VFO A to VFO B
void cat_yaesu_tuner_control(const char *args) {}	// Antenna Tuner Control
void cat_yaesu_af_gain(const char *args) {} 		// Read/Set AF gain
void cat_yaesu_auto_info(const char *args) {}	 	// Enable/Disable Auto Information mode
void cat_yaesu_vfo_a_to_mem(const char *args) {} 	// Store VFO A to memory channel
void cat_yaesu_vfo_b_to_a(const char *args) {}		// VFO B to VFO A
void cat_yaesu_ant_select(const char *args) {} 		// Read/Set Antenna selection
void cat_yaesu_auto_notch(const char *args) {} 		// Read/Set Auto Notch
void cat_yaesu_band_down(const char *args) {} 		// Band Down
void cat_yaesu_band_select(const char *args) { } 	// Band Select
void cat_yaesu_band_up(const char *args) { } 		// band up
void cat_yaesu_break_in(const char *args) {} 		// Break-in
void cat_yaesu_manual_notch(const char *args) {} 	// Manual Notch
void cat_yaesu_busy(const char *args) { } 		// Busy
void cat_yaesu_clarifier(const char *args) { } 		// Clarifier
void cat_yaesu_channel(const char *args) { } 		// Channel
void cat_yaesu_ctcss_number(const char *arg) { } 	// CTCSS Number
void cat_yaesu_contour(const char *arg) { } 		// Contour
void cat_yaesu_cw_spot(const char *arg) { } 		// CW Spot
void cat_yaesu_ctcss(const char *arg) { } 		// CTCSS
void cat_yaesu_dimmer(const char *arg) { }		// Dimmer
void cat_yaesu_down(const char *args) {}		// Down
void cat_yaesu_encoder_down(const char *args) {}	// Encoder Down
void cat_yaesu_enter(const char *args) {}		// Enter key
void cat_yaesu_encoder_up(const char *args) {}		// Encoder Up
void cat_yaesu_menu(const char *args) {}		// Extended menu commands
void cat_yaesu_set_vfo_a(const char *args) {}		// Read/Set VFO A frequency
void cat_yaesu_set_vfo_b(const char *args) {} 		// Read/Set VFO B frequency
void cat_yaesu_fast_step(const char *args) {}		// Fast Step
void cat_yaesu_agc_func(const char *args) {}		// AGC Function
void cat_yaesu_id(const char *args) {}			// IDentification
void cat_yaesu_info(const char *args) {}		// Get receiver status
void cat_yaesu_if_shift(const char *args) {}		// IF shift
void cat_yaesu_cw_key(const char *args) {}		// Send CW Keying
void cat_yaesu_lock(const char *args) {}		// Read/Set lock status
void cat_yaesu_mem_to_vfo_a(const char *args) {}	// Memory to VFO A
void cat_yaesu_mem_channel(const char *args) {} 	// Read/Set Memory Channel data
void cat_yaesu_mic_gain(const char *args) {}		// Mic Gain
void cat_yaesu_mode(const char *args) {}		// Read/Set operating mode
void cat_yaesu_monitor_level(const char *args) {} 	// Monitor Level
void cat_yaesu_memory_read(const char *args) {} 	// Memory Read
void cat_yaesu_meter_switch(const char *args) {} 	// Meter Switch
void cat_yaesu_mem_write_tag(const char *args) {} 	// Memory write & tag
void cat_yaesu_mem_write(const char *args) {} 		// Memory write
void cat_yaesu_mox(const char *args) {} 		// MOX
void cat_yaesu_narrow(const char *args) {} 		// Narrow
void cat_yaesu_noise_blanker(const char *args) {} 	// Read/Set Noise Blanker
void cat_yaesu_nb_level(const char *args) {} 		// NB Level
void cat_yaesu_noise_reduction(const char *args) {} 	// Noise Reduction
void cat_yaesu_obi(const char *args) {} 		// Opposite Band Info??
void cat_yaesu_offset(const char *args) {} 		// Offset
void cat_yaesu_preamp(const char *args) {} 		// Preamp
void cat_yaesu_playback(const char *args) {} 		// Playback
void cat_yaesu_preproc(const char *args) {} 		// Preprocessor
void cat_yaesu_preproc_level(const char *args) {} 	// Preprocessor level
void cat_yaesu_tx_power(const char *args) {} 		// Read/Set RF Output Power
void cat_yaesu_power(const char *args) {}		// Toggle power
void cat_yaesu_qmb_store(const char *args) {} 		// QMB Store
void cat_yaesu_width(const char *args) {} 		// Width
void cat_yaesu_s_meter(const char *args) {}		// Read S-meter/SWR/ALC/COMP
void cat_yaesu_squelch(const char *args) {} 		// Read/Set squelch
void cat_yaesu_split(const char *args) {} 		// Split
void cat_yaesu_swap_vfo(const char *args) {} 		// Swap VFOs
void cat_yaesu_txw(const char *args) {} 		// Read/Set Tuning step


// PTT set
void cat_yaesu_ptt(const char *args) {
//   ptt_set();
//   ptt_toggle();
}

void cat_yaesu_unlock(const char *args) {}		// Unlock
void cat_yaesu_up(const char *args) {} 			// UP
void cat_yaesu_vfo_mem(const char *args) {}		// VFO/Memory button
void cat_yaesu_vox_gain(const char *args) {}		// VOX Gain
void cat_yaesu_vox(const char *args) {}			// VOX
void cat_yaesu_zero_in(const char *args) {}		// Zero In

CATCommand cat_commands[] = {
    // cmd, min, max, callback
    { "AB", 0, 1, cat_yaesu_vfo_a_to_b },	// VFO A to VFO B
    { "AC", 1, 1, cat_yaesu_tuner_control },	// Antenna Tuner Control
    { "AG", 1, 1, cat_yaesu_af_gain },		// AF gain
    { "AI", 1, 1, cat_yaesu_auto_info },	// Auto Information
    { "AM", 1, 1, cat_yaesu_vfo_a_to_mem },	// Store VFO A to memory
    { "AN", 1, 1, cat_yaesu_ant_select },	// ANtenna selection (not on ft891)
    { "BA", 0, 1, cat_yaesu_vfo_b_to_a },	// VFO B to VFO A
    { "BC", 1, 1, cat_yaesu_auto_notch },	// Auto Notch
    { "BI", 1, 1, cat_yaesu_break_in },		// Break-in
    { "BD", 1, 1, cat_yaesu_band_down },	// Band Down
    { "BP", 1, 1, cat_yaesu_manual_notch },	// Manual notch
    { "BS", 1, 1, cat_yaesu_band_select },	// Band Select
    { "BU", 1, 1, cat_yaesu_band_up },		// Band up
    { "BY", 1, 1, cat_yaesu_busy },		// Busy
    { "CF", 1, 1, cat_yaesu_clarifier },	// Clarifier
    { "CH", 0, 1, cat_yaesu_channel },		// Channel Up/Down
    { "CN", 0, 1, cat_yaesu_ctcss_number },	// CTCSS Number
    { "CO", 0, 1, cat_yaesu_contour },		// Contour
    { "CS", 0, 1, cat_yaesu_cw_spot },		// CW Spot
    { "CT", 0, 1, cat_yaesu_ctcss },		// CTCSS
    { "DA", 0, 1, cat_yaesu_dimmer },		// Dimmer
    { "DN", 0, 0, cat_yaesu_down },		// DOWN
    { "ED", 0, 1, cat_yaesu_encoder_down },	// Encoder Down
    { "EK", 0, 1, cat_yaesu_enter },		// Enter key
    { "EU", 0, 1, cat_yaesu_encoder_up },	// Encoder Up
    { "EX", 1, 1, cat_yaesu_menu },		// Menu
    { "FA", 1, 1, cat_yaesu_set_vfo_a },	// Set VFO A freq
    { "FB", 1, 1, cat_yaesu_set_vfo_b },	// Set VFO B freq
    { "FS", 0, 1, cat_yaesu_fast_step },	// Fast Step
    { "GT", 0, 1, cat_yaesu_agc_func },		// AGC Function
    { "ID", 0, 1, cat_yaesu_id },		// IDentification
    { "IF", 0, 0, cat_yaesu_info },		// Information
    { "IS", 1, 1, cat_yaesu_if_shift },		// IF shift
    { "KY", 1, 1, cat_yaesu_cw_key },		// CW keying
    { "LK", 1, 1, cat_yaesu_lock },		// Lock
    { "MA", 1, 1, cat_yaesu_mem_to_vfo_a },	// Memory to VFO A
    { "MC", 1, 1, cat_yaesu_mem_channel },	// Memory Channel
    { "MD", 1, 1, cat_yaesu_mode },		// Mode
    { "MG", 0, 1, cat_yaesu_mic_gain },		// Mic Gain
    { "ML", 0, 1, cat_yaesu_monitor_level },	// Monitor Level
    { "MR", 0, 1, cat_yaesu_memory_read },	// Memory Read
    { "MS", 0, 1, cat_yaesu_meter_switch },	// Meter Switch
    { "MT", 0, 1, cat_yaesu_mem_write_tag },	// Memory Write and Tag
    { "MW", 0, 1, cat_yaesu_mem_write },	// Memory Write
    { "MX", 0, 1, cat_yaesu_mox },		// MOX
    { "NA", 0, 1, cat_yaesu_narrow },		// Narrow
    { "NB", 0, 1, cat_yaesu_noise_blanker },	// Noise Blanker
    { "NL", 0, 1, cat_yaesu_nb_level },		// NB Level
    { "NR", 1, 1, cat_yaesu_noise_reduction },  // Noise Reduction
    { "OI", 0, 1, cat_yaesu_obi },		// Other Band Info?
    { "OS", 0, 1, cat_yaesu_offset },		// Offset
    { "PA", 0, 1, cat_yaesu_preamp },		// Preamp
    { "PB", 0, 1, cat_yaesu_playback },		// Playback
    { "PC", 1, 1, cat_yaesu_tx_power },		// TX power level
    { "PL", 0, 1, cat_yaesu_preproc_level },	// Speech preprocessor level
    { "PR", 0, 1, cat_yaesu_preproc },		// Speech preprocessor
    { "PS", 0, 0, cat_yaesu_power },		// Power Switch
    { "QI", 0, 1, cat_yaesu_qmb_store },	// QMB Store
    { "SH", 1, 1, cat_yaesu_width },		// Width
    { "SM", 1, 1, cat_yaesu_s_meter },		// S-Meter
    { "SQ", 1, 1, cat_yaesu_squelch },		// Squelch
    { "ST", 1, 1, cat_yaesu_split },		// Split
    { "SV", 1, 1, cat_yaesu_swap_vfo },		// Swap VFO
    { "TS", 1, 1, cat_yaesu_txw },		// TXW?
    { "TX", 0, 0, cat_yaesu_ptt },		// TX Set (PTT)
    { "UP", 0, 0, cat_yaesu_down },		// UP
    { "UL", 0, 1, cat_yaesu_unlock },		// Unlock
    { "VG", 0, 1, cat_yaesu_vox_gain },		// VOX Gain
    { "VM", 0, 1, cat_yaesu_vfo_mem },		// VFO/Memory button
    { "VX", 0, 1, cat_yaesu_vox },		// VOX
    { "ZI", 0, 1, cat_yaesu_zero_in },		// Zero in
    { NULL, -1, -1, NULL }		 // Terminator
};

bool cat_yaesu_init(void) {
   Log(LOG_INFO, "Rig CAT control (yaesu emu) initialized");
   return false;
}

#endif

#if	!defined(_rr_cat_yaesu_h)
#define	_rr_cat_yaesu_h
#include "config.h"

extern void cat_yaesu_vfo_a_to_b(const char *args);		// VFO A to VFO B
extern void cat_yaesu_tuner_control(const char *args);		// Antenna Tuner Control
extern void cat_yaesu_af_gain(const char *args); 		// Read/Set AF gain
extern void cat_yaesu_auto_info(const char *args);	 	// Enable/Disable Auto Information mode
extern void cat_yaesu_vfo_a_to_mem(const char *args); 		// Store VFO A to memory channel
extern void cat_yaesu_vfo_b_to_a(const char *args);		// VFO B to VFO A
extern void cat_yaesu_ant_select(const char *args); 		// Read/Set Antenna selection
extern void cat_yaesu_auto_notch(const char *args); 		// Read/Set Auto Notch
extern void cat_yaesu_band_down(const char *args); 		// Band Down
extern void cat_yaesu_band_select(const char *args); 		// Band Select
extern void cat_yaesu_band_up(const char *args); 		// band up
extern void cat_yaesu_break_in(const char *args); 		// Break-in
extern void cat_yaesu_manual_notch(const char *args); 		// Manual Notch
extern void cat_yaesu_busy(const char *args); 			// Busy
extern void cat_yaesu_clarifier(const char *args); 		// Clarifier
extern void cat_yaesu_channel(const char *args); 		// Channel
extern void cat_yaesu_ctcss_number(const char *arg);	 	// CTCSS Number
extern void cat_yaesu_contour(const char *arg); 		// Contour
extern void cat_yaesu_cw_spot(const char *arg); 		// CW Spot
extern void cat_yaesu_ctcss(const char *arg); 			// CTCSS
extern void cat_yaesu_dimmer(const char *arg);			// Dimmer
extern void cat_yaesu_down(const char *args);			// Down
extern void cat_yaesu_encoder_down(const char *args);		// Encoder Down
extern void cat_yaesu_enter(const char *args);			// Enter key
extern void cat_yaesu_encoder_up(const char *args);		// Encoder Up
extern void cat_yaesu_menu(const char *args);			// Extended menu commands
extern void cat_yaesu_set_vfo_a(const char *args);		// Read/Set VFO A frequency
extern void cat_yaesu_set_vfo_b(const char *args); 		// Read/Set VFO B frequency
extern void cat_yaesu_fast_step(const char *args);		// Fast Step
extern void cat_yaesu_agc_func(const char *args);		// AGC Function
extern void cat_yaesu_id(const char *args);			// IDentification
extern void cat_yaesu_info(const char *args);			// Get receiver status
extern void cat_yaesu_if_shift(const char *args);		// IF shift
extern void cat_yaesu_cw_key(const char *args);			// Send CW Keying
extern void cat_yaesu_lock(const char *args);			// Read/Set lock status
extern void cat_yaesu_mem_to_vfo_a(const char *args);		// Memory to VFO A
extern void cat_yaesu_mem_channel(const char *args); 		// Read/Set Memory Channel data
extern void cat_yaesu_mic_gain(const char *args);		// Mic Gain
extern void cat_yaesu_mode(const char *args);			// Read/Set operating mode
extern void cat_yaesu_monitor_level(const char *args); 		// Monitor Level
extern void cat_yaesu_memory_read(const char *args); 		// Memory Read
extern void cat_yaesu_meter_switch(const char *args); 		// Meter Switch
extern void cat_yaesu_mem_write_tag(const char *args); 		// Memory write & tag
extern void cat_yaesu_mem_write(const char *args); 		// Memory write
extern void cat_yaesu_mox(const char *args); 			// MOX
extern void cat_yaesu_narrow(const char *args); 		// Narrow
extern void cat_yaesu_noise_blanker(const char *args); 		// Read/Set Noise Blanker
extern void cat_yaesu_nb_level(const char *args); 		// NB Level
extern void cat_yaesu_noise_reduction(const char *args); 	// Noise Reduction
extern void cat_yaesu_obi(const char *args); 			// Opposite Band Info??
extern void cat_yaesu_offset(const char *args); 		// Offset
extern void cat_yaesu_preamp(const char *args); 		// Preamp
extern void cat_yaesu_playback(const char *args); 		// Playback
extern void cat_yaesu_preproc(const char *args); 		// Preprocessor
extern void cat_yaesu_preproc_level(const char *args); 		// Preprocessor level
extern void cat_yaesu_tx_power(const char *args); 		// Read/Set RF Output Power
extern void cat_yaesu_power(const char *args);			// Toggle power
extern void cat_yaesu_qmb_store(const char *args); 		// QMB Store
extern void cat_yaesu_width(const char *args); 			// Width
extern void cat_yaesu_s_meter(const char *args);		// Read S-meter/SWR/ALC/COMP
extern void cat_yaesu_squelch(const char *args); 		// Read/Set squelch
extern void cat_yaesu_split(const char *args); 			// Split
extern void cat_yaesu_swap_vfo(const char *args); 		// Swap VFOs
extern void cat_yaesu_txw(const char *args); 			// Read/Set Tuning step
extern void cat_yaesu_ptt(const char *args);			// PTT set
extern void cat_yaesu_unlock(const char *args);			// Unlock
extern void cat_yaesu_up(const char *args); 			// UP
extern void cat_yaesu_vfo_mem(const char *args);		// VFO/Memory button
extern void cat_yaesu_vox_gain(const char *args);		// VOX Gain
extern void cat_yaesu_vox(const char *args);			// VOX
extern void cat_yaesu_zero_in(const char *args);		// Zero In
extern bool cat_yaesu_init(void);
extern CATCommand cat_yaesu_commands[];

#endif	// !defined(_rr_cat_yaesu_h)

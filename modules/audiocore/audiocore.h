#ifndef bsaudiocore_h
#define bsaudiocore_h

#include <re/re.h>

typedef struct {
	float	release_time;
	float	attack_time;
	float	hold_time;
	float	closed_gain;
	float	open_threshold;
	float	close_threshold;
} noisegate_parameter;

typedef struct {
	float	release_time;
	float	attack_time;
	float	hold_open_time;
	float	hold_closed_time;
	float	closed_gain;
	float	open_threshold;
	float	close_threshold;
	float	tau;
} postgain_parameter;

typedef struct {
	float gain;
	float thresh_lo;
	float thresh_hi;
	float noise_gain;
	bool use_noise_gain;
} compressor_parameter;

#endif

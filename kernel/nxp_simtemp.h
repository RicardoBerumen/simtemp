#ifndef __NXP_SIMTEMP_H__
#define __NXP_SIMTEMP_H__

#include <linux/types.h>

/* --- Modes --- */
enum simtemp_mode {
    MODE_NORMAL = 0,
    MODE_NOISY = 1,
    MODE_RAMP = 2,
};

//Binary Record
/* --- Sample struct returned to user --- */
struct simtemp_sample {
    __u64 timestamp_ns;
    __s32 temp_mC;
    __s16 mode_name;
    __u32 flags; /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */
} __attribute__((packed));

/* --- Optional: default values --- */
#define SIMTEMP_DEFAULT_SAMPLING_MS 1000
#define SIMTEMP_DEFAULT_THRESHOLD_MC 45000

#endif /* __NXP_SIMTEMP_H__ */
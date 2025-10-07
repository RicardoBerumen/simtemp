#ifndef __NXP_SIMTEMP_IOCTL_H__
#define __NXP_SIMTEMP_IOCTL_H__

#include <linux/ioctl.h>
#include <linux/types.h>
#include "nxp_simtemp.h"

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFF
#endif


struct simtemp_config {
    __u32 sampling_ms;
    __u32 threshold_mC;
    __u32 mode;  // 0=normal,1=noisy,2=ramp
};


#define SIMTEMP_IOC_MAGIC 's'
#define SIMTEMP_IOC_CONFIG _IOW(SIMTEMP_IOC_MAGIC, 1, struct simtemp_config) // Writing to the script (Read by the script)
#define SIMTEMP_IOC_GETCONF  _IOR(SIMTEMP_IOC_MAGIC, 2, struct simtemp_config) // Reading of the script (write by the script)


#endif /* __NXP_SIMTEMP_IOCTL_H__ */
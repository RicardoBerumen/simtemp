#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DEVICE_NAME "simtemp"

struct simtemp_sample {
    __u64 timestamp_ns;
    __s32 temp_mC;
    __u32 flags;
} __attribute__((packend));

/* File Operations */

static ssize_t simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct simtemp_sample sample;
    size_t size = sizeof(sample);

    if (count < size) 
        return -EINVAL;
    /* Fill in a dummy sample */
    sample.timestamp_ns = ktime_get_ns();
    sample.temp_mC = 44000;
    sample.flags = 1;
    if (copy_to_user(buf, &sample, size))
        return -EFAULT;
    *ppos += size;
    return size;
    
};

static const struct file_operations simtemp_fops = {
    .owner = THIS_MODULE,
    .read = simtemp_read,
};

/* Device */
static struct miscdevice simtemp_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &simtemp_fops,
    .mode = 0666,
};

/* Module Init / Exit */
static int __init simtemp_init(void)
{
    int ret = misc_register(&simtemp_dev);
    if (ret) {
        pr_err("simtemp: failed to register mic device \n");
        return ret;
    }
    pr_info("simtemp: module loaded, device /dev/%s\n", DEVICE_NAME);
    return 0;
}

static void __exit simtemp_exit(void)
{
    misc_deregister(&simtemp_dev);
    pr_info("simtemp: module unloaded\n");
}

module_init(simtemp_init);
module_exit(simtemp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ricardo Berumen");
MODULE_DESCRIPTION("Temperature sensor simulator for NXP");

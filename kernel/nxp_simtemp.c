#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/random.h> 
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/ioctl.h>

#include "nxp_simtemp.h"
#include "nxp_simtemp_ioctl.h"

#define DEVICE_NAME "simtemp"
#define FIFO_SIZE 128 //number of samples
#define DEFAULT_PERIOD_MS 100

#define SAMPLE_FLAG_NEW   0x01
#define SAMPLE_FLAG_ALERT 0x02



// Global Variables
static DECLARE_KFIFO(sample_fifo, struct simtemp_sample, FIFO_SIZE);
static struct hrtimer simtemp_timer;
static struct work_struct simtemp_work;
static wait_queue_head_t read_wq; //for blocking reads / poll()
static DEFINE_MUTEX(fifo_lock);

// attributes
static int threshold_mC = 45000;
static unsigned int sampling_ms = DEFAULT_PERIOD_MS; 
static enum simtemp_mode mode = MODE_NORMAL;
// alert flag for poll
static atomic_t alert_pending = ATOMIC_INIT(0);
// stat counters
static atomic64_t updates = ATOMIC64_INIT(0);
static atomic64_t alerts = ATOMIC64_INIT(0);
static atomic64_t errors = ATOMIC64_INIT(0);



static struct timer_list sample_timer;
static struct platform_device *simtemp_pdev;


// timer callback, generating samples
static void simtemp_generate_sample(struct work_struct *work){
    struct simtemp_sample s;
    static int ramp_val = 40000;
    s.timestamp_ns = ktime_get_ns();

    // generate sample according to mode
    switch (mode)
    {
    case MODE_NORMAL:
        s.temp_mC = 40000 + (get_random_u32() % 10000); // between 40 - 49°C
        break;
    case MODE_NOISY:
        s.temp_mC = 20000 + (get_random_u32() % 40000); // between 20 - 59°C
        break;
    case MODE_RAMP:
        s.temp_mC = ramp_val;
        ramp_val += 500;
        if (ramp_val > 50000){
            ramp_val = 40000;
        }
        break;
    default:
        s.temp_mC = 40000 + (get_random_u32() % 10000); // between 40 - 49°C
        break;
    }
    
    s.flags = SAMPLE_FLAG_NEW; // new sample flag
    atomic64_inc(&updates);

    if (s.temp_mC <= threshold_mC){
        s.flags |= SAMPLE_FLAG_ALERT; // threshold alert
        atomic64_inc(&alerts);
        atomic_set(&alert_pending, 1);
    }

    //pushing sample to fifo, update errors if full
    mutex_lock(&fifo_lock);
    if (!kfifo_is_full(&sample_fifo)){
        kfifo_in(&sample_fifo, &s, 1);
        wake_up_interruptible(&read_wq);
    } else if (s.flags & SAMPLE_FLAG_ALERT)
    {
        wake_up_interruptible(&read_wq);
    }
    
    mutex_unlock(&fifo_lock);

    //schedule nex timer
    mod_timer(&sample_timer, jiffies + msecs_to_jiffies(sampling_ms));
}

static enum hrtimer_restart simtemp_timer_fn(struct hrtimer *t){
    schedule_work(&simtemp_work);
    hrtimer_forward_now(t, ms_to_ktime(sampling_ms));
    return HRTIMER_RESTART;
}

//Sysfs show/store helpers
static ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr, char *buf){
    return sprintf(buf, "%d\n", sampling_ms);
}

static ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    int val;
    if (kstrtoint(buf, 10, &val)){
        return -EINVAL;
    }
    if (val < 10 || val > 10000){
        return -EINVAL;
    }
    sampling_ms = val;
    return count;
}

static DEVICE_ATTR_RW(sampling_ms);

static ssize_t threshold_mC_show(struct device *dev, struct device_attribute *attr, char *buf){
    return sprintf(buf, "%d\n", threshold_mC);
}
static ssize_t threshold_mC_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    int val;
    if (kstrtoint(buf, 10, &val)){
        return -EINVAL;
    }
    if (val < 10 || val > 10000){
        return -EINVAL;
    }
    threshold_mC = val;
    return count;
}

static DEVICE_ATTR_RW(threshold_mC);

static ssize_t mode_show(struct device *dev,
                         struct device_attribute *attr, char *buf)
{
    const char *m = "normal";
    if (mode == MODE_NOISY) m = "noisy";
    else if (mode == MODE_RAMP) m = "ramp";
    return sprintf(buf, "%s\n", m);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    if (sysfs_streq(buf, "normal")){
        mode = MODE_NORMAL;
    }
    else if (sysfs_streq(buf, "noisy")){
        mode = MODE_NOISY;
    }
    else if (sysfs_streq(buf, "ramp")){
        mode = MODE_RAMP;
    }
    else {
        return -EINVAL;
    }
    return count;
}

static DEVICE_ATTR_RW(mode);

static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buf){
    return sprintf(buf, "updates=%lld\nalerts=%lld\nerrors=%lld\n", atomic64_read(&updates), atomic64_read(&alerts), atomic64_read(&errors));
}

static DEVICE_ATTR_RO(stats);



/* File Operations */


static ssize_t simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct simtemp_sample sample;
    size_t size = sizeof(sample);

    if (count < size) {
        return -EINVAL;
    }
    
    if (kfifo_is_empty(&sample_fifo)){
        if (file->f_flags & O_NONBLOCK){
            return -EAGAIN;
        }
        //blocking read
        if (wait_event_interruptible(read_wq, !kfifo_is_empty(&sample_fifo))){
            return -ERESTARTSYS;
        }
    }

    mutex_lock(&fifo_lock);
    if (!kfifo_out(&sample_fifo, &sample, 1)){
        mutex_unlock(&fifo_lock);
        return -EFAULT;
    }
    mutex_unlock(&fifo_lock);
    
    // clear alerts once consumed
    if (sample.flags & SAMPLE_FLAG_ALERT){
        atomic_set(&alert_pending, 0);
    }

    if (copy_to_user(buf, &sample, size)){
        mutex_unlock(&fifo_lock);
        return -EFAULT;
    }
    return size;
    
};

static __poll_t simtemp_poll(struct file *file, poll_table *wait){
    __poll_t mask = 0;
    poll_wait(file, &read_wq, wait);

    if (!kfifo_is_empty(&sample_fifo)){
        mask |= POLLIN | POLLRDNORM; // new sample alert
    }
    if (atomic_read(&alert_pending)){
        mask |= POLLPRI | POLLERR; // threshold alert
    }
    return mask;
}


// ioctl for atomic/batch config
static long simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
    struct simtemp_config cfg;

    pr_info("simtemp: ioctl called, cmd=0x%x\n", cmd);
    
    switch (cmd){
        // reading new config
        case SIMTEMP_IOC_CONFIG:
            if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg))){
            return -EFAULT;
            }
            sampling_ms = cfg.sampling_ms;
            threshold_mC = cfg.threshold_mC;
            mode = cfg.mode;

            pr_info("simtemp: config updated via ioctl: sampling=%u, threshold=%u, mode=%u\n",
                sampling_ms, threshold_mC, mode);

            break;
        // writing existing config to request
        case SIMTEMP_IOC_GETCONF:
            cfg.sampling_ms = sampling_ms;
            cfg.threshold_mC = threshold_mC;
            cfg.mode = mode;
            if (copy_to_user((void __user *)arg, &cfg, sizeof(cfg)))
                return -EFAULT;
            pr_info("simtemp: config read via ioctl\n");
            break;
        default:
            pr_info("simtemp: unknown ioctl cmd=0x%x\n", cmd);
            return -ENOTTY;
    }
    
    return 0;


}


// file operations
static const struct file_operations simtemp_fops = {
    .owner = THIS_MODULE,
    .read = simtemp_read,
    .poll = simtemp_poll,
    .unlocked_ioctl = simtemp_ioctl,
};

/* Device Registration */
static struct miscdevice simtemp_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &simtemp_fops,
    .mode = 0666,
};


// sysfs attributes
static struct attribute *simtemp_attrs[] = {
    &dev_attr_sampling_ms.attr,
    &dev_attr_threshold_mC.attr,
    &dev_attr_mode.attr,
    &dev_attr_stats.attr,
    NULL,
};

ATTRIBUTE_GROUPS(simtemp);
/* Platform driver & DT Support */
static int simtemp_probe(struct platform_device *pdev){
    u32 val;

    // Use DT property if available, otherwise keep default
    if (pdev->dev.of_node) {  // only if DT node exists
        if (!of_property_read_u32(pdev->dev.of_node, "sampling-ms", &val)) {
            sampling_ms = val;
            pr_info("simtemp: sampling-ms from DT = %u ms\n", sampling_ms);
        } else {
            pr_info("simtemp: DT property sampling-ms not found, using default %u ms\n", sampling_ms);
        }

        if (!of_property_read_u32(pdev->dev.of_node, "threshold-mC", &val)) {
            threshold_mC = val;
            pr_info("simtemp: threshold-mC from DT = %u mC\n", threshold_mC);
        } else {
            pr_info("simtemp: DT property threshold-mC not found, using default %u mC\n", threshold_mC);
        }
    } else {
        pr_info("simtemp: no DT node, using defaults sampling_ms=%u, threshold_mC=%d\n",
                sampling_ms, threshold_mC);
    }
    
    // Register misc device
    int ret = misc_register(&simtemp_dev);
    if (ret) {
        pr_err("simtemp: failed to register mic device \n");
        return ret;
    }

    // create sysfs groups
    ret = sysfs_create_groups(&simtemp_dev.this_device->kobj, simtemp_groups);

    if (ret){
        misc_deregister(&simtemp_dev); // check for errors
        return ret;
    }

    INIT_WORK(&simtemp_work, simtemp_generate_sample);
    INIT_KFIFO(sample_fifo);
    init_waitqueue_head(&read_wq);

    hrtimer_init(&simtemp_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    simtemp_timer.function = simtemp_timer_fn;
    hrtimer_start(&simtemp_timer, ms_to_ktime(sampling_ms), HRTIMER_MODE_REL);

    pr_info("simtemp: platform device probed, device /dev/%s\n", DEVICE_NAME);
    return 0;
}

static int simtemp_remove(struct platform_device *pdev){
    // module unload
    hrtimer_cancel(&simtemp_timer);
    cancel_work_sync(&simtemp_work);
    sysfs_remove_groups(&simtemp_dev.this_device->kobj, simtemp_groups);
    misc_deregister(&simtemp_dev);
    del_timer_sync(&sample_timer);
    pr_info("simtemp: module unloaded\n");
    return 0;
}


/* Of Match Table for real DT */
static const struct of_device_id simtemp_of_match[] = {
    { .compatible = "nxp,simtemp" },
    {},
};
MODULE_DEVICE_TABLE(of, simtemp_of_match);

static struct platform_driver simtemp_driver = {
    .driver = {
        .name = "simtemp",
        .owner = THIS_MODULE,
        .of_match_table = simtemp_of_match,
    },
    .probe = simtemp_probe,
    .remove = simtemp_remove,
};

/* Module Init / Exit */
static int __init simtemp_init(void)
{
    int ret;

    ret = platform_driver_register(&simtemp_driver);
    if (ret)
        return ret;

    simtemp_pdev = platform_device_alloc("simtemp", -1);
    if (!simtemp_pdev) {
        platform_driver_unregister(&simtemp_driver);
        return -ENOMEM;
    }

    ret = platform_device_add(simtemp_pdev);
    if (ret) {
        platform_device_put(simtemp_pdev);
        platform_driver_unregister(&simtemp_driver);
        return ret;
    }

    return 0;
}

static void __exit simtemp_exit(void)
{
    platform_device_unregister(simtemp_pdev);
    platform_driver_unregister(&simtemp_driver);
}

module_init(simtemp_init);
module_exit(simtemp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ricardo Berumen");
MODULE_DESCRIPTION("Temperature sensor simulator for NXP");

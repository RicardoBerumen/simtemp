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

#include "nxp_simtemp.h"

#define DEVICE_NAME "simtemp"
#define FIFO_SIZE 128 //number of samples
#define DEFAULT_PERIOD_MS 100

#define SAMPLE_FLAG_NEW   0x01
#define SAMPLE_FLAG_ALERT 0x02

// Global Variables
static DECLARE_KFIFO(sample_fifo, struct simtemp_sample, FIFO_SIZE);
static struct hrtimer simtemp_timer;
static struct work_struct simtemp_work;
static wait_queue_head_t read_wq;
static DEFINE_MUTEX(fifo_lock);
static int threshold_mC = 45000; 
static atomic_t alert_pending = ATOMIC_INIT(0);

static unsigned int sampling_ms = DEFAULT_PERIOD_MS;

static void simtemp_generate_sample(struct work_struct *work){
    struct simtemp_sample s;
    s.timestamp_ns = ktime_get_ns();
    s.temp_mC = 40000 + (get_random_u32() % 10000); // between 40 - 49°C
    s.flags = SAMPLE_FLAG_NEW;

    if (s.temp_mC <= threshold_mC){
        s.flags |= SAMPLE_FLAG_ALERT;
        atomic_set(&alert_pending, 1);
    }

    mutex_lock(&fifo_lock);
    if (!kfifo_is_full(&sample_fifo)){
        kfifo_in(&sample_fifo, &s, 1);
        wake_up_interruptible(&read_wq);
    } else if (s.flags & SAMPLE_FLAG_ALERT)
    {
        wake_up_interruptible(&read_wq);
    }
    
    mutex_unlock(&fifo_lock);
}

static enum hrtimer_restart simtemp_timer_fn(struct hrtimer *t){
    schedule_work(&simtemp_work);
    hrtimer_forward_now(t, ms_to_ktime(sampling_ms));
    return HRTIMER_RESTART;
}



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
        mask |= POLLIN | POLLRDNORM;
    }
    if (atomic_read(&alert_pending)){
        mask |= POLLPRI | POLLERR;
    }
    return mask;
}



static const struct file_operations simtemp_fops = {
    .owner = THIS_MODULE,
    .read = simtemp_read,
    .poll = simtemp_poll,
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

    INIT_WORK(&simtemp_work, simtemp_generate_sample);
    INIT_KFIFO(sample_fifo);
    init_waitqueue_head(&read_wq);

    hrtimer_init(&simtemp_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    simtemp_timer.function = simtemp_timer_fn;
    hrtimer_start(&simtemp_timer, ms_to_ktime(sampling_ms), HRTIMER_MODE_REL);
    pr_info("simtemp: module loaded, device /dev/%s\n", DEVICE_NAME);
    return 0;
}

static void __exit simtemp_exit(void)
{
    hrtimer_cancel(&simtemp_timer);
    cancel_work_sync(&simtemp_work);
    misc_deregister(&simtemp_dev);
    pr_info("simtemp: module unloaded\n");
}

module_init(simtemp_init);
module_exit(simtemp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ricardo Berumen");
MODULE_DESCRIPTION("Temperature sensor simulator for NXP");

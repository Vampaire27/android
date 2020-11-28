#ifndef _ACC_SIGNAL_H_
#define _ACC_SIGNAL_H_
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>

#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/input.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/interrupt.h>

#include <linux/string.h>

#define ACC_SIGNAL_THREAD_SUPPORT  //Add for run kthread in ACC_SIGNAL driver modules. 
				   //Maybe cannot wakeup the system under deepidle status. 

/*----------------------------------------------------------------------
IOCTL
----------------------------------------------------------------------*/
#define ACC_SIGNAL_DEVNAME "acc_signal"

#define ACC_SIGNAL_IOC_MAGIC	'Z'
#define ACC_SIGNAL_CHECK _IOW(ACC_SIGNAL_IOC_MAGIC, 0, int)

/*define for phone call state*/

extern const struct file_operations *acc_signal_get_fops(void);/*from acc_signal_drv.c*/
extern struct of_device_id acc_signal_of_match[];
void mt_acc_signal_remove(void);
void mt_acc_signal_suspend(void);
void mt_acc_signal_resume(void);
void mt_acc_signal_pm_restore_noirq(void);
long mt_acc_signal_unlocked_ioctl(unsigned int cmd, unsigned long arg);
int mt_acc_signal_probe(struct platform_device *dev);
/****************************************************
globle ACC_SIGNAL variables
****************************************************/


enum acc_signal_status {
	ACC_ON = 0,
	ACC_OFF = 1
};



extern struct platform_device acc_signal_device;

#endif

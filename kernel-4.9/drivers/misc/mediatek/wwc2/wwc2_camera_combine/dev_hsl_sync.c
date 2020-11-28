#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/ioctl.h>
#include <linux/signal.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>


struct WWC2_HSL_NOTIFY
{
	wait_queue_head_t queue;
	int state;
	unsigned char data[4];
};

#define WWC2_HSL_SYNC_NAME	"wwc2_hsl_sync"
static struct WWC2_HSL_NOTIFY hsl_notify;

static int wwc2_hsl_sync_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int wwc2_hsl_sync_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t wwc2_hsl_sync_read(struct file *file, char __user *buf,
						size_t count, loff_t *ptr)
{
	struct WWC2_HSL_NOTIFY *notify = &hsl_notify;
	long time_out = 0;

	notify->state = 0;
	time_out = wait_event_interruptible_timeout(notify->queue, notify->state, 5*HZ);
	if(time_out == 0)
	{
		printk("bbl--wwc2_hsl_sync_read time out\n");
		notify->data[0] = 0;
		notify->data[1] = 0;
		notify->data[2] = 0;
		notify->data[3] = 0;
	}
	if(copy_to_user((void *)buf, (void *)&notify->data,4) != 0)
		printk("bbl--wwc2_hsl_sync_read error\n");

	return count;
}

static ssize_t wwc2_hsl_sync_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct WWC2_HSL_NOTIFY *notify = &hsl_notify;

	if(copy_from_user((void *)&notify->data, (void *)buf, 4) != 0)
		printk("bbl--wwc2_hsl_sync_write error\n");

	notify->state = 1;
	wake_up_interruptible(&notify->queue);
	return count;
}


static struct file_operations wwc2_hsl_sync_fops = 
{
	.owner = THIS_MODULE,
	.open = wwc2_hsl_sync_open,
	.release = wwc2_hsl_sync_release,
	.read = wwc2_hsl_sync_read,
	.write = wwc2_hsl_sync_write,
};

static struct miscdevice wwc2_hsl_sync_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = WWC2_HSL_SYNC_NAME,
	.fops = &wwc2_hsl_sync_fops,
};


static int wwc2_hsl_sync_probe(struct platform_device *pDev)
{
	int err = 0;

	init_waitqueue_head(&hsl_notify.queue);
	hsl_notify.state = 0;
	hsl_notify.data[0] = 0;
	hsl_notify.data[1] = 0;
	hsl_notify.data[2] = 0;
	hsl_notify.data[3] = 0;
	err = misc_register(&wwc2_hsl_sync_device);
	if(err < 0)
		printk("bbl--wwc2_hsl_sync_probe misc_register fail\n");

	return 0;
}

static int wwc2_hsl_sync_remove(struct platform_device *pDev)
{
	misc_deregister(&wwc2_hsl_sync_device);

	return 0;
}

static int wwc2_hsl_sync_suspend(struct platform_device *pDev, pm_message_t Mesg)
{
	return 0;
}

static int wwc2_hsl_sync_resume(struct platform_device *pDev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id wwc2_hsl_sync_of_ids[] = {
	{.compatible = "mediatek,wwc2-hsl-sync",},
	{}
};
#endif

static struct platform_driver wwc2_hsl_sync_driver = {
	.probe = wwc2_hsl_sync_probe,
	.remove = wwc2_hsl_sync_remove,
	.suspend = wwc2_hsl_sync_suspend,
	.resume = wwc2_hsl_sync_resume,
	.driver = {
		.name = WWC2_HSL_SYNC_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table =wwc2_hsl_sync_of_ids,
#endif
	}
};
static  int __init wwc2_hsl_sync_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&wwc2_hsl_sync_driver);

	return ret;
}

static void __exit wwc2_hsl_sync_exit(void)
{
	platform_driver_unregister(&wwc2_hsl_sync_driver);
}

module_init(wwc2_hsl_sync_init);
module_exit(wwc2_hsl_sync_exit);

MODULE_AUTHOR("bbl");
MODULE_DESCRIPTION("wwc2 hsl sync driver");
MODULE_LICENSE("GPL");


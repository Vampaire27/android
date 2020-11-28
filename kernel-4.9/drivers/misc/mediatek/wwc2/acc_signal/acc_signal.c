#include "acc_signal.h"
#include <linux/of_gpio.h>
#include <upmu_common.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/input.h>

extern void usb_mode_swith_by_id(bool en);
#define SUPPORT_SWITCH_DEV 1
/*----------------------------------------------------------------------
static variable defination
----------------------------------------------------------------------*/
#define ACC_SIGNAL_DEBUG(format, args...) pr_debug(format, ##args)
#define ACC_SIGNAL_INFO(format, args...) pr_warn(format, ##args)
#define ACC_SIGNAL_ERROR(format, args...) pr_err(format, ##args)

static int acc_signal_irq;
static unsigned int acc_signal_gpiopin = 0;
static unsigned int acc_signal_debounce = 0;
static unsigned int acc_signal_eint_type;
#if SUPPORT_SWITCH_DEV
#include <linux/switch.h>
static struct switch_dev acc_signal_data;
#endif
static struct cdev *acc_signal_cdev;
static int g_acc_signal_status = 1; //fangjie for copy to user space, 0-->ACC ON   1-->ACC OFF
static dev_t acc_signal_devno;
static int eint_acc_signal_sync_flag;
static int g_acc_signal_first = 1;
static struct wakeup_source *acc_signal_irq_lock;
static struct work_struct acc_signal_on_work;
static struct workqueue_struct *acc_signal_on_workqueue;
static struct work_struct acc_signal_off_work;
static struct workqueue_struct *acc_signal_off_workqueue;
static DEFINE_MUTEX(acc_signal_eint_irq_sync_mutex);
static struct delayed_work acc_signal_eint_work;
static struct workqueue_struct *acc_signal_eint_workqueue;

#ifdef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
//TODO nothing, touch coordinate from external device.  eg. CanMCU
#else
extern int tpd_load_status;//add by liuxin for decide which tp is loading success  0: fail.  1: success
extern void tpd_off_extern(void);
extern void tpd_on_extern(void);
#endif

/* Used to let system know if ACC SIGNAL is ON or OFF */
#define EINT_PIN_ACC_ON        (0)
#define EINT_PIN_ACC_OFF       (1)
int cur_acc_enit_state = EINT_PIN_ACC_OFF;

#ifdef ACC_SIGNAL_THREAD_SUPPORT
static DECLARE_WAIT_QUEUE_HEAD(acc_waiter);
static struct task_struct *acc_signal_thread;
static int acc_irq_flag;

static int acc_signal_event_handler(void *unused)
{	
	struct sched_param acc_signal_param = { .sched_priority = /*RTPM_PRIO_TPD*/ 4 };
	sched_setscheduler(current, SCHED_RR, &acc_signal_param);
	
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		printk("acc_signal, wait_event_interrup ++++++\n");
		wait_event_interruptible(acc_waiter, (acc_irq_flag != 0));
		acc_irq_flag = 0;
		printk("acc_signal, wait_event_interrup ------\n");
		set_current_state(TASK_RUNNING);
		
	} while (!kthread_should_stop()); //destroy_workqueue() will call kthread_should_stop()

	return 0;
}

#endif

#ifdef CONFIG_ACC_SIGNAL_REPORT_KEY
static struct input_dev *input_acc = NULL;
static void acc_signal_report_key(int status)
{
	int report_key = 0;
	if (status)
	{
		report_key = KEY_SLEEP;  //Note: KEY_SLEEP will report to frameworks, but frameworks may ignore it.
                                         //Enter SLEEP only follow the uart command which from MCU.
	}	
	else
	{
		report_key = KEY_WAKEUP;	
	}
		
	if (report_key != 0)
	{
		input_report_key(input_acc, report_key, 1);
		input_sync(input_acc);
		input_report_key(input_acc, report_key, 0);
		input_sync(input_acc);
	}
}
#endif 

#ifdef CONFIG_ACCEL_HIT_HAPPEN_DETECT
bool is_acc_off(void)
{
	bool ret = true;

	if(acc_signal_gpiopin > 0 && gpio_get_value(acc_signal_gpiopin) == 0)
		ret = false;

	return ret;
}

extern void enable_gsensor_irq(void);
extern void disable_gsensor_irq(void);
#endif
static void acc_signal_on_work_callback(struct work_struct *work)
{
	__pm_stay_awake(acc_signal_irq_lock);  //when ACC ON must lock wake_lock for exit suspend .

	mutex_lock(&acc_signal_eint_irq_sync_mutex);
	if ((1 == eint_acc_signal_sync_flag) && (!gpio_get_value(acc_signal_gpiopin)))
	{
#if SUPPORT_SWITCH_DEV
		switch_set_state((struct switch_dev *)&acc_signal_data, ACC_ON);
#endif
		g_acc_signal_status = 0;
#ifdef CONFIG_ACC_SIGNAL_REPORT_KEY
		acc_signal_report_key(g_acc_signal_status);
#endif

        #ifdef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
        //TODO nothing, touch coordinate from external device.  eg. CanMCU
        #else
		if(1 == tpd_load_status) // add by liuxin for decide which tp is loading success 
		{
		    tpd_on_extern();/*add by liuxin 20180414---use for acc func to disable tp irq & enable tp irq,reviewed by fjie*/
		}	
	#endif
		usb_mode_swith_by_id(0);

#ifdef CONFIG_ACCEL_HIT_HAPPEN_DETECT
		disable_gsensor_irq();
#endif
		ACC_SIGNAL_DEBUG(" [acc_signal] set state in ACC ON  status\n");
	}		
	else
	{	
		ACC_SIGNAL_DEBUG("[acc_signal] ACC ON but don't set acc_signal state\n");
	}
	mutex_unlock(&acc_signal_eint_irq_sync_mutex);
}


static void acc_signal_off_work_callback(struct work_struct *work)
{
	__pm_relax(acc_signal_irq_lock); //when ACC OFF must unlock wake_lock for entry suspend .

	mutex_lock(&acc_signal_eint_irq_sync_mutex);
	if ((0 == eint_acc_signal_sync_flag) && (gpio_get_value(acc_signal_gpiopin)))
	{
		//Send switch event (ACC_OFF) to frameworks.
#if SUPPORT_SWITCH_DEV
		switch_set_state((struct switch_dev *)&acc_signal_data, ACC_OFF); 
#endif
		g_acc_signal_status = 1;
		
		//Report KEY_SLEEP to upper. is not need when main service throught uart to receive ACC OFF information.
#ifdef CONFIG_ACC_SIGNAL_REPORT_KEY
		acc_signal_report_key(g_acc_signal_status); 
#endif

#ifdef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
//TODO nothing, touch coordinate from external device.  eg. CanMCU
#else
	if(1 == tpd_load_status) // add by liuxin for decide which tp is loading success 
        {
            tpd_off_extern();/*add by liuxin 20180414---use for acc func to disable tp irq & enable tp irq,reviewed by fjie*/
        }
#endif
	
#ifdef CONFIG_ACCEL_HIT_HAPPEN_DETECT
		enable_gsensor_irq();
#endif
		ACC_SIGNAL_DEBUG(" [acc_signal] set state in ACC OFF  status\n");
	}		
	else
	{
		ACC_SIGNAL_DEBUG("[acc_signal] ACC OFF but don't set acc_signal state\n");
	}		
	mutex_unlock(&acc_signal_eint_irq_sync_mutex);	
}

static void acc_signal_on_detect(void)
{
	int ret = 0;

	ACC_SIGNAL_DEBUG("[acc_signal]acc_signal_on_detect\n");
	ret = queue_work(acc_signal_on_workqueue, &acc_signal_on_work);
	if (!ret)
		ACC_SIGNAL_DEBUG("[acc_signal]acc_signal_on_detect: return:%d!\n", ret);
}

static void acc_signal_off_detect(void)
{
	int ret = 0;

	ACC_SIGNAL_DEBUG("[acc_signal]acc_signal_off_detect\n");
	ret = queue_work(acc_signal_off_workqueue, &acc_signal_off_work);
	if (!ret)
		ACC_SIGNAL_DEBUG("[acc_signal]acc_signal_off_detect: return:%d!\n", ret);
}

static void acc_signal_eint_work_callback(struct work_struct *work)
{
	printk("%s cur_acc_enit_state = %d, pinValue = %d\n",__func__, cur_acc_enit_state, gpio_get_value(acc_signal_gpiopin));
	if ((cur_acc_enit_state == EINT_PIN_ACC_ON) && (!gpio_get_value(acc_signal_gpiopin))) {
		//ACC_SIGNAL_DEBUG("[acc_signal]ACC_ON, cur_acc_enit_state = %d\n", cur_acc_enit_state);
		mutex_lock(&acc_signal_eint_irq_sync_mutex);
		eint_acc_signal_sync_flag = 1;
		mutex_unlock(&acc_signal_eint_irq_sync_mutex);
		acc_signal_on_detect();

	} else if ((cur_acc_enit_state == EINT_PIN_ACC_OFF) && (gpio_get_value(acc_signal_gpiopin))) {
		//ACC_SIGNAL_DEBUG("[acc_signal]ACC_OFF, cur_acc_enit_state = %d\n", cur_acc_enit_state);
		mutex_lock(&acc_signal_eint_irq_sync_mutex);
		eint_acc_signal_sync_flag = 0;
		mutex_unlock(&acc_signal_eint_irq_sync_mutex);
		acc_signal_off_detect();
	}
	else
	{
		ACC_SIGNAL_DEBUG("acc_signal exception: may be a jitter!\n");
	}
	enable_irq(acc_signal_irq);
	ACC_SIGNAL_DEBUG("[acc_signal]enable_irq again !!!!!!\n");
}

static irqreturn_t acc_signal_eint_func(int irq, void *data)
{
	int ret = 0;	
	
	//ACC_SIGNAL_DEBUG("fj [acc_signal]Enter acc_signal_eint_func !!!!!!\n");
	//ACC_SIGNAL_DEBUG("fj acc gpio = %d\n",gpio_get_value(acc_signal_gpiopin)); 	

	if (cur_acc_enit_state == EINT_PIN_ACC_ON) {
		/*
		   To trigger EINT when the ACC signal ON
		   We set the polarity back as we initialed.
		 */

		if (acc_signal_eint_type == IRQ_TYPE_LEVEL_HIGH)
			irq_set_irq_type(acc_signal_irq, IRQ_TYPE_LEVEL_HIGH);
		else
			irq_set_irq_type(acc_signal_irq, IRQ_TYPE_LEVEL_LOW);

		gpio_set_debounce(acc_signal_gpiopin, acc_signal_debounce);


		/* update the eint status */
		cur_acc_enit_state = EINT_PIN_ACC_OFF;
	} else {
		/*
		   To trigger EINT when the ACC signal OFF
		   We set the opposite polarity to what we initialed.
		 */
		if (acc_signal_eint_type == IRQ_TYPE_LEVEL_HIGH)
			irq_set_irq_type(acc_signal_irq, IRQ_TYPE_LEVEL_LOW);
		else
			irq_set_irq_type(acc_signal_irq, IRQ_TYPE_LEVEL_HIGH);

		gpio_set_debounce(acc_signal_gpiopin, acc_signal_debounce);

		/* update the eint status */
		cur_acc_enit_state = EINT_PIN_ACC_ON;

	}

	disable_irq_nosync(acc_signal_irq);
	//ACC_SIGNAL_DEBUG("[acc_signal]current state: = %d\n", cur_acc_enit_state);

	#ifdef ACC_SIGNAL_THREAD_SUPPORT
	acc_irq_flag = 1;
	wake_up_interruptible(&acc_waiter);
	#endif

	ret = queue_delayed_work(acc_signal_eint_workqueue, &acc_signal_eint_work,10);
	return IRQ_HANDLED;
}




static inline int acc_signal_setup_eint(struct platform_device *acc_signal_device)
{
	int ret;
	u32 intmode[2] = {0}; // for interrupts = <gpio9 IRQ_TYPE_LEVEL_LOW>;
	struct device_node *node = NULL;
	struct pinctrl_state *pins_default;
	struct pinctrl *acc_signal_pinctrl1;
	struct pinctrl_state *pins_accsignal_int;

	/*configure to GPIO function, external interrupt */
	ACC_SIGNAL_INFO("[acc_signal] acc_signal_setup_eint\n");
	acc_signal_pinctrl1 = devm_pinctrl_get(&acc_signal_device->dev);
	if (IS_ERR(acc_signal_pinctrl1)) {
		ret = PTR_ERR(acc_signal_pinctrl1);
		dev_err(&acc_signal_device->dev, "Cannot find acc_signal acc_signal_pinctrl1!\n");
		return ret;
	}

	pins_default = pinctrl_lookup_state(acc_signal_pinctrl1, "default");
	if (IS_ERR(pins_default)) {
		ret = PTR_ERR(pins_default);
		/*dev_err(&acc_signal_device->dev, "fwq Cannot find acc_signal pinctrl default!\n");*/
	}

	pins_accsignal_int = pinctrl_lookup_state(acc_signal_pinctrl1, "state_eint_as_int");
	if (IS_ERR(pins_accsignal_int)) {
		ret = PTR_ERR(pins_accsignal_int);
		dev_err(&acc_signal_device->dev, "fwq Cannot find acc_signal pinctrl state_eint_acc_signal!\n");
		return ret;
	}
	pinctrl_select_state(acc_signal_pinctrl1, pins_accsignal_int);

	node = of_find_matching_node(node, acc_signal_of_match);
	if (node) {
		acc_signal_gpiopin = of_get_named_gpio(node, "deb-gpios", 0);
		of_property_read_u32(node, "debounce", &acc_signal_debounce);
		of_property_read_u32_array(node, "interrupts", intmode, ARRAY_SIZE(intmode));
		acc_signal_eint_type = intmode[1];
		gpio_set_debounce(acc_signal_gpiopin, acc_signal_debounce);
		acc_signal_irq = irq_of_parse_and_map(node, 0);
		ret = request_irq(acc_signal_irq, acc_signal_eint_func, IRQF_TRIGGER_NONE, "acc_signal-eint", NULL);
		if (ret != 0) {
			ACC_SIGNAL_ERROR("[acc_signal]EINT IRQ LINE NOT AVAILABLE\n");
		} else {
			enable_irq_wake(acc_signal_irq); //can wake system when deep sleep
			ACC_SIGNAL_ERROR("[acc_signal] set EINT finished,acc_signal_gpiopin =%d, irq=%d, debounce=%d\n",
				     acc_signal_gpiopin, acc_signal_irq, acc_signal_debounce);
		}
	} else {
		ACC_SIGNAL_ERROR("[acc_signal]%s can't find compatible node\n", __func__);
	}
	return 0;
}


int mt_acc_signal_probe(struct platform_device *dev)
{
	int ret = 0;

	ACC_SIGNAL_INFO("acc_signal_probe begin!\n");

	/*--------------------------------------------------------------------
	// below register acc_signal as switch class
        // Create /sys/class/switch/acc_signal/acc_signal
        //        /sys/class/switch/acc_signal/state
	//------------------------------------------------------------------*/
#if SUPPORT_SWITCH_DEV
	acc_signal_data.name = "acc_signal";
	acc_signal_data.index = 0;
	acc_signal_data.state = ACC_OFF;
	ret = switch_dev_register(&acc_signal_data);
	if (ret) {
		ACC_SIGNAL_ERROR("[acc_signal]switch_dev_register returned:%d!\n", ret);
		return 1;
	}
#endif
	/*----------------------------------------------------------------------
	// Create normal device for auido use
	// Create /dev/acc_signal
	//--------------------------------------------------------------------*/
	ret = alloc_chrdev_region(&acc_signal_devno, 0, 1, ACC_SIGNAL_DEVNAME);
	if (ret)
	{
	    ACC_SIGNAL_ERROR("[acc_signal]alloc_chrdev_region: Get Major number error!\n");
	}
		

	acc_signal_cdev = cdev_alloc();
	acc_signal_cdev->owner = THIS_MODULE;
	acc_signal_cdev->ops = acc_signal_get_fops();
	ret = cdev_add(acc_signal_cdev, acc_signal_devno, 1);
	if (ret)
	{
	    ACC_SIGNAL_ERROR("[acc_signal]acc_signal error: cdev_add\n");
	}	

	/*------------------------------------------------------------------
	// Create workqueue
	//------------------------------------------------------------------ */
	acc_signal_on_workqueue = create_singlethread_workqueue("acc_signal_on");
	INIT_WORK(&acc_signal_on_work, acc_signal_on_work_callback);
	acc_signal_off_workqueue = create_singlethread_workqueue("acc_signal_off");
	INIT_WORK(&acc_signal_off_work, acc_signal_off_work_callback);

	/*------------------------------------------------------------------
	// wake lock
	//------------------------------------------------------------------*/
	acc_signal_irq_lock = wakeup_source_register("acc_signal_irq_lock");

	if (1 == g_acc_signal_first ) {
		eint_acc_signal_sync_flag = 0;
		acc_signal_eint_workqueue = create_singlethread_workqueue("acc_signal_eint");
		INIT_DELAYED_WORK(&acc_signal_eint_work, acc_signal_eint_work_callback);
		acc_signal_setup_eint(dev);

		//BEGIN: add by fangjie, Because System reboot may be under ACC_OFF or ACC_ON status.
		if (gpio_get_value(acc_signal_gpiopin))	
		{
			cur_acc_enit_state = EINT_PIN_ACC_ON; //the next irq handler is ACC ON.
			g_acc_signal_status = 0;
#if SUPPORT_SWITCH_DEV
			switch_set_state((struct switch_dev *)&acc_signal_data, ACC_OFF);
#endif
			printk("acc_signal first as HIGH");
		}
		else
		{			
			cur_acc_enit_state = EINT_PIN_ACC_OFF;//the next irq handler is ACC OFF
			g_acc_signal_status = 1;
#if SUPPORT_SWITCH_DEV
			switch_set_state((struct switch_dev *)&acc_signal_data, ACC_ON);
#endif
			printk("acc_signal first as LOW");
		}
		
		g_acc_signal_first = 0;
	}

	ACC_SIGNAL_INFO("[acc_signal]acc_signal_probe done!\n");

#ifdef ACC_SIGNAL_THREAD_SUPPORT
	acc_signal_thread = kthread_run(acc_signal_event_handler, 0, ACC_SIGNAL_DEVNAME);
	if (IS_ERR(acc_signal_thread)) {
		printk( "acc_signal failed to create kernel thread:");
	}
#endif 

#ifdef CONFIG_ACC_SIGNAL_REPORT_KEY
	//Create input device(acc_signal), cat /proc/bus/input/devices
	input_acc = input_allocate_device();
	if (!input_acc)
	{
		printk("acc_signal: input dev allocate memory failed\n");
		return -ENOMEM;
	}

	input_acc->name = "acc_signal";
	input_acc->id.bustype = BUS_HOST;
	__set_bit(KEY_SLEEP, input_acc->keybit);
	__set_bit(KEY_WAKEUP, input_acc->keybit);
	input_set_capability(input_acc, EV_KEY, KEY_SLEEP);
	input_set_capability(input_acc, EV_KEY, KEY_WAKEUP);
	ret = input_register_device(input_acc);
	if (ret)
	{
		printk("acc_signal: input_register_device failed\n");
		goto err_input_register;
	}
#endif

	usb_mode_swith_by_id(0);

	return 0;

#ifdef CONFIG_ACC_SIGNAL_REPORT_KEY
        err_input_register:
		input_free_device(input_acc);
		input_acc = NULL;
		return ret;
#endif
}

void mt_acc_signal_remove(void)
{
	ACC_SIGNAL_DEBUG("[acc_signal]acc_signal_remove begin!\n");

	/*cancel_delayed_work(&acc_signal_on_work);*/
	destroy_workqueue(acc_signal_eint_workqueue);
	destroy_workqueue(acc_signal_on_workqueue);
	destroy_workqueue(acc_signal_off_workqueue);
#if SUPPORT_SWITCH_DEV
	switch_dev_unregister(&acc_signal_data);
#endif
	//device_del(acc_signal_nor_device);
	//class_destroy(acc_signal_class);
	cdev_del(acc_signal_cdev);
	unregister_chrdev_region(acc_signal_devno, 1);
#ifdef CONFIG_ACC_SIGNAL_REPORT_KEY
	input_unregister_device(input_acc);
	if (input_acc)
	{
		input_free_device(input_acc);
		input_acc = NULL;
	}
#endif
	ACC_SIGNAL_DEBUG("[acc_signal]acc_signal_remove Done!\n");
}

#ifdef CONFIG_PM
void mt_acc_signal_suspend(void)	/*only one suspend mode*/
{

}

void mt_acc_signal_resume(void)	/*wake up*/
{

}
/**********************************************************************
//add for IPO-H need update ACC signal when resume

***********************************************************************/
void mt_acc_signal_pm_restore_noirq(void)
{
	int current_status_restore = 0;

	ACC_SIGNAL_DEBUG("[acc_signal]acc_signal_pm_restore_noirq start!\n");
	/*enable ACC_SIGNAL unit*/
	ACC_SIGNAL_DEBUG("acc_signal: enable_acc_signal\n");
	
	eint_acc_signal_sync_flag = 1;
	current_status_restore = gpio_get_value(acc_signal_gpiopin);
	ACC_SIGNAL_DEBUG("current_status_restore = %d\n",current_status_restore);

#if SUPPORT_SWITCH_DEV
	switch_set_state((struct switch_dev *)&acc_signal_data, current_status_restore);
#endif
}
#endif

static long acc_signal_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    	unsigned int temp = 0;  
    	ACC_SIGNAL_DEBUG ("acc_signal_unlocked_ioctl :cmd=%d arg=%ld\n",cmd,arg);
	switch(cmd)
	{		
		case ACC_SIGNAL_CHECK:
			temp=gpio_get_value(acc_signal_gpiopin);
			if(copy_to_user((unsigned char *)arg, &temp, sizeof(temp))!=0)
				return -EFAULT;
			break;
		//case BACK_SIGNAL_CHECK:
		//	printk("------------------------kernel---car back test------------\n");
		//	break;
	}  
    	return 1;  
}

static int acc_signal_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int acc_signal_release(struct inode *inode, struct file *file)
{
	return 0;
}


static ssize_t acc_signal_read(struct file *file, char __user *buf, size_t count, loff_t * ppos)
{
	g_acc_signal_status = gpio_get_value(acc_signal_gpiopin);
	ACC_SIGNAL_DEBUG("acc_signal_read: g_acc_signal_status = %d\n",g_acc_signal_status);
	if (copy_to_user(buf, &g_acc_signal_status, sizeof(g_acc_signal_status)))
	{
		ACC_SIGNAL_DEBUG("acc_signal_read: copy_to_user fail!\n");
		return -EFAULT;
	}
	return 0;
}


static const struct file_operations acc_signal_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = acc_signal_unlocked_ioctl,
	.open = acc_signal_open,
	.release = acc_signal_release,
	.read  = acc_signal_read,
};

const struct file_operations *acc_signal_get_fops(void)
{
	return &acc_signal_fops;
}



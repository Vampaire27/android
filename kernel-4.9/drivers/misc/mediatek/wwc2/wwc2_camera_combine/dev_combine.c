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
#include "dev_combine.h"

struct WWC2_DEV_COMBINE *combine_obj = NULL;
static struct WWC2_CAMERA_ACTION action = {.mode = 0xf, .act = 0xf};
static struct fasync_struct *display_async_queue = NULL;
extern int camera_work_status;
extern int g_user_select_360_camtype;

static int wwc2_camera_combine_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &display_async_queue);
}

static int wwc2_camera_combine_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int wwc2_camera_combine_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long wwc2_camera_combine_ioctl(struct file *file, unsigned int cmd, unsigned long param)
{
	struct WWC2_SIGIO_NOTIFY *notify = &combine_obj->sigio_notify;

	switch(cmd)
	{
		case WWC2_SIGIO_ACK:
			notify->state = 1;
			wake_up_interruptible(&notify->queue);
			break;
		default:
			break;
	}

	return 0;
}

#ifdef CONFIG_COMPAT
static long wwc2_camera_combine_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;
	void __user *arg32 = compat_ptr(arg);

	switch(cmd)
	{
		case WWC2_SIGIO_ACK:
			ret = file->f_op->unlocked_ioctl(file, WWC2_SIGIO_ACK,
						 (unsigned long)arg32);
			if (ret) {
				printk("WWC2_SIGIO_ACK unlocked_ioctl failed.");
			return ret;
		}
	}

	return 0;
}
#endif

static ssize_t wwc2_camera_combine_read(struct file *file, char __user *buf,
						size_t count, loff_t *ptr)
{
	struct WWC2_AVM_NOTIFY *notify = &combine_obj->avm_notify;

	notify->state = 0;
	wait_event_interruptible(notify->queue, notify->state);
	if(copy_to_user((void *)buf, (void *)&notify->data,1) != 0)
		printk("bbl--wwc2_camera1_read error\n");

	return count;
}

static ssize_t wwc2_camera_combine_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct WWC2_AVM_NOTIFY *notify = &combine_obj->avm_notify;

	if(copy_from_user((void *)&notify->data, (void *)buf, 1) != 0)
		printk("bbl--wwc2_camera1_write error\n");

	notify->state = 1;
	wake_up_interruptible(&notify->queue);
	return count;
}

static struct file_operations wwc2_camera_combine_fops = 
{
	.owner = THIS_MODULE,
	.open = wwc2_camera_combine_open,
	.release = wwc2_camera_combine_release,
	.unlocked_ioctl = wwc2_camera_combine_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = wwc2_camera_combine_compat_ioctl,
#endif
	.read = wwc2_camera_combine_read,
	.write = wwc2_camera_combine_write,
	.fasync = wwc2_camera_combine_fasync,
};

static struct miscdevice wwc2_camera_combine_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = WWC2_CAMERA_COMBINE_NAME,
	.fops = &wwc2_camera_combine_fops,
};

static ssize_t show_camera_action(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d-%d\n",action.mode, action.act);
}
static ssize_t store_camera_action(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	int val1, val2;
	bool signal = 1;
	enum WWC2_CAMERA_MODE mode = WWC2_UNKNOW;
	struct WWC2_DEV_COMBINE *obj = combine_obj;
	struct WWC2_SIGIO_NOTIFY *notify = &combine_obj->sigio_notify;
	long time_out = 0;

	sscanf(buf,"%d %d",&val1, &val2);

	mutex_lock(&obj->action_lock);
	action.mode = val1;
	action.act = val2;

	mode = (enum WWC2_CAMERA_MODE)val1;
	switch(mode)
	{
		case WWC2_DISPLAY:
			obj->display_mode= (DISPLAY_MODE)val2;
			break;
		case WWC2_CAPTURE:
			obj->capture_mode= (CAPTURE_MODE)val2;
			break;
		case WWC2_RECORD:
			obj->record_mode= (RECORD_MODE)val2;
			break;
		case WWC2_H264:
			obj->h264_mode= (H264_MODE)val2;
			break;
		case WWC2_CHANNELWATERMARK:
			obj->channel_water_mark = (bool)val2;
			break;
		case WWC2_TIMEWATERMARK:
			obj->time_water_mark = (bool)val2;
			break;
		case WWC2_GPSWATERMARK:
			obj->gps_water_mark = (bool)val2;
			break;
		case WWC2_CARDWATERMARK:
			obj->card_water_mark = (bool)val2;
			break;
		case WWC2_AUDIOENABLE:
			obj->audio_enable = (bool)val2;
			break;
		case WWC2_RECORD_TIMEOUT:
			break;
		case WWC2_CH0_FLIP:
			obj->ch0_filp = val2;
			break;
		case WWC2_CH1_FLIP:
			obj->ch1_filp = val2;
			break;
		case WWC2_CH2_FLIP:
			obj->ch2_filp = val2;
			break;
		case WWC2_CH3_FLIP:
			obj->ch3_filp = val2;
			break;
		case WWC2_RECORD_BPS:
			obj->record_bps = val2;
			break;
		case WWC2_RECORD_DIR:
			obj->record_dir = (SAVE_FILE_DIR)val2;
			break;
		case WWC2_CAPTURE_DIR:
			obj->capture_dir = (SAVE_FILE_DIR)val2;
			break;
		case WWC2_CHANNEL_ORDER:
			obj->ch_order = (CH_ORDER)val2;
			break;
		default:
			signal = 0;
			break;
	}

	if(display_async_queue != NULL && signal == 1)
		kill_fasync(&display_async_queue, SIGIO, POLL_IN);

	if(camera_work_status != 0)
	{
		notify->state = 0;
		time_out = wait_event_interruptible_timeout(notify->queue, notify->state, 2*HZ);
		if(time_out == 0)
			printk("bbl--store_camera_action time out\n");
	}
	mutex_unlock(&obj->action_lock);

	return size;
}
static DEVICE_ATTR(camera_action, 0664, show_camera_action, store_camera_action);


static void dev_combine_key_report(void)
{
	mutex_lock(&combine_obj->input_lock);
	if(combine_obj->input_record_status != NULL)
	{
		input_report_key(combine_obj->input_record_status, KEY_COMPOSE, 1);
		input_sync(combine_obj->input_record_status);
		input_report_key(combine_obj->input_record_status, KEY_COMPOSE, 0);
		input_sync(combine_obj->input_record_status);
	}
	mutex_unlock(&combine_obj->input_lock);
}

void camera_open_report(void)
{
	if(combine_obj != NULL)
	{
		combine_obj->record_status = CAMERA_OPEN_STATUS;
		combine_obj->record_four_status = CAMERA_OPEN_STATUS;
	}
}

void camera_close_report(void)
{
	if(combine_obj != NULL)
	{
		combine_obj->record_status = CAMERA_CLOSE_STATUS;
		combine_obj->record_four_status = CAMERA_CLOSE_STATUS;
		dev_combine_key_report();
	}
}

static ssize_t show_display_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",combine_obj->display_mode);
}
static ssize_t store_display_mode(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	int value;
	sscanf(buf,"%d",&value);
	combine_obj->display_mode = (DISPLAY_MODE)value;

	return size;
}
static DEVICE_ATTR(display_mode, 0664, show_display_mode, store_display_mode);

static ssize_t show_record_status(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", combine_obj->record_status);
}
static ssize_t store_record_status(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	int value;
	sscanf(buf,"%d",&value);

	combine_obj->record_status = (RECORD_STATUS)value;
	dev_combine_key_report();

	return size;
}
static DEVICE_ATTR(record_status, 0664, show_record_status, store_record_status);

static ssize_t show_record_four_status(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", combine_obj->record_four_status);
}
static ssize_t store_record_four_status(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	static int count = 0;

	int value;
	sscanf(buf,"%d",&value);

	count++;
	if(count == 4 && g_user_select_360_camtype == 1)
	{
		combine_obj->record_four_status = (RECORD_STATUS)value;
		dev_combine_key_report();

		count = 0;
	}
	else if(count == 2 && g_user_select_360_camtype != 1)
	{
		combine_obj->record_four_status = (RECORD_STATUS)value;
		dev_combine_key_report();

		count = 0;
	}

	return size;
}
static DEVICE_ATTR(record_four_status, 0664, show_record_four_status, store_record_four_status);


static ssize_t show_record_latency(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct timeval now;
	struct WWC2_DEV_COMBINE *obj = combine_obj;

	printk("bbl--record start latency = %d\n", obj->latency);

	if(obj->latency > 0)
	{
		do_gettimeofday(&now);
		obj->record_start_time_ms = now.tv_sec*1000 + now.tv_usec/1000;
		queue_delayed_work(obj->record_work_queue, &obj->record_work, HZ);
	}

	return sprintf(buf, "%d",obj->latency);
}
static ssize_t store_record_latency(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	int value;
	struct WWC2_DEV_COMBINE *obj = combine_obj;

	sscanf(buf,"%d",&value);

	if(value)
	{
		obj->latency = value;
	}
	else
	{
		cancel_delayed_work(&obj->record_work);
	}

	return size;
}
static DEVICE_ATTR(record_latency, 0664, show_record_latency, store_record_latency);


static ssize_t show_camera_param(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct WWC2_DEV_COMBINE *obj = combine_obj;
	int len = 0;

	if(g_user_select_360_camtype == 2 || g_user_select_360_camtype == 8)
		obj->ch_order = 1;

	len += sprintf(buf+len, "%d-%d-%d-%d-", obj->display_mode, obj->record_mode, obj->capture_mode, obj->h264_mode);
	len += sprintf(buf+len, "%d-%d-%d-%d-", obj->record_status, obj->record_four_status, obj->capture_status, obj->h264_status);
	len += sprintf(buf+len, "%d-%d-%d-%d-", obj->channel_water_mark, obj->time_water_mark, obj->gps_water_mark, obj->card_water_mark);
	len += sprintf(buf+len, "%d-%d-", obj->audio_enable, obj->latency);
	len += sprintf(buf+len, "%d-%d-%d-%d-", obj->ch0_filp, obj->ch1_filp, obj->ch2_filp, obj->ch3_filp);
	len += sprintf(buf+len, "%d-%d-%d-%d\n", obj->record_bps, obj->record_dir, obj->capture_dir, obj->ch_order);
	return len;
}
static ssize_t store_camera_param(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	struct WWC2_DEV_COMBINE *obj = combine_obj;
	int val[18] = {0};

	val[17] = 7;
	sscanf(buf,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
		val, val+1, val+2, val+3, val+4, val+5, val+6, val+7, val+8, val+9, val+10, val+11, val+12, val+13, val+14, val+15, val+16, val+17);

	obj->display_mode = (DISPLAY_MODE)val[0];
	obj->record_mode = (RECORD_MODE)val[1];
	obj->capture_mode = (CAPTURE_MODE)val[2];
	obj->h264_mode = (H264_MODE)val[3];
	obj->channel_water_mark= (bool)val[4];
	obj->time_water_mark = (bool)val[5];
	obj->gps_water_mark = (bool)val[6];
	obj->card_water_mark = (bool)val[7];
	obj->audio_enable = (bool)val[8];
	obj->latency = val[9];
	obj->ch0_filp= val[10];
	obj->ch1_filp= val[11];
	obj->ch2_filp= val[12];
	obj->ch3_filp= val[13];
	obj->record_bps = val[14];
	obj->record_dir = (SAVE_FILE_DIR)val[15];
	obj->capture_dir = (SAVE_FILE_DIR)val[16];
	obj->ch_order = (CH_ORDER)val[17];

	return size;
}
static DEVICE_ATTR(camera_param, 0664, show_camera_param, store_camera_param);

extern void ic_param_set(unsigned int channel_id, unsigned int mode, unsigned char param);
static const unsigned char default_ic_param[][4] = {
						//cont brigt sat hue
						{128, 128, 144, 128},//ch0
						{128, 128, 144, 128},//ch1
						{128, 128, 144, 128},//ch2
						{128, 128, 144, 128}//ch3
						};
static ssize_t show_ic_param(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len = 0;

	len += sprintf(buf+len, "%03d %03d %03d %03d\n", default_ic_param[0][0], default_ic_param[0][1], default_ic_param[0][2], default_ic_param[0][3]);
	len += sprintf(buf+len, "%03d %03d %03d %03d\n", default_ic_param[1][0], default_ic_param[1][1], default_ic_param[1][2], default_ic_param[1][3]);
	len += sprintf(buf+len, "%03d %03d %03d %03d\n", default_ic_param[2][0], default_ic_param[2][1], default_ic_param[2][2], default_ic_param[2][3]);
	len += sprintf(buf+len, "%03d %03d %03d %03d\n", default_ic_param[3][0], default_ic_param[3][1], default_ic_param[3][2], default_ic_param[3][3]);

	return len;
}
static ssize_t store_ic_param(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	unsigned int value[3];
	unsigned int channel_id = 0;
	unsigned int mode = 0;
	unsigned char param = 0;

	sscanf(buf,"%d %d %d",value, value+1, value+2);

	if(value[0] > 3 || value[1] > 3 || value[2] > 255)
	{
		printk("bbl--ic_param error %d %d %d", value[0], value[1], value[2]);
		return size;
	}

	channel_id = value[0];
	mode = value[1];
	param = (unsigned char)value[2];

	ic_param_set(channel_id, mode, param);

	return size;
}
static DEVICE_ATTR(ic_param, 0664, show_ic_param, store_ic_param);

static ssize_t show_capture_file(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct WWC2_FILE_NOTIFY *notify = &combine_obj->capture_file;
	int len = 0;
	long time_out = 0;

	notify->state = 0;
	time_out = wait_event_interruptible_timeout(notify->queue, notify->state, 5*HZ);
	if(time_out == 0)
	{
		printk("bbl--record capture file time out\n");
		return sprintf(buf, "%s\n", " ");
	}

	len += sprintf(buf+len, "%s\n", notify->file_name);
	return len;
}
static ssize_t store_capture_file(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	struct WWC2_FILE_NOTIFY *notify = &combine_obj->capture_file;

	sscanf(buf,"%s", notify->file_name);
	notify->state = 1;
	wake_up_interruptible(&notify->queue);

	return size;
}
static DEVICE_ATTR(capture_file, 0664, show_capture_file, store_capture_file);

static ssize_t show_card_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	int *data = combine_obj->card_data;
	int len = 0;

	len += sprintf(buf+len, "%d-%d-%d-%d-%d-", data[0], data[1], data[2], data[3], data[4]);
	len += sprintf(buf+len, "%d-%d-%d-%d-%d\n", data[5], data[6], data[7], data[8], data[9]);
	return len;
}
static ssize_t store_card_data(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	int *data = combine_obj->card_data;
	struct WWC2_SIGIO_NOTIFY *notify = &combine_obj->sigio_notify;
	long time_out = 0;

	sscanf(buf,"%d %d %d %d %d %d %d %d %d %d",data, data+1, data+2, data+3, data+4, data+5, data+6, data+7, data+8, data+9);

	mutex_lock(&combine_obj->action_lock);
	action.mode = WWC2_CARDWATERMARK;
	action.act = (int)combine_obj->card_water_mark;
	if(display_async_queue)
		kill_fasync(&display_async_queue, SIGIO, POLL_IN);

	if(camera_work_status != 0)
	{
		notify->state = 0;
		time_out = wait_event_interruptible_timeout(notify->queue, notify->state, 2*HZ);
		if(time_out == 0)
			printk("bbl--store_card_data time out\n");
	}
	mutex_unlock(&combine_obj->action_lock);

	return size;
}
static DEVICE_ATTR(card_data, 0664, show_card_data, store_card_data);

static ssize_t show_gps_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct WWC2_GPS_DATA *gps = &combine_obj->gps_data;
	int len = 0;
	int i = 0;
	char n_or_s[24] = {'N'};
	char w_or_e[24] = {'W'};

	if(gps->longitude[0] == '-')
	{
		n_or_s[0] = 'S';
		for(i = 1; i < 16; i++)
			n_or_s[i] = gps->longitude[i];
	}
	else
	{
		for(i = 0; i < 16; i++)
			n_or_s[i+1] = gps->longitude[i];
	}

	if(gps->latitude[0] == '-')
	{
		w_or_e[0] = 'E';
		for(i = 1; i < 16; i++)
			w_or_e[i] = gps->latitude[i];
	}
	else
	{
		for(i = 0; i < 16; i++)
			w_or_e[i+1] = gps->latitude[i];
	}
	len += sprintf(buf+len, "%s,%s\n", n_or_s, w_or_e);
	return len;
}
static ssize_t store_gps_data(struct device *dev, struct device_attribute *attr, const char *buf,size_t size)
{
	struct WWC2_GPS_DATA *gps = &combine_obj->gps_data;
	struct WWC2_SIGIO_NOTIFY *notify = &combine_obj->sigio_notify;
	long time_out = 0;

	sscanf(buf,"%s %s",gps->longitude, gps->latitude);

	mutex_lock(&combine_obj->action_lock);
	action.mode = WWC2_GPSWATERMARK;
	action.act = (int)combine_obj->gps_water_mark;
	if(display_async_queue)
		kill_fasync(&display_async_queue, SIGIO, POLL_IN);

	if(camera_work_status != 0)
	{
		notify->state = 0;
		wait_event_interruptible_timeout(notify->queue, notify->state, 2*HZ);
		if(time_out == 0)
			printk("bbl--store_gps_data time out\n");
	}
	mutex_unlock(&combine_obj->action_lock);

	return size;
}
static DEVICE_ATTR(gps_data, 0664, show_gps_data, store_gps_data);

static struct device_attribute *dev_combine_ctrl_attr_list[] =
{
	&dev_attr_camera_action,
	&dev_attr_record_status,
	&dev_attr_record_four_status,	
	&dev_attr_record_latency,
	&dev_attr_camera_param,
	&dev_attr_display_mode,
	&dev_attr_ic_param,
	&dev_attr_capture_file,
	&dev_attr_card_data,
	&dev_attr_gps_data,
};

static int dev_combine_create_attr(struct device *dev) 
{
	int idx,err = 0;
	int num = (int)(sizeof(dev_combine_ctrl_attr_list)/sizeof(dev_combine_ctrl_attr_list[0]));

	if(!dev) {
		return -EINVAL;
	}	

	for(idx = 0; idx < num; idx++)
	{
		if((err = device_create_file(dev, dev_combine_ctrl_attr_list[idx]))) {            
			break;
		}
	}
	return err;
}

static int dev_combine_delete_attr(struct device *dev)
{
	int idx,err = 0;
	int num = (int)(sizeof(dev_combine_ctrl_attr_list)/sizeof(dev_combine_ctrl_attr_list[0]));

	if (!dev) {
		return -EINVAL;
	}

	for (idx = 0; idx < num; idx++)
		device_remove_file(dev, dev_combine_ctrl_attr_list[idx]);

	return err;
}

static void do_record_work(struct work_struct *data)
{
	struct timeval now;
	long now_ms = 0L;
	struct WWC2_DEV_COMBINE *obj = combine_obj;
	struct WWC2_SIGIO_NOTIFY *notify = &combine_obj->sigio_notify;
	long time_out = 0;

	do_gettimeofday(&now);
	now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;

	if((now_ms - obj->record_start_time_ms) >= obj->latency*60000L)
	{
		printk("bbl--record timeout set to next\n");

		mutex_lock(&obj->action_lock);
		action.mode = WWC2_RECORD_TIMEOUT;
		action.act = 0;
		if(display_async_queue)
			kill_fasync(&display_async_queue, SIGIO, POLL_IN);

		notify->state = 0;
		wait_event_interruptible_timeout(notify->queue, notify->state, 2*HZ);
		if(time_out == 0)
			printk("bbl--do_record_work time out\n");

		mutex_unlock(&obj->action_lock);
		obj->record_start_time_ms = now_ms;
	}

	queue_delayed_work(obj->record_work_queue, &obj->record_work, HZ);
}


static void file_notify_init(struct WWC2_FILE_NOTIFY *notify)
{
	init_waitqueue_head(&notify->queue);
	notify->state = 0;
	memset(notify->file_name, 0, 128);
}

static void avm_notify_init(struct WWC2_AVM_NOTIFY *notify)
{
	init_waitqueue_head(&notify->queue);
	notify->state = 0;
	notify->data = 0;
}

static void sigio_notify_init(struct WWC2_SIGIO_NOTIFY *notify)
{
	init_waitqueue_head(&notify->queue);
	notify->state = 0;
}

static void init_card_data(int data[10])
{
	int i = 0;

	for(i = 0; i < 10; i++)
		data[i] = 0;

	data[0] = 55;
	data[1] = 10;
	data[2] = 36;
	data[3] = 8;
	data[4] = 8;
	data[5] = 8;
	data[6] = 8;
	data[7] = 8;
}

static void init_gps_data(struct WWC2_GPS_DATA *data)
{
	int i = 0;

	for(i = 0; i < 16; i++)
		data->longitude[i] = '\0';

	for(i = 0; i < 16; i++)
		data->latitude[i] = '\0';

	data->longitude[0] = '1';
	data->longitude[1] = '1';
	data->longitude[2] = '4';
	data->longitude[3] = '.';
	data->longitude[4] = '0';
	data->longitude[5] = '8';
	data->longitude[6] = '5';
	data->longitude[7] = '9';
	data->longitude[8] = '5';

	data->latitude[0] = '2';
	data->latitude[1] = '2';
	data->latitude[2] = '.';
	data->latitude[3] = '5';
	data->latitude[4] = '4';
	data->latitude[5] = '7';
	data->latitude[6] = '0';
	data->latitude[7] = '0';
	
}

static int wwc2_camera_combine_probe(struct platform_device *pDev)
{
	int err = 0;

	struct WWC2_DEV_COMBINE *obj = NULL;
	
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		err = -ENOMEM;
		return err;;
	}
	obj->display_mode = QUART_DISPLAY;
	obj->record_mode = QUART_RECORD;
	obj->capture_mode = FOUR_CAPTURE;
	obj->h264_mode = QUART_H264;
	obj->record_status = UNKNOW_RECORD_STATUS;
	obj->record_four_status  = UNKNOW_RECORD_STATUS;
	obj->capture_status = UNKNOW_CAPTURE_STATUS;
	obj->h264_status = UNKNOW_H264_STATUS;
	obj->channel_water_mark = false;
	obj->time_water_mark = false;
	obj->gps_water_mark = false;
	obj->card_water_mark = false;
	obj->audio_enable = false;
	obj->latency = 1;
	obj->ch0_filp = 0;
	obj->ch1_filp = 0;
	obj->ch2_filp = 0;
	obj->ch3_filp = 0;
	obj->record_bps = 3*1024*1024;
	obj->record_dir = DIR_LOCAL;
	obj->capture_dir = DIR_LOCAL;
	obj->ch_order = 7;
	init_card_data(obj->card_data);
	init_gps_data(&obj->gps_data);

	file_notify_init(&obj->capture_file);
	avm_notify_init(&obj->avm_notify);
	sigio_notify_init(&obj->sigio_notify);

	mutex_init(&obj->action_lock);
	mutex_init(&obj->input_lock);
	obj->record_work_queue = create_singlethread_workqueue("record_work_queue");
	INIT_DELAYED_WORK(&obj->record_work, do_record_work);
	obj->record_start_time_ms = 0L;

	obj->input_record_status = input_allocate_device();
	if(obj->input_record_status == NULL)
		printk("dev_combine input allocate device fail.\n");
	else
	{
		obj->input_record_status->name = "dev_combine";
		obj->input_record_status->id.bustype = BUS_HOST;
		__set_bit(KEY_COMPOSE, obj->input_record_status->keybit);
		input_set_capability(obj->input_record_status, EV_KEY, KEY_COMPOSE);
		err = input_register_device(obj->input_record_status);
		if(err)
		{
			printk("dev_combine register input device failed (%d)\n",err);
			input_free_device(obj->input_record_status);
		}
	}
	combine_obj = obj;

	err = misc_register(&wwc2_camera_combine_device);
	if(err < 0)
		goto fail;

	err = dev_combine_create_attr(&(pDev->dev));

	return err;

fail:
	if(obj)
		kfree(obj);

	if(obj->input_record_status)
	{
		input_unregister_device(obj->input_record_status);
		input_free_device(obj->input_record_status);
	}
	return err;
}

static int wwc2_camera_combine_remove(struct platform_device *pDev)
{
	dev_combine_delete_attr(&(pDev->dev));

	misc_deregister(&wwc2_camera_combine_device);
	if(combine_obj)
		kfree(combine_obj);
	return 0;
}

static int wwc2_camera_combine_suspend(struct platform_device *pDev, pm_message_t Mesg)
{
	return 0;
}

static int wwc2_camera_combine_resume(struct platform_device *pDev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id wwc2_camera_combine_of_ids[] = {
	{.compatible = "mediatek,wwc2-camera-combine",},
	{}
};
#endif

static struct platform_driver wwc2_camera_combine_driver = {
	.probe = wwc2_camera_combine_probe,
	.remove = wwc2_camera_combine_remove,
	.suspend = wwc2_camera_combine_suspend,
	.resume = wwc2_camera_combine_resume,
	.driver = {
		.name = WWC2_CAMERA_COMBINE_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table =wwc2_camera_combine_of_ids,
#endif
	}
};
static  int __init wwc2_camera_combine_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&wwc2_camera_combine_driver);

	return ret;
}

static void __exit wwc2_camera_combine_exit(void)
{
	platform_driver_unregister(&wwc2_camera_combine_driver);
}

module_init(wwc2_camera_combine_init);
module_exit(wwc2_camera_combine_exit);

MODULE_AUTHOR("bbl");
MODULE_DESCRIPTION("wwc2 camera combine driver");
MODULE_LICENSE("GPL");


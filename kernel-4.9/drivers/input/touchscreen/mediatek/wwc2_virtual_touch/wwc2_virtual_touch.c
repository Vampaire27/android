#include "tpd.h"
#include "wwc2_virtual_touch.h"

extern int touch_board_down(struct input_dev *dev, int x, int y, int id);
extern void touch_board_up(struct input_dev *dev);
extern void touch_board_init(struct input_dev *dev);

#define WWC2_MAX_TOUCH	2

static struct wwc2_point_info point_info = {.status = 0, .x = 0, .y = 0, .id = 0};
static enum WWC2_POINT_STATUS point_status = BTN_TOUCH_UP;
static enum WWC2_TOUCH_TYPE g_touchtype = TOUCH_TYPE_TOUCHSCREEN;

static int wwc2_tpd_local_init(void)
{
    tpd_load_status = 1;

	//input_set_abs_params(tpd->dev, ABS_MT_TRACKING_ID, 0, (WWC2_MAX_TOUCH-1), 0, 0);

	touch_board_init(tpd->dev);
	return 0;
}

static void wwc2_tpd_suspend(struct device *h)
{

}

static void wwc2_tpd_resume(struct device *h)
{

}

static void wwc2_tpd_down(int x, int y, int id)
{

	if(TOUCH_TYPE_TOUCHBOARD == g_touchtype) 
	{
                touch_board_down(tpd->dev, x, y, id);
	}
	else  //TOUCH_TYPE_TOUCHSCREEN
	{
		input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 1);
		//input_report_abs(tpd->dev, ABS_MT_TRACKING_ID, id);
		input_report_key(tpd->dev, BTN_TOUCH, 1);
		input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
		input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
		input_mt_sync(tpd->dev);
		//input_sync(tpd->dev);
	}
}

static void wwc2_tpd_up(void)
{
        if(TOUCH_TYPE_TOUCHBOARD == g_touchtype) 
	{
                touch_board_up(tpd->dev);
        }
	else //TOUCH_TYPE_TOUCHSCREEN
	{
	input_report_key(tpd->dev, BTN_TOUCH, 0);
	input_mt_sync(tpd->dev);
	//input_sync(tpd->dev);
	}
}

void wwc2_tpd_report(struct wwc2_point_info *info)
{
	g_touchtype = (info->status >> 4) ;   //get the touch type (touchscreen , touchboard, ...)
	info->status  = (info->status & 0xf);  //get the touch status, (touch_up, touch_down,...)
	switch(point_status)
	{
		case BTN_TOUCH_UP: 
			if(info->status == 1)
			{
				wwc2_tpd_down(info->x, info->y, info->id);
				input_sync(tpd->dev);
				point_status = ONE_POINT_DOWN;
			}
			else
			{
				printk("%s BTN_TOUCH_UP error\n",__func__);
			}
			break;
		case ONE_POINT_DOWN:
			if(info->status == 0)
			{
				wwc2_tpd_up();
				input_sync(tpd->dev);
				point_status = BTN_TOUCH_UP;
			}
			else if(info->status == 1 && info->id == 0)
			{
				wwc2_tpd_down(info->x, info->y, info->id);
				input_sync(tpd->dev);
				point_info.status = 1;
				point_info.x = info->x;
				point_info.y = info->y;
				point_info.id = 0;
				point_status = ONE_POINT_DOWN;
			}
			else if(info->status == 1 && info->id == 1)
			{
				wwc2_tpd_down(point_info.x, point_info.y, point_info.id);
				wwc2_tpd_down(info->x, info->y, info->id);
				input_sync(tpd->dev);
				point_status = TWO_POINT_DOWN;
			}
			else
			{
				printk("%s ONE_POINT_DOWN error\n",__func__);
			}
			break;
		case TWO_POINT_DOWN:
			if(info->status == 0)
			{
				point_status = ONE_POINT_DOWN;
			}
			else if(info->status == 1 && info->id == 0)
			{
				point_info.status = 1;
				point_info.x = info->x;
				point_info.y = info->y;
				point_info.id = 0;
				point_status = TWO_POINT_DOWN;
			}
			else if(info->status == 1 && info->id == 1)
			{
				wwc2_tpd_down(point_info.x, point_info.y, point_info.id);
				wwc2_tpd_down(info->x, info->y, info->id);
				input_sync(tpd->dev);
				point_status = TWO_POINT_DOWN;
			}
			else
			{
				printk("%s TWO_POINT_DOWN error\n",__func__);
			}
			break;
	}
}

static struct tpd_driver_t wwc2_tpd_device_driver = {
	.tpd_device_name = "wwc2_tpd",
	.tpd_local_init = wwc2_tpd_local_init,
	.suspend = wwc2_tpd_suspend,
	.resume = wwc2_tpd_resume,	
};


static int __init wwc2_tpd_driver_init(void)
{
	if (tpd_driver_add(&wwc2_tpd_device_driver) < 0)
		printk("add wwc2 touch driver failed\n");
	
	return 0;
}

static void __exit wwc2_tpd_driver_exit(void)
{
	tpd_driver_remove(&wwc2_tpd_device_driver);
}


module_init(wwc2_tpd_driver_init);
module_exit(wwc2_tpd_driver_exit);
MODULE_LICENSE(GTP v2);
MODULE_DESCRIPTION("WWC2 Series Touch Panel Driver");


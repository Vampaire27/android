#ifndef _wwc_gpio_ctrl_H_
#define _wwc_gpio_ctrl_H_
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

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/input.h>
#include <linux/time.h>

//#include <linux/string.h>

/*----------------------------------------------------------------------
IOCTL
----------------------------------------------------------------------*/
#define GPIO_CTRL_DEVNAME            "gpio_ctrl"

#define WWC_GPIO_IOC 'G'
#define GPIO_SET_DISABLE _IOWR(WWC_GPIO_IOC, 0, int)
#define GPIO_SET_ENABLE  _IOWR(WWC_GPIO_IOC, 1, int)


/* ww gpio dst state */
enum WW_DTS_GPIO_STATE {
	WW_DTS_GPIO_STATE_LCM_RST_OUT0 = 0,
	WW_DTS_GPIO_STATE_LCM_RST_OUT1,
	WW_DTS_GPIO_LCD_BACKLIGHT_CONTROL0,
	WW_DTS_GPIO_LCD_BACKLIGHT_CONTROL1,
	WW_DTS_GPIO_ARM_SLEEP_INDICATOR0,
	WW_DTS_GPIO_ARM_SLEEP_INDICATOR1,
	WW_DTS_GPIO_USB_ID_CONTROL0,
	WW_DTS_GPIO_USB_ID_CONTROL1,
	WW_DTS_GPIO_USB_HUB_RST0,
	WW_DTS_GPIO_USB_HUB_RST1,
	WW_DTS_GPIO_LCD_VCOM_PWM,
	WW_DTS_GPIO_LCD_VCOM_GPIO,
	WW_DTS_GPIO_LCD_AVDD_PWM,
	WW_DTS_GPIO_LCD_AVDD_GPIO,
	WW_DTS_GPIO_LCD_DISP_BACKLIGHT_PWM,
	WW_DTS_GPIO_LCD_DISP_BACKLIGHT_GPIO,
	WW_DTS_GPIO_NAVI_AUDIO_CONTROL0,
	WW_DTS_GPIO_NAVI_AUDIO_CONTROL1,
	WW_DTS_GPIO_STATE_MAX,	/* for array size */
};

/****************************************************
globle wwc_gpio_ctrl variables
****************************************************/

enum wwc_gpio_ctrl_status {
	GPIO_STATE_ON = 0,
	GPIO_STATE_OFF = 1
};

#define WWC2_CAM_PARAMETER_SET
#define WWC2_360_CAM_SET
#define WWC2_HUB_SET
#define WWC2_CAM_TYPE_GET

#define WWC2_CAM_MIRROR_SET
//#define WWC2_CAM_RATE_SET
#define WWC2_CAMERA_CHANNEL_SWITCH
#define WWC2_AUX_TYPE_USER_SET

#ifdef WWC2_CAM_RATE_SET
extern int camera_refresh_rate;//0x10:25Hz 0x20:30Hz 0x30:50Hz 0x40:0x60Hz
#endif

#ifdef WWC2_CAM_PARAMETER_SET
extern void camera_param_set(int mode, int para);
extern void get_camera_default_param(int *brgt, int *sat, int *cont);
#endif

#ifdef WWC2_360_CAM_SET
extern int g_user_select_360_camtype;
#endif

#ifdef WWC2_CAM_TYPE_GET
extern int camera_get_cam_type(void);
#endif

#ifdef WWC2_CAMERA_CHANNEL_SWITCH
extern void camera_src_switch_by_user(int channel);
#endif

#ifdef WWC2_AUX_TYPE_USER_SET
extern int aux_type;
#endif
#endif

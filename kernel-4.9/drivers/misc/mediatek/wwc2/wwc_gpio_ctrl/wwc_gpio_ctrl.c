#include "wwc_gpio_ctrl.h"
#include <linux/gpio.h>
#include <upmu_common.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>
#include <asm/atomic.h>
#include <mt-plat/mtk_pwm.h>
#include "mtk_boot_common.h"
#include "../../../../input/touchscreen/mediatek/wwc2_virtual_touch/wwc2_virtual_touch.h"
#include <linux/signal.h>
/*----------------------------------------------------------------------
static variable defination
----------------------------------------------------------------------*/
#define WWC_GPIO_CTRL_DEBUG(format, args...) pr_debug(format, ##args)
#define WWC_GPIO_CTRL_INFO(format, args...) pr_warn(format, ##args)
#define WWC_GPIO_CTRL_ERROR(format, args...) pr_err(format, ##args)

/* ======================================================================== */
static struct class *gpio_class;
static struct device *gpio_device;
static dev_t gpio_devno;
static struct cdev gpio_cdev;
static struct fasync_struct *wwc2_gpio_async_queue;

static DEFINE_MUTEX(usb_id_mutex);

extern int g_camera_isAHDcam;//0:720*240 1:1280*720 2:720*480 3:720*288 4:720*576
extern int g_camera_sig_check;//0:no signal in 1:signal in
extern int g_aux_isAHDcam;//0:720*240 1:1280*720 2:720*480 3:720*288 4:720*576
extern int g_aux_sig_check;//0:no signal in 1:signal in
#ifdef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
int gNavi_audio;
#endif
int usb_speed_hs;
//int musb_speed_set(int mode); //add by fangjie
/* ======================================================================== */
int input_src = 0x00;
int ctp_switch = 1;

#ifndef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
extern int gtp_update;
extern u8 gup_init_update_proc_custom(void);
#endif

static struct pinctrl *ww_gpio_pctrl; /* static pinctrl instance */
extern int vcom_pwm_level;
extern int avdd_pwm_level;
/* DTS state mapping name */
static const char *ww_state_name[WW_DTS_GPIO_STATE_MAX] = {
	"lcm_reset0",
	"lcm_reset1",
	"lcd_backlight_control0", 
	"lcd_backlight_control1",
	"arm_sleep_indicator0",
	"arm_sleep_indicator1",
	"usb_id_control0", 
	"usb_id_control1",
	"usb_hub_rst0",
	"usb_hub_rst1",
	"lcd_vcom_pwm",
	"lcd_vcom_gpio",
	"lcd_avdd_pwm",
	"lcd_avdd_gpio",
	"lcd_disp_backlight_pwm",
	"lcd_disp_backlight_gpio",
	"navi_audio_control0", 
	"navi_audio_control1"
};

enum CHRDET_TYPE
{
	DETECT_STATUS = 0,
	NOT_DETECT_STATUS = 1,
	UNKOWN_STATUS = 2
};

/* pinctrl implementation */
static long _set_state(const char *name)
{
	long ret = 0;
	struct pinctrl_state *pState = 0;

	if (!ww_gpio_pctrl) {
		WWC_GPIO_CTRL_INFO("this pctrl is null\n");
		return -1;
	}

	pState = pinctrl_lookup_state(ww_gpio_pctrl, name);
	if (IS_ERR(pState)) {
		WWC_GPIO_CTRL_INFO("lookup state '%s' failed\n", name);
		ret = PTR_ERR(pState);
		goto exit;
	}

	/* select state! */
	pinctrl_select_state(ww_gpio_pctrl, pState);

exit:
	return ret; /* Good! */
}

static long ww_dts_gpio_init(struct platform_device *pdev)
{
	long ret = 0;
	struct pinctrl *pctrl;

	/* retrieve */
	pctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pctrl)) {
		ret = PTR_ERR(pctrl);
		goto exit;
	}

	ww_gpio_pctrl = pctrl;

exit:
	return ret;
}

static long ww_dts_gpio_select_state(enum WW_DTS_GPIO_STATE s)
{
	if (!((unsigned int)(s) < (unsigned int)(WW_DTS_GPIO_STATE_MAX))) {
		WWC_GPIO_CTRL_INFO("GPIO STATE is invalid,state=%d\n", (unsigned int)s);
		return -1;
	}
	return _set_state(ww_state_name[s]);
}

void lcm_reset_pin_set(bool en)
{
	ww_dts_gpio_select_state(en?WW_DTS_GPIO_STATE_LCM_RST_OUT0:WW_DTS_GPIO_STATE_LCM_RST_OUT1);//opposite by hardware
}

void lcm_blacklight_enable_pin_set(bool en)
{
	ww_dts_gpio_select_state(en?WW_DTS_GPIO_LCD_BACKLIGHT_CONTROL1:WW_DTS_GPIO_LCD_BACKLIGHT_CONTROL0);	
}

void navi_audio_pin_set(bool en)
{
	ww_dts_gpio_select_state(en?WW_DTS_GPIO_NAVI_AUDIO_CONTROL1:WW_DTS_GPIO_NAVI_AUDIO_CONTROL0);
}

extern unsigned int upmu_get_rgs_chrdet(void);
static void usb_id_gpio_control(bool en)
{
	static enum CHRDET_TYPE is_chrdet = UNKOWN_STATUS;

	if(is_chrdet == DETECT_STATUS)
		return;

	if(is_chrdet == NOT_DETECT_STATUS)
	{
		ww_dts_gpio_select_state(en?WW_DTS_GPIO_USB_ID_CONTROL1:WW_DTS_GPIO_USB_ID_CONTROL0);
		return;
	}

	if(upmu_get_rgs_chrdet())
	{
		is_chrdet = DETECT_STATUS;
	}
	else
	{
		is_chrdet = NOT_DETECT_STATUS;
		ww_dts_gpio_select_state(en?WW_DTS_GPIO_USB_ID_CONTROL1:WW_DTS_GPIO_USB_ID_CONTROL0);
	}
}

static void usb_hub_reset_gpio_control(bool en)
{
	ww_dts_gpio_select_state(en?WW_DTS_GPIO_USB_HUB_RST1:WW_DTS_GPIO_USB_HUB_RST0);
}

void usb_mode_swith_by_id(bool en)
{
	if(get_boot_mode() != NORMAL_BOOT && get_boot_mode() != RECOVERY_BOOT)
		return;

	mutex_lock(&usb_id_mutex);
	usb_id_gpio_control(en);
	usb_hub_reset_gpio_control(en);
	mutex_unlock(&usb_id_mutex);
}

void arm_sleep_status_indicator(bool en)
{
	printk("LP_STATUS %d\n",en);
	ww_dts_gpio_select_state(en?WW_DTS_GPIO_ARM_SLEEP_INDICATOR1:WW_DTS_GPIO_ARM_SLEEP_INDICATOR0);
}

void lcm_avdd_set(int duty)
{
	struct pwm_spec_config avdd_pwm_setting;

	printk("%s duty = %d\n",__func__,duty);
	if(duty == -1)
	{
		ww_dts_gpio_select_state(WW_DTS_GPIO_LCD_AVDD_GPIO);
	}
	else
	{
		ww_dts_gpio_select_state(WW_DTS_GPIO_LCD_AVDD_PWM);

		avdd_pwm_setting.pwm_no = PWM2;
		avdd_pwm_setting.mode = PWM_MODE_OLD;
		avdd_pwm_setting.pmic_pad = false;
		avdd_pwm_setting.clk_div = CLK_DIV2;
		avdd_pwm_setting.clk_src = PWM_CLK_OLD_MODE_BLOCK;
		avdd_pwm_setting.PWM_MODE_OLD_REGS.IDLE_VALUE = 0;
		avdd_pwm_setting.PWM_MODE_OLD_REGS.GUARD_VALUE = 0;
		avdd_pwm_setting.PWM_MODE_OLD_REGS.GDURATION = 0;
		avdd_pwm_setting.PWM_MODE_OLD_REGS.WAVE_NUM = 0;
		avdd_pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 250; // default (250/(356+1))~350/(356+1))(70%~98%) to remap 0~100%
		avdd_pwm_setting.PWM_MODE_OLD_REGS.THRESH = duty + 150;
		if(duty >= 0 && duty <= 100)
		{
			avdd_pwm_level = duty;
			pwm_set_spec_config(&avdd_pwm_setting);
		}
	}
}

void lcm_vcom_set(int duty)
{
	struct pwm_spec_config vcom_pwm_setting;

	printk("%s duty = %d\n",__func__,duty);
	if(duty == -1)
	{
		ww_dts_gpio_select_state(WW_DTS_GPIO_LCD_VCOM_GPIO);
	}
	else
	{
		ww_dts_gpio_select_state(WW_DTS_GPIO_LCD_VCOM_PWM);

		vcom_pwm_setting.pwm_no = PWM6;
		vcom_pwm_setting.mode = PWM_MODE_OLD;
		vcom_pwm_setting.pmic_pad = false;
		vcom_pwm_setting.clk_div = CLK_DIV1;
		vcom_pwm_setting.clk_src = PWM_CLK_OLD_MODE_BLOCK;
		vcom_pwm_setting.PWM_MODE_OLD_REGS.IDLE_VALUE = 0;
		vcom_pwm_setting.PWM_MODE_OLD_REGS.GUARD_VALUE = 0;
		vcom_pwm_setting.PWM_MODE_OLD_REGS.GDURATION = 0;
		vcom_pwm_setting.PWM_MODE_OLD_REGS.WAVE_NUM = 0;
		vcom_pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 100; // 100 level
		vcom_pwm_setting.PWM_MODE_OLD_REGS.THRESH = duty;
		if(duty >=0 && duty <= 100)
		{
			vcom_pwm_level = duty;
			pwm_set_spec_config(&vcom_pwm_setting);
		}
	}
}

void lcm_disp_pwm_control(bool is_pwm)
{
	ww_dts_gpio_select_state(is_pwm?WW_DTS_GPIO_LCD_DISP_BACKLIGHT_PWM:WW_DTS_GPIO_LCD_DISP_BACKLIGHT_GPIO);
}

static ssize_t show_input_src(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf , "%d\n", input_src);
}

static ssize_t store_input_src(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int mode;
	
	if (1 != sscanf(buf, "%d", &mode)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}

	switch (mode) {
	case 1:
		input_src = 0x00;
		break;
	case 2:
		input_src = 0x01;
		break;
	case 11:
		input_src = 0x00;//main_cam,sub_cam
		break;
	case 12:
		input_src = 0x01;//main_aux,sub_cam
		break;
	case 21:
		input_src = 0x10;//main_cam,sub_aux
		break;
	case 22:
		input_src = 0x11;//main_aux.sub_aux
		break;
	default:
		input_src = 0x00;
		break;
	}
	
	return size;
}
static DEVICE_ATTR(video_switch, 0664, show_input_src, store_input_src);
static ssize_t show_cammode_src(struct device *dev,struct device_attribute *attr, char *buf)
{
	int ret_value = 0;

	if (input_src == 0x00)
	{
		ret_value = sprintf(buf , "%d\n", g_camera_isAHDcam);       
	}
	else
	{
		ret_value = sprintf(buf , "%d\n", g_aux_isAHDcam);
	} 

	return ret_value;
}


static ssize_t store_cammode_src(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int mode;
	
	if (1 != sscanf(buf, "%d", &mode)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}
	if (input_src == 0x00)
	{
		g_camera_isAHDcam = mode;       
	}
	else
	{
		g_aux_isAHDcam = mode;
	} 
	return size;
}

static DEVICE_ATTR(cam_mode, 0664, show_cammode_src, store_cammode_src);

#ifdef WWC2_CAM_MIRROR_SET
static int cam_mirror_value = 0;
static ssize_t show_cam_mirror(struct device *dev,struct device_attribute *attr, char *buf)
{
    return sprintf(buf , "%d\n", cam_mirror_value);
}

ssize_t store_cam_mirror(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int value = 0;

	if (1 != sscanf(buf, "%d", &value)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}
	cam_mirror_value = value;
	return size;
}
static DEVICE_ATTR(cam_mirror, 0664, show_cam_mirror, store_cam_mirror);
#endif

#ifdef WWC2_CAM_RATE_SET
static ssize_t show_cam_rate(struct device *dev,struct device_attribute *attr, char *buf)
{
    return sprintf(buf , "0x%x\n", camera_refresh_rate);;
}
static DEVICE_ATTR(cam_rate, 0444, show_cam_rate, NULL);
#endif

static ssize_t show_sig_check_src(struct device *dev,struct device_attribute *attr, char *buf)
{
	int ret_value = 0;

	if ((input_src & 0x01) == 0x00)
	{
		ret_value = sprintf(buf , "%d\n", g_camera_sig_check);       
	}
	else
	{
		ret_value = sprintf(buf , "%d\n", g_aux_sig_check);
	}  
	return ret_value;
}


ssize_t store_sig_check_src(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int mode;
	
	if (1 != sscanf(buf, "%d", &mode)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}
	g_camera_sig_check = mode;
	g_aux_sig_check = mode;
	return size;
}

static DEVICE_ATTR(sig_check, 0664, show_sig_check_src, store_sig_check_src);
static ssize_t show_ctp_src(struct device *dev,struct device_attribute *attr, char *buf)
{
    int ret_value = 1;

    ret_value = sprintf(buf , "%d\n", ctp_switch);
  
    return ret_value;
}

static ssize_t store_ctp_src(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int mode;
	
	if (1 != sscanf(buf, "%d", &mode)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}

	ctp_switch = mode;

	return size;
}
static DEVICE_ATTR(ctp_switch, 0664, show_ctp_src, store_ctp_src);

#ifndef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
static ssize_t show_gtp_update_src(struct device *dev,struct device_attribute *attr, char *buf)
{
    int ret_value = 1;
    ret_value = sprintf(buf , "%d\n", gtp_update);
    return ret_value;
}

ssize_t store_gtp_update_src(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int mode;
	
	if (1 != sscanf(buf, "%d", &mode)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}
	if(1 == mode)
	{
		gtp_update = 1;
		if(gup_init_update_proc_custom() < 0)
  			printk("update gtp cfg FAIL!!!\n");
	}
	return size;
}
static DEVICE_ATTR(gtp_update, 0664, show_gtp_update_src, store_gtp_update_src);
#endif

extern unsigned int sw_cg_2_sta_read(void);
static ssize_t show_vcom_pwm_level_src(struct device *dev,struct device_attribute *attr, char *buf)
{
    int ret_value = 0;
    ret_value = sprintf(buf , "vcom hw-pwm5 %d 0x%08x\n", vcom_pwm_level,sw_cg_2_sta_read());
  
    return ret_value;
}

static ssize_t store_vcom_pwm_level_src(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int duty =0;
	if (1 != sscanf(buf, "%d", &duty)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}
	lcm_vcom_set(duty);
	return size;
}
static DEVICE_ATTR(vcom_pwm_duty, 0664, show_vcom_pwm_level_src, store_vcom_pwm_level_src);

extern unsigned int sw_cg_0_sta_read(void);
static ssize_t show_avdd_pwm_level_src(struct device *dev,struct device_attribute *attr, char *buf)
{
    int ret_value = 0;
    ret_value = sprintf(buf , "avdd hw-pwm1 %d 0x%08x\n", avdd_pwm_level,sw_cg_0_sta_read());
  
    return ret_value;
}

static ssize_t store_avdd_pwm_level_src(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int duty =0;
	if (1 != sscanf(buf, "%d", &duty)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}
	lcm_avdd_set(duty);
	return size;
}
static DEVICE_ATTR(avdd_pwm_duty, 0664, show_avdd_pwm_level_src, store_avdd_pwm_level_src);

 /* ======BEGIN: BLSpower attribute operation===================== */
static atomic_t gBLSpower = ATOMIC_INIT(0);

static void BLSpower_enable(int enable)
{
	lcm_blacklight_enable_pin_set(enable?1:0);
	atomic_set(&gBLSpower, enable);	
	return;
}

static ssize_t BLSpower_show(struct device* dev, struct device_attribute *attr, char *buf)
{
	WWC_GPIO_CTRL_INFO("%s", __func__);
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&gBLSpower));
}

static ssize_t BLSpower_store(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val = 0;
	int ret;
	WWC_GPIO_CTRL_INFO("%s buf: %s", __func__, buf);
	if ((ret = sscanf(buf, "%d", &val)) !=1) {
		WWC_GPIO_CTRL_ERROR("set BLSpower enable fail!! ret: %d\n",ret);
		return size;
	}

	if(((val == 0) && (atomic_read(&gBLSpower) != 0))||((val == 1) && (atomic_read(&gBLSpower) != 1)))
	{
		WWC_GPIO_CTRL_INFO("BLSpower onoff val=%d\n",val);
		BLSpower_enable(val);
	}
	else
	{
		WWC_GPIO_CTRL_INFO("BLSpower ignore double setting val=%d\n",val);
	}

	return size;
}

static DEVICE_ATTR(bls_ctrl, 0664, BLSpower_show, BLSpower_store);
/* ======END: BLSpower attribute operation===================== */


/* ======BEGIN: usb_speed attribute operation===================== */
//BEGIN: add by fangjie for configuration USB2.0 or USB1.1
static ssize_t show_usb_speed(struct device *dev,struct device_attribute *attr, char *buf)
{
    int ret_value = 0;
    
    //musb_speed_get();

    ret_value = sprintf(buf , "%d\n", usb_speed_hs);
  
    return ret_value;
}

static ssize_t store_usb_speed(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int mode = 1;
	
	if (1 != sscanf(buf, "%d", &mode)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}		
	
	usb_speed_hs = mode;
	//musb_speed_set(mode);

	return size;
}
static DEVICE_ATTR(usb_speed, 0664, show_usb_speed, store_usb_speed);
/* ======END: usb_speed attribute operation===================== */

static ssize_t show_host_device_switch(struct device *dev,struct device_attribute *attr, char *buf)
{
    int ret_value = 0;
    return ret_value;
}
extern void mt_usb_connect(void);
extern void mt_usb_disconnect(void);
extern void pmic_sp_irq_handler(unsigned int spNo, unsigned int sp_conNo, unsigned int sp_int_status);
extern bool vbus_exist_extern;
ssize_t store_host_device_switch(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int mode = 0;
	
	//printk("<liuxin> %s buf: %s", __func__, buf);
	if (1 != sscanf(buf, "%d", &mode)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}		
#if 0	
	if(mode == 1)
    {
        usb_mode_swith_by_id(1);
        msleep(300);
        pmic_set_register_value(PMIC_RG_USBDL_RST, 1);
        vbus_exist_extern = 1;
        mt_usb_connect();
        pmic_sp_irq_handler(0x2, 0x0, 0x40);
        printk("hsot mode 1\n");
    }
    else
    {
        vbus_exist_extern = 0;
        mt_usb_disconnect();
        usb_mode_swith_by_id(0);//default host
        printk("hsot mode 0\n");
    }
#endif
	return size;
}
static DEVICE_ATTR(host_device_switch, 0664, show_host_device_switch, store_host_device_switch);

#ifdef WWC2_CAM_PARAMETER_SET
static ssize_t show_cam_params(struct device *dev,struct device_attribute *attr, char *buf)
{  
	int brgt = 0;
	int sat = 0;
	int cont = 0;

	get_camera_default_param(&brgt, &sat, &cont);

	return sprintf(buf, "%d %d %d\n", brgt,sat,cont);
}

static ssize_t store_cam_params(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int mode = 0, param = 0;
	int ret = sscanf(buf, "%d %d", &mode,&param);
	
	if(ret != 2) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}		

	camera_param_set(mode,param);

	return size;
}
static DEVICE_ATTR(cam_params, 0664, show_cam_params, store_cam_params);
#endif

#ifdef WWC2_360_CAM_SET
static ssize_t show_360_camtype_src(struct device *dev,struct device_attribute *attr, char *buf)
{
    return sprintf(buf , "%d\n", g_user_select_360_camtype); 
}

static ssize_t store_360_camtype_src(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int val;
	
	if (1 != sscanf(buf, "%d", &val)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}

	g_user_select_360_camtype = val;       

	return size;
}
static DEVICE_ATTR(360_camtype, 0664, show_360_camtype_src, store_360_camtype_src);
#endif
#ifdef WWC2_CAM_TYPE_GET
static ssize_t show_get_cam_type(struct device *dev,struct device_attribute *attr, char *buf)
{
    return sprintf(buf , "%d\n", camera_get_cam_type()); 
}

static ssize_t store_get_cam_type(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{	
	return size;
}
static DEVICE_ATTR(get_cam_type, 0664, show_get_cam_type, store_get_cam_type);
#endif

#ifdef WWC2_HUB_SET
static ssize_t show_hub_set(struct device *dev,struct device_attribute *attr, char *buf)
{
    return 0;
}

static ssize_t store_hub_set(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int val = 0;

	if (1 != sscanf(buf, "%d", &val)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}

	usb_mode_swith_by_id(val?1:0);
	return size;
}
static DEVICE_ATTR(hub_set, 0664, show_hub_set, store_hub_set);
#endif

#ifdef CONFIG_WWC2_VIDEO_RECORD
static int capture_debug_flag = 0;
static ssize_t show_capture_debug(struct device *dev,struct device_attribute *attr, char *buf)
{
    return sprintf(buf , "%d\n", capture_debug_flag);
}

static ssize_t store_capture_debug(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int val;

	if (1 != sscanf(buf, "%d", &val)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}

	capture_debug_flag = val;

	return size;
}
static DEVICE_ATTR(capture_debug, 0664, show_capture_debug, store_capture_debug);
#endif

#ifdef WWC2_CAMERA_CHANNEL_SWITCH
static ssize_t show_camera_channel_switch(struct device *dev,struct device_attribute *attr, char *buf)
{
    return 0;
}

static ssize_t store_camera_channel_switch(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int val;

	if (1 != sscanf(buf, "%d", &val)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}

	camera_src_switch_by_user(val);

	return size;
}
static DEVICE_ATTR(camera_channel_switch, 0664, show_camera_channel_switch, store_camera_channel_switch);
#endif

extern int camera_work_status;
static ssize_t show_camera_work_status(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf , "%x\n", camera_work_status);
}

static DEVICE_ATTR(camera_work_status, 0444, show_camera_work_status, NULL);

#ifdef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
static ssize_t show_audio_channel(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf , "%d\n", gNavi_audio);
}

static ssize_t store_audio_channel(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int channel = 0;

	if (1 != sscanf(buf, "%d", &channel)) {
		return -1;
	}
    gNavi_audio = channel;
    navi_audio_pin_set(gNavi_audio?1:0);  

	return size;
}
static DEVICE_ATTR(audio_channel, 0664, show_audio_channel, store_audio_channel);

static struct wwc2_point_info point_info;
static ssize_t show_virtual_touch_report(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf , "%d %d %d %d\n",point_info.status, point_info.x, point_info.y, point_info.id);
}

static ssize_t store_virtual_touch_report(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	if (4 != sscanf(buf, "%d %d %d %d", &point_info.status, &point_info.x, &point_info.y, &point_info.id)) {
		return -1;
	}

	wwc2_tpd_report(&point_info);

	return size;
}
static DEVICE_ATTR(virtual_touch_report, 0664, show_virtual_touch_report, store_virtual_touch_report);
#endif

#ifdef WWC2_AUX_TYPE_USER_SET
static ssize_t show_aux_type(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf , "%d\n", aux_type);
}
static ssize_t store_aux_type(struct device* dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int val;
	if (1 != sscanf(buf, "%d", &val)) {
		printk("<liuxin> sscanf fail\n");
		return -1;
	}
	aux_type = val;
	return size;
}
static DEVICE_ATTR(aux_type, 0664, show_aux_type, store_aux_type);
#endif

static struct device_attribute *wwc_gpio_ctrl_attr_list[] =
{
    &dev_attr_video_switch,
    &dev_attr_ctp_switch,
    &dev_attr_bls_ctrl,
    &dev_attr_cam_mode,
    &dev_attr_sig_check,
#ifndef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
    &dev_attr_gtp_update,
#endif
    &dev_attr_usb_speed,
    &dev_attr_vcom_pwm_duty,
    &dev_attr_avdd_pwm_duty,
#ifdef WWC2_CAM_MIRROR_SET
	&dev_attr_cam_mirror,
#endif
#ifdef WWC2_CAM_PARAMETER_SET
	&dev_attr_cam_params,
#endif
#ifdef WWC2_CAM_RATE_SET
	&dev_attr_cam_rate,
#endif
#ifdef WWC2_360_CAM_SET
	&dev_attr_360_camtype,
#endif
#ifdef WWC2_CAM_TYPE_GET
	&dev_attr_get_cam_type,
#endif
    &dev_attr_host_device_switch,
#ifdef WWC2_HUB_SET
    &dev_attr_hub_set,
#endif
#ifdef CONFIG_WWC2_VIDEO_RECORD
	&dev_attr_capture_debug,
#endif
#ifdef WWC2_CAMERA_CHANNEL_SWITCH
	&dev_attr_camera_channel_switch,
#endif
	&dev_attr_camera_work_status,
#ifdef CONFIG_WWC2_VIRTUAL_TOUCH_SUPPORT
    &dev_attr_audio_channel,
	&dev_attr_virtual_touch_report,
#endif
#ifdef WWC2_AUX_TYPE_USER_SET
	&dev_attr_aux_type,
#endif
};

static int gpio_ctrl_create_attr(struct device *dev) 
{
	int idx,err = 0;
	int num = (int)(sizeof(wwc_gpio_ctrl_attr_list)/sizeof(wwc_gpio_ctrl_attr_list[0]));

	WWC_GPIO_CTRL_INFO("%s", __func__);
	if(!dev) {
		return -EINVAL;
	}	

	for(idx = 0; idx < num; idx++) {
		if((err = device_create_file(dev, wwc_gpio_ctrl_attr_list[idx]))) {            
			WWC_GPIO_CTRL_INFO("device_create_file (%s) = %d\n", wwc_gpio_ctrl_attr_list[idx]->attr.name, err);        
			break;
		}
	}

	return err;
}

static int gpio_ctrl_delete_attr(struct device *dev)
{
	int idx,err = 0;
	int num = (int)(sizeof(wwc_gpio_ctrl_attr_list)/sizeof(wwc_gpio_ctrl_attr_list[0]));

    	WWC_GPIO_CTRL_INFO("%s", __func__);

	if (!dev) {
		return -EINVAL;
	}

	for (idx = 0; idx < num; idx++) {
		device_remove_file(dev, wwc_gpio_ctrl_attr_list[idx]);
	}	

	return err;
}

static long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = 0;

	WWC_GPIO_CTRL_INFO(" gpio_ioctl\n");

	switch (cmd) {
	case GPIO_SET_ENABLE:
		WWC_GPIO_CTRL_INFO(" GPIO_SET_ENABLE\n");
		break;

	case GPIO_SET_DISABLE:
		WWC_GPIO_CTRL_INFO(" GPIO_SET_ENABLE");
		break;

	default:
		{
			WWC_GPIO_CTRL_INFO(" gpio_ioctl: default\n");
			return -EINVAL;
		}
		break;
	}
	return err;
}


static ssize_t gpio_read(struct file *file,  char __user *buf, size_t count, loff_t *ppos)
{
	return count;
}

static ssize_t gpio_write(struct file *file,  const char __user *buf, size_t count, loff_t *ppos)
{
	return count;		
}

static int gpio_open(struct inode *inode, struct file *file)
{
	return 0;  
}

static int gpio_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &wwc2_gpio_async_queue);
}

static int gpio_release(struct inode *inode, struct file *file)
{
	gpio_fasync(-1, file, 0);
	return 0;
}

void wwc2_gpio_sigio(void)
{
	if(wwc2_gpio_async_queue)
		kill_fasync(&wwc2_gpio_async_queue, SIGIO, POLL_IN);
}

static const struct file_operations gpio_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = gpio_ioctl,
	.open = gpio_open,
	.release = gpio_release,
	.read = gpio_read,
	.write = gpio_write,
	.fasync = gpio_fasync,
};

/* ======================================================================== */
/* Driver interface */
/* ======================================================================== */
static int gpio_probe(struct platform_device *dev)
{
	int ret = 0, err = 0;
	long ww_dts_gpio_state = 0;

	WWC_GPIO_CTRL_INFO(" [gpio_probe] start ~");

	/* repo call DTS gpio module, if not necessary, invoke nothing */
	ww_dts_gpio_state = ww_dts_gpio_init(dev);
	if (ww_dts_gpio_state != 0)
		WWC_GPIO_CTRL_INFO("retrieve ww GPIO DTS failed.\n");
	else
	{
#ifdef WWC2_HUB_SET
		usb_mode_swith_by_id(0);
#endif
	}

	//Register ChrDev 
	ret = alloc_chrdev_region(&gpio_devno, 0, 1, GPIO_CTRL_DEVNAME);
	if (ret) {
		WWC_GPIO_CTRL_INFO(" [gpio_probe] alloc_chrdev_region fail: %d ~", ret);
		goto gpio_probe_error;
	} else {
		WWC_GPIO_CTRL_INFO(" [gpio_probe] major: %d, minor: %d ~", MAJOR(gpio_devno),
		     MINOR(gpio_devno));
	}
	cdev_init(&gpio_cdev, &gpio_fops);
	gpio_cdev.owner = THIS_MODULE;
	err = cdev_add(&gpio_cdev, gpio_devno, 1);
	if (err) {
		WWC_GPIO_CTRL_INFO(" [gpio_probe] cdev_add fail: %d ~", err);
		goto gpio_probe_error;
	}

	//Create Node:  sys/class/gpiodrv/gpio_ctrl/BTpower or  ....
	gpio_class = class_create(THIS_MODULE, "gpiodrv");
	if (IS_ERR(gpio_class)) {
		WWC_GPIO_CTRL_INFO(" [gpio_probe] Unable to create class, err = %d ~",
		     (int)PTR_ERR(gpio_class));
		goto gpio_probe_error;
	}

	gpio_device = device_create(gpio_class, NULL, gpio_devno, NULL, GPIO_CTRL_DEVNAME);
	if (NULL == gpio_device) {
		WWC_GPIO_CTRL_INFO(" [gpio_probe] device_create fail ~");
		goto gpio_probe_error;
	}
	
	
	if(gpio_ctrl_create_attr(gpio_device) != 0) {
		WWC_GPIO_CTRL_ERROR("unable to create attributes!!\n");
		gpio_ctrl_delete_attr(gpio_device);
		goto gpio_probe_error;
	}


	WWC_GPIO_CTRL_INFO(" [gpio_probe] Done ~");
	return 0;

gpio_probe_error:
	if (err == 0)
		cdev_del(&gpio_cdev);
	if (ret == 0)
		unregister_chrdev_region(gpio_devno, 1);
	return -1;
}

static int gpio_remove(struct platform_device *dev)
{
	WWC_GPIO_CTRL_INFO(" gpio_remove start\n");

	cdev_del(&gpio_cdev);
	unregister_chrdev_region(gpio_devno, 1);
	device_destroy(gpio_class, gpio_devno);
	class_destroy(gpio_class);
	gpio_ctrl_delete_attr(gpio_device);
	WWC_GPIO_CTRL_INFO(" gpio_remove Done ~");
	return 0;
}

static void gpio_shutdown(struct platform_device *dev)
{
	WWC_GPIO_CTRL_INFO(" [gpio_shutdown] start\n");
}

static const struct of_device_id mtkfb_of_ids[] = {
	{.compatible = "mediatek,wwc2_gpio",},
	{}
};

static struct platform_driver gpio_platform_driver = {
	.probe = gpio_probe,
	.remove = gpio_remove,
	.shutdown = gpio_shutdown,
	.driver = {
		   .name = GPIO_CTRL_DEVNAME,
		   .owner = THIS_MODULE,
		   .of_match_table = mtkfb_of_ids,
		   },
};

static int __init gpio_ctrl_init(void)
{
	int ret = 0;

	WWC_GPIO_CTRL_INFO(" gpio_init ");
	ret = platform_driver_register(&gpio_platform_driver);
	if (ret) {
		WWC_GPIO_CTRL_INFO(" gpio_init platform_driver_register fail ~");
		return ret;
	}

	WWC_GPIO_CTRL_INFO(" gpio_init done! ~");
	return ret;
}

static void __exit gpio_ctrl_exit(void)
{
	WWC_GPIO_CTRL_INFO(" gpio_exit start ~");
	platform_driver_unregister(&gpio_platform_driver);
	WWC_GPIO_CTRL_INFO(" gpio_exit done! ~");
}

module_init(gpio_ctrl_init);
module_exit(gpio_ctrl_exit);
MODULE_DESCRIPTION("wwc2 pinctrl driver");
MODULE_AUTHOR("wwc2 <fjie@waterworld.com.cn>");
MODULE_LICENSE("GPL");

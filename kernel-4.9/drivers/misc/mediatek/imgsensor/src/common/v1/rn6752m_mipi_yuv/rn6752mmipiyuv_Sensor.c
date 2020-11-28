/*****************************************************************************
 *
 * Filename:
 * ---------
 *	rn6752mmipiyuv_Sensor.c
 *
 * Project:
 * --------
 *	ALPS
 *
 * Description:
 * ------------
 *	Source code of RN6752M YUV to MIPI IC driver
 *
 ****************************************************************************/
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/types.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "kd_camera_typedef.h"

#include "rn6752mmipiyuv_Sensor.h"
#include <linux/kthread.h> 
#include "imgsensor_hw.h"
#include "imgsensor.h"

#ifdef SIGNAL_CHECK_THREAD
static struct task_struct *signal_check_thread = NULL;
#endif

static DEFINE_MUTEX(rn6752m_i2c_mutex);

/*************************************************************************
* DEFINITION
*************************************************************************/
#define RN6752M_PREFIX "[RN6752M]"
#define RN6752M_LOG(format, args...)    printk(RN6752M_PREFIX "[%d][%s]" format "\n", __LINE__, __FUNCTION__, ##args)

#define RN6752M_IIC_ADDR							(0x58)
#define RN6752M_IIC_SPEED						(100)
#define RN6752M_IIC_CHECK_CNT					(2)
#define RN6752M_REG_ADDR_CHIP_ID_MSB				(0xFE)
#define RN6752M_REG_ADDR_CHIP_ID_LSB				(0xFD)

#define RN6752M_WIDTH_CUT						(0)
#define RN6752M_HEIGHT_CUT						(0)

static enum INPUT_SIZE camera_video_size = SIZE_1280X720;
static enum INPUT_SIZE aux_video_size = SIZE_1280X720;
static kal_uint8 rn6752m_mipi_lane = SENSOR_MIPI_4_LANE;

extern int g_camera_sig_check;
extern int g_aux_sig_check;
extern int g_user_select_360_camtype;
extern int g_camera_isAHDcam;
extern int g_aux_isAHDcam;
extern int input_src;

static struct SENSOR_WINSIZE_INFO_STRUCT rn6752m_imgsensor_winsize_info[10] = {
	{1920, 1080, 0, 0, 1920, 1080, 1920, 1080, 0, 0, 1920, 1080, 0, 0, 1920, 1080},	/* FHD */
	{1280, 720, 0, 0, 1280, 720, 1280, 720, 0, 0, 1280, 720, 0, 0, 1280, 720},	/* HD */
	{720, 240, 0, 0, 720, 240, 720, 240, 0, 0, 720, 240, 0, 0, 720, 240},	/* CVBS NTSC */
	{720, 288, 0, 0, 720, 288, 720, 288, 0, 0, 720, 288, 0, 0, 720, 288},	/* CVBS PAL */
};				/*custom2 */

/*************************************************************************
* FUNCTION
*	rn6752m_iic_read
*
* DESCRIPTION
*	This function read data from CMOS sensor through I2C.
*
* PARAMETERS
*	addr: the 16bit address of register
*
* RETURNS
*	8bit data read through I2C
*
* LOCAL AFFECTED
*
*************************************************************************/
static UINT16 rn6752m_iic_read(UINT8 addr)
{
	UINT8 in_buff[1] = {0xFF};
	UINT8 out_buff[1];
	int ret;

	out_buff[0] = addr & 0xFF;

	ret = iReadRegI2C((UINT8*)out_buff , (UINT16)sizeof(out_buff),
			(UINT8 *)in_buff, (UINT16)sizeof(in_buff), RN6752M_IIC_ADDR);
	//RN6752M_LOG("IIC read[0x%02X:0x%02X] return:%d", out_buff[0], in_buff[0], ret);

	return in_buff[0];
}

/*************************************************************************
* FUNCTION
*	rn6752m_iic_write
*
* DESCRIPTION
*	This function wirte data to CMOS sensor through I2C
*
* PARAMETERS
*	addr: the 16bit address of register
*	para: the 8bit value of register
*
* RETURNS
*	None
*
* LOCAL AFFECTED
*
*************************************************************************/
static void rn6752m_iic_write(UINT8 addr, UINT8 para)
{
	UINT8 out_buff[2];
	int ret;

    out_buff[0] = addr & 0xFF;
    out_buff[1] = para & 0xFF;

    ret = iWriteRegI2C((UINT8 *)out_buff , (UINT16)sizeof(out_buff), RN6752M_IIC_ADDR);
}

static void rn6752m_pre_initial(bool is_used_26m_clk)
{
	UINT8 rom_byte1,rom_byte2,rom_byte3,rom_byte4,rom_byte5,rom_byte6;

	rn6752m_iic_write(0xE1, 0x80);
	rn6752m_iic_write(0xFA, 0x81);
	
	rom_byte1 = rn6752m_iic_read(0xFB);
	rom_byte2 = rn6752m_iic_read(0xFB);
	rom_byte3 = rn6752m_iic_read(0xFB);
	rom_byte4 = rn6752m_iic_read(0xFB);
	rom_byte5 = rn6752m_iic_read(0xFB);
	rom_byte6 = rn6752m_iic_read(0xFB);

	// config. decoder accroding to rom_byte5 and rom_byte6
	if ((rom_byte6 == 0x00) && (rom_byte5 == 0x00)) {
		rn6752m_iic_write(0xEF, 0xAA);  
		rn6752m_iic_write(0xE7, 0xFF);
		rn6752m_iic_write(0xFF, 0x09);
		rn6752m_iic_write(0x03, 0x0C);
		rn6752m_iic_write(0xFF, 0x0B);
		rn6752m_iic_write(0x03, 0x0C);
	}
	else if (((rom_byte6 == 0x34) && (rom_byte5 == 0xA9)) ||
         ((rom_byte6 == 0x2C) && (rom_byte5 == 0xA8))) {
		rn6752m_iic_write(0xEF, 0xAA);  
		rn6752m_iic_write(0xE7, 0xFF);
		rn6752m_iic_write(0xFC, 0x60);
		rn6752m_iic_write(0xFF, 0x09);
		rn6752m_iic_write(0x03, 0x18);
		rn6752m_iic_write(0xFF, 0x0B);
		rn6752m_iic_write(0x03, 0x18);
	}
	else {
		rn6752m_iic_write(0xEF, 0xAA);  
		rn6752m_iic_write(0xFC, 0x60);
		rn6752m_iic_write(0xFF, 0x09);
		rn6752m_iic_write(0x03, 0x18);
		rn6752m_iic_write(0xFF, 0x0B);
		rn6752m_iic_write(0x03, 0x18);	
	}

	if(is_used_26m_clk)
	{
		rn6752m_iic_write(0xD2, 0x85);
		rn6752m_iic_write(0xD6, 0x37);
		rn6752m_iic_write(0xD8, 0x18);
		mdelay(100);
	}

}

static void rn6752m_1920x1080p25_init(kal_uint8 lane_num)
{
	rn6752m_iic_write(0x81, 0x01);// turn on video decoder0
	rn6752m_iic_write(0xDF, 0xFE);// enable ch0 as HD format

	//ch0
	rn6752m_iic_write(0xF0, 0xC0);// 144MHz output
	rn6752m_iic_write(0xFF, 0x00);// switch to ch0 (default; optional)
	rn6752m_iic_write(0x00, 0x20);// internal use*
	rn6752m_iic_write(0x06, 0x08);// internal use*
	rn6752m_iic_write(0x07, 0x63);// HD format
	rn6752m_iic_write(0x2A, 0x01);// filter control
	rn6752m_iic_write(0x3A, 0x20);// Insert Channel ID in SAV/EAV code
	rn6752m_iic_write(0x3F, 0x10);// channel ID
	rn6752m_iic_write(0x4C, 0x37);// equalizer
	rn6752m_iic_write(0x4F, 0x03);// sync control
	rn6752m_iic_write(0x50, 0x03);// 1080p resolution
	rn6752m_iic_write(0x56, 0x02);// BT 144M mode
	rn6752m_iic_write(0x5F, 0x44);// blank level
	rn6752m_iic_write(0x63, 0xF8);// filter control
	rn6752m_iic_write(0x59, 0x00);// extended register access
	rn6752m_iic_write(0x5A, 0x48);// data for extended register
	rn6752m_iic_write(0x58, 0x01);// enable extended register write
	rn6752m_iic_write(0x59, 0x33);// extended register access
	rn6752m_iic_write(0x5A, 0x23);// data for extended register
	rn6752m_iic_write(0x58, 0x01);// enable extended register write
	rn6752m_iic_write(0x51, 0xF4);// scale factor1
	rn6752m_iic_write(0x52, 0x29);// scale factor2
	rn6752m_iic_write(0x53, 0x15);// scale factor3
	rn6752m_iic_write(0x5B, 0x01);// H-scaling control
	rn6752m_iic_write(0x5E, 0x0F);// enable H-scaling control
	rn6752m_iic_write(0x6A, 0x87);// H-scaling control
	rn6752m_iic_write(0x1A, 0x83);// NOBlue
	rn6752m_iic_write(0x28, 0x92);// cropping
	rn6752m_iic_write(0x03, 0x80);// saturation
	rn6752m_iic_write(0x04, 0x80);// hue
	rn6752m_iic_write(0x05, 0x04);// sharpness
	rn6752m_iic_write(0x57, 0x23);// black/white stretch
	rn6752m_iic_write(0x68, 0x00);// coring

	rn6752m_iic_write(0x81, 0x01);// turn on video decoder

	// mipi link1
	rn6752m_iic_write(0xFF, 0x09); // switch to mipi tx1
	rn6752m_iic_write(0x00, 0x03); // enable bias
	rn6752m_iic_write(0x02, 0x01); // lane0 clock mode enable
	rn6752m_iic_write(0x04, 0x04); // TD*11 for lane0; TD*10 for clock
	rn6752m_iic_write(0x05, 0x31); // TD*13 for lane3; TD*12 for lane2
	rn6752m_iic_write(0x06, 0x02); // TC*1 for lane1
	rn6752m_iic_write(0xFF, 0x08); // switch to mipi csi1
	rn6752m_iic_write(0x04, 0x03); // csi1 and tx1 reset
	rn6752m_iic_write(0x6C, 0x11); // disable ch output; turn on ch0
	rn6752m_iic_write(0x06, (lane_num == SENSOR_MIPI_4_LANE)?0x7C:0x4c);// 0x7c 4 lanes 0x4c 2 lanes
	rn6752m_iic_write(0x21, 0x01); // enable hs clock
	rn6752m_iic_write(0x78, 0xC0);// Y/C counts for ch0
	rn6752m_iic_write(0x79, 0x03); // Y/C counts for ch0
	rn6752m_iic_write(0x6C, 0x00);// disable ch output
	rn6752m_iic_write(0x04, 0x00); // csi1 and tx1 reset finish
	// 0x20, 0xAA, // invert clock phase
	// 0x07, 0x05, // enable non-clock

	// mipi link3
	rn6752m_iic_write(0xFF, 0x0A); // switch to mipi csi3
	rn6752m_iic_write(0x6C, 0x10);// disable ch output; turn off ch0~3

	//rn6752m_iic_write(0xFF, 0x00);
	//rn6752m_iic_write(0x00, 0x60);
}

static void rn6752m_1280x720p25_init(kal_uint8 lane_num)
{
	rn6752m_iic_write(0x81, 0x01);// turn on video decoder
	rn6752m_iic_write(0xDF, 0xFE);// enable HD format

	// ch0
	rn6752m_iic_write(0xFF, 0x00);// switch to ch0 (default; optional)
	rn6752m_iic_write(0x00, 0x20);// internal use*
	rn6752m_iic_write(0x06, 0x08);// internal use*
	rn6752m_iic_write(0x07, 0x63);// HD format
	rn6752m_iic_write(0x2A, 0x01);// filter control
	rn6752m_iic_write(0x3A, 0x20);// Insert Channel ID in SAV/EAV code
	rn6752m_iic_write(0x3F, 0x10);// channel ID
	rn6752m_iic_write(0x4C, 0x37);// equalizer
	rn6752m_iic_write(0x4F, 0x03);// sync control
	rn6752m_iic_write(0x50, 0x02);// 720p resolution
	rn6752m_iic_write(0x56, 0x01);// BT 72M mode
	rn6752m_iic_write(0x5F, 0x40);// blank level
	rn6752m_iic_write(0x63, 0xF5);// filter control
	rn6752m_iic_write(0x59, 0x00);// extended register access
	rn6752m_iic_write(0x5A, 0x42);// data for extended register
	rn6752m_iic_write(0x58, 0x01);// enable extended register write
	rn6752m_iic_write(0x59, 0x33);// extended register access
	rn6752m_iic_write(0x5A, 0x23);// data for extended register
	rn6752m_iic_write(0x58, 0x01);// enable extended register write
	rn6752m_iic_write(0x51, 0xE1);// scale factor1
	rn6752m_iic_write(0x52, 0x88);// scale factor2
	rn6752m_iic_write(0x53, 0x12);// scale factor3
	rn6752m_iic_write(0x5B, 0x07);// H-scaling control
	rn6752m_iic_write(0x5E, 0x0B);// enable H-scaling control
	rn6752m_iic_write(0x6A, 0x82);// H-scaling control
	rn6752m_iic_write(0x1A, 0x83);// NOBlue
	rn6752m_iic_write(0x28, 0x92);// cropping
	rn6752m_iic_write(0x03, 0x80);// saturation
	rn6752m_iic_write(0x04, 0x80);// hue
	rn6752m_iic_write(0x05, 0x04);// sharpness
	rn6752m_iic_write(0x57, 0x23);// black/white stretch
	rn6752m_iic_write(0x68, 0x32);// coring


	rn6752m_iic_write(0x81, 0x01);// turn on video decoder

	// mipi link1
	rn6752m_iic_write(0xFF, 0x09);// switch to mipi tx1
	rn6752m_iic_write(0x00, 0x03);// enable bias
	rn6752m_iic_write(0x02, 0x01);// lane0 clock mode enable
	rn6752m_iic_write(0x04, 0x04);// TD*11 for lane0; TD*10 for clock
	rn6752m_iic_write(0x05, 0x31);// TD*13 for lane3; TD*12 for lane2
	rn6752m_iic_write(0x06, 0x02);// TC*1 for lane1
	rn6752m_iic_write(0xFF, 0x08);// switch to mipi csi1
	rn6752m_iic_write(0x04, 0x03);// csi1 and tx1 reset
	rn6752m_iic_write(0x6C, 0x11);// disable ch output; turn on ch0
	rn6752m_iic_write(0x06, (lane_num == SENSOR_MIPI_4_LANE)?0x7C:0x4c);// 0x7c 4 lanes 0x4c 2 lanes
	rn6752m_iic_write(0x21, 0x01);// enable hs clock
	rn6752m_iic_write(0x78, 0x80);// Y/C counts for ch0
	rn6752m_iic_write(0x79, 0x02);// Y/C counts for ch0
	rn6752m_iic_write(0x6C, 0x00);// disable ch output
	rn6752m_iic_write(0x04, 0x00);// csi1 and tx1 reset finish
	//rn6752m_iic_write(0x20, 0xAA);// invert hs clock

	// mipi link3
	rn6752m_iic_write(0xFF, 0x0A);// switch to mipi csi3
	rn6752m_iic_write(0x6C, 0x10);// disable ch output; turn off ch0~3

	//rn6752m_iic_write(0xFF, 0x00);
	//rn6752m_iic_write(0x00, 0x60);
}

static void rn6752m_720x480p60_init(kal_uint8 lane_num)
{
	rn6752m_iic_write(0x81, 0x01);// turn on video decoder0
	rn6752m_iic_write(0xA3, 0x04);
	rn6752m_iic_write(0xDF, 0x0F);// enable cvbs format
	rn6752m_iic_write(0x88, 0x00);
	rn6752m_iic_write(0xF6, 0x00);
	rn6752m_iic_write(0xFF, 0x00);// switch to ch0 (default; optional)
	rn6752m_iic_write(0x00, 0x00);// internal use*
	rn6752m_iic_write(0x06, 0x08);// internal use*
	rn6752m_iic_write(0x07, 0x63);// HD format
	rn6752m_iic_write(0x2A, 0x81);// filter control
	rn6752m_iic_write(0x3A, 0x20);// Insert Channel ID in SAV/EAV code
	rn6752m_iic_write(0x3F, 0x10);// channel ID
	rn6752m_iic_write(0x4C, 0x37);// equalizer
	rn6752m_iic_write(0x4F, 0x00);// sync control
	rn6752m_iic_write(0x50, 0x00);// 720p resolution
	rn6752m_iic_write(0x56, 0x01);// BT 72M mode
	rn6752m_iic_write(0x5F, 0x00);// blank level
	rn6752m_iic_write(0x63, 0x75);// filter control
	rn6752m_iic_write(0x59, 0x00);// extended register access
	rn6752m_iic_write(0x5A, 0x00);// data for extended register
	rn6752m_iic_write(0x58, 0x01);// enable extended register write
	rn6752m_iic_write(0x59, 0x33);// extended register access
	rn6752m_iic_write(0x5A, 0x02);// data for extended register
	rn6752m_iic_write(0x58, 0x01);// enable extended register write
	rn6752m_iic_write(0x5B, 0x00);// H-scaling control
	rn6752m_iic_write(0x5E, 0x01);// enable H-scaling control
	rn6752m_iic_write(0x6A, 0x00);// H-scaling control
	rn6752m_iic_write(0x1A, 0x83);// NOBlue
	rn6752m_iic_write(0x28, 0x92);// cropping
	//rn6752m_iic_write(0x20, 0x24);
	//rn6752m_iic_write(0x23, 0x11);
	//rn6752m_iic_write(0x24, 0x05);
	//rn6752m_iic_write(0x25, 0x11);
	//rn6752m_iic_write(0x26, 0x00);
	//rn6752m_iic_write(0x42, 0x00);
	rn6752m_iic_write(0x03, 0x80);// saturation
	rn6752m_iic_write(0x04, 0x80);// hue
	rn6752m_iic_write(0x05, 0x03);// sharpness
	rn6752m_iic_write(0x57, 0x20);// black/white stretch
	rn6752m_iic_write(0x68, 0x32);// coring
	rn6752m_iic_write(0x37, 0x33);
	rn6752m_iic_write(0x61, 0x6C);

	rn6752m_iic_write(0x81, 0x01);// turn on video decoder

	rn6752m_iic_write(0xFF, 0x09);
	rn6752m_iic_write(0x00, 0x03);
	rn6752m_iic_write(0x02, 0x01);
	rn6752m_iic_write(0x04, 0x04);
	rn6752m_iic_write(0x05, 0x31);
	rn6752m_iic_write(0x06, 0x02);
	rn6752m_iic_write(0xFF, 0x08);
	rn6752m_iic_write(0x04, 0x03);
	rn6752m_iic_write(0x6C, 0x11);
	rn6752m_iic_write(0x06, (lane_num == SENSOR_MIPI_4_LANE)?0x7C:0x4c);// 0x7c 4 lanes 0x4c 2 lanes
	rn6752m_iic_write(0x07, 0x05);
	rn6752m_iic_write(0x21, 0x01);
	rn6752m_iic_write(0x78, 0x68);
	rn6752m_iic_write(0x79, 0x01);
	rn6752m_iic_write(0x6C, 0x00);// disable ch output
	rn6752m_iic_write(0x04, 0x00);

	rn6752m_iic_write(0xFF, 0x0A);
	rn6752m_iic_write(0x6C, 0x10);

	//rn6752m_iic_write(0xFF, 0x00);
	//rn6752m_iic_write(0x00, 0x60);
}
static void rn6752m_720x576p50_init(kal_uint8 lane_num)
{
	rn6752m_iic_write(0x81, 0x01);// turn on video decoder
	rn6752m_iic_write(0xA3, 0x04);
	rn6752m_iic_write(0xDF, 0x0F);// enable CVBS format

	rn6752m_iic_write(0x88, 0x00);
	rn6752m_iic_write(0xF6, 0x00);

	// ch0
	rn6752m_iic_write(0xFF, 0x00);// switch to ch0 (default; optional)
	rn6752m_iic_write(0x00, 0x00);// internal use*
	rn6752m_iic_write(0x06, 0x08);// internal use*
	rn6752m_iic_write(0x07, 0x62);// HD format
	rn6752m_iic_write(0x2A, 0x81);// filter control
	rn6752m_iic_write(0x3A, 0x20);// Insert Channel ID in SAV/EAV code
	rn6752m_iic_write(0x3F, 0x10);// channel ID
	rn6752m_iic_write(0x4C, 0x37);// equalizer
	rn6752m_iic_write(0x4F, 0x00);// sync control
	rn6752m_iic_write(0x50, 0x00);// 720p resolution
	rn6752m_iic_write(0x56, 0x01);// 72M mode
	rn6752m_iic_write(0x5F, 0x00);// blank level
	rn6752m_iic_write(0x63, 0x75);// filter control
	rn6752m_iic_write(0x59, 0x00);// extended register access
	rn6752m_iic_write(0x5A, 0x00);// data for extended register
	rn6752m_iic_write(0x58, 0x01);// enable extended register write
	rn6752m_iic_write(0x59, 0x33);// extended register access
	rn6752m_iic_write(0x5A, 0x02);// data for extended register
	rn6752m_iic_write(0x58, 0x01);// enable extended register write
	rn6752m_iic_write(0x5B, 0x00);// H-scaling control
	rn6752m_iic_write(0x5E, 0x01);// enable H-scaling control
	rn6752m_iic_write(0x6A, 0x00);// H-scaling control
	rn6752m_iic_write(0x1A, 0x83);// NOBlue
	rn6752m_iic_write(0x28, 0x92);// cropping
	//rn6752m_iic_write(0x20, 0x24);
	//rn6752m_iic_write(0x23, 0x17);
	//rn6752m_iic_write(0x24, 0x37);
	//rn6752m_iic_write(0x25, 0x17);
	//rn6752m_iic_write(0x26, 0x00);
	//rn6752m_iic_write(0x42, 0x00);
	rn6752m_iic_write(0x03, 0x80);// saturation
	rn6752m_iic_write(0x04, 0x80);// hue
	rn6752m_iic_write(0x05, 0x03);// sharpness
	rn6752m_iic_write(0x57, 0x20);// black/white stretch
	rn6752m_iic_write(0x68, 0x32);// coring
	rn6752m_iic_write(0x37, 0x33);
	rn6752m_iic_write(0x61, 0x6C);

	rn6752m_iic_write(0x81, 0x01);

	// mipi link1
	rn6752m_iic_write(0xFF, 0x09);// switch to mipi tx1
	rn6752m_iic_write(0x00, 0x03);// enable bias
	rn6752m_iic_write(0x02, 0x01);// lane0 clock mode enable
	rn6752m_iic_write(0x04, 0x04);// TD*11 for lane0; TD*10 for clock
	rn6752m_iic_write(0x05, 0x31);// TD*13 for lane3; TD*12 for lane2
	rn6752m_iic_write(0x06, 0x02);// TC*1 for lane1
	rn6752m_iic_write(0xFF, 0x08);// switch to mipi csi1
	rn6752m_iic_write(0x04, 0x03);// csi1 and tx1 reset
	rn6752m_iic_write(0x6C, 0x11);// disable ch output; turn on ch0
	rn6752m_iic_write(0x06, (lane_num == SENSOR_MIPI_4_LANE)?0x7C:0x4c);// 0x7c 4 lanes 0x4c 2 lanes
	rn6752m_iic_write(0x07, 0x05);// enable non-clock
	rn6752m_iic_write(0x21, 0x01);// enable hs clock
	rn6752m_iic_write(0x78, 0x68);// Y/C counts for ch0
	rn6752m_iic_write(0x79, 0x01);// Y/C counts for ch0
	rn6752m_iic_write(0x6C, 0x00);// disable ch output
	rn6752m_iic_write(0x04, 0x00);// csi1 and tx1 reset finish

	// mipi link3
	rn6752m_iic_write(0xFF, 0x0A);// switch to mipi csi3
	rn6752m_iic_write(0x6C, 0x10);// disable ch output; turn off ch0~3

	//rn6752m_iic_write(0xFF, 0x00);
	//rn6752m_iic_write(0x00, 0x60);
}

static void rn6752m_color_bar_test(enum INPUT_SIZE size)
{
	return;

	rn6752m_iic_write(0xFF, 0x00);
	rn6752m_iic_write(0x00, 0x60);
}

static void rn6752m_src_switch(enum INPUT_PORT src)
{
	mutex_lock(&rn6752m_i2c_mutex);
	rn6752m_iic_write(0xff,0x00);
	rn6752m_iic_write(0xD3,(src == SRC_CAMERA)?0x00:0x01);
	mutex_unlock(&rn6752m_i2c_mutex);
}


extern enum IMGSENSOR_SENSOR_IDX sensor_id_flag;
extern enum SENSOR_ID custom_sensor_id;
static UINT32 rn6752m_get_sensor_id(UINT32 *p_sensor_id)
{
	volatile signed char i;

	RN6752M_LOG("sensor_id_flag = %d",sensor_id_flag);
	if(sensor_id_flag != IMGSENSOR_SENSOR_IDX_MAIN)
	{
		*p_sensor_id = 0xFFFFFFFF;
		return ERROR_SENSOR_CONNECT_FAIL;
	}

	*p_sensor_id = RN6752M_SENSOR_ID;
	//return ERROR_NONE;

	// Read sensor ID to check IIC is OK
	for (i = 0; i < RN6752M_IIC_CHECK_CNT; i++)
	{
		*p_sensor_id = ((rn6752m_iic_read(RN6752M_REG_ADDR_CHIP_ID_MSB) << 8)
			| rn6752m_iic_read(RN6752M_REG_ADDR_CHIP_ID_LSB));
		RN6752M_LOG("Sensor ID = 0x%04x", *p_sensor_id);
		if (RN6752M_SENSOR_ID == *p_sensor_id) // RN6752M_SENSOR_ID = 0x0501
		{
			break;
		}
	}
	if (RN6752M_IIC_CHECK_CNT == i) // RN6752M_SENSOR_ID != 0x0501
	{
		RN6752M_LOG("RN6752M IC Read ID Failed!!!");
		*p_sensor_id = 0xFFFFFFFF;
		return ERROR_SENSOR_CONNECT_FAIL;
	}
	custom_sensor_id = RN6752M;
	RN6752M_LOG("RN6752M IC Read ID OK");
    return ERROR_NONE;
}


static UINT32 rn6752m_preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *p_image_window,
		MSDK_SENSOR_CONFIG_STRUCT *p_sensor_config_data)
{
	RN6752M_LOG("Start preview.");

#ifdef SIGNAL_CHECK_THREAD
	if(!IS_ERR_OR_NULL(signal_check_thread))
	{
		kthread_stop(signal_check_thread);	
        	signal_check_thread = NULL;
	}
#endif

	return ERROR_NONE;
}

static UINT32 rn6752m_preview_ext(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *p_image_window,
		MSDK_SENSOR_CONFIG_STRUCT *p_sensor_config_data)
{
	RN6752M_LOG("Close preview.");
#ifdef SIGNAL_CHECK_THREAD
	if(!IS_ERR_OR_NULL(signal_check_thread))
	{
		kthread_stop(signal_check_thread);	
        	signal_check_thread = NULL;
	}
#endif
	g_camera_sig_check = 0;
	g_aux_sig_check = 0;
	return ERROR_NONE;
}

/*************************************************************************
* FUNCTION
*	rn6752m_open
*
* DESCRIPTION
*	This function initialize the registers of CMOS sensor
*
* PARAMETERS
*	None
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static enum INPUT_SIZE rn6752m_get_user_set_size(int type)
{
	enum INPUT_SIZE user_size = SIZE_1280X720;

	switch(type)
	{
		case AHD_25FPS:
		case AHD_30FPS:
		case MAIN_HD_SUB_NTSC:
		case MAIN_HD_SUB_PAL:
		case MAIN_HD_SUB_HD:
		case MAIN_HD_SUB_FHD:
			user_size =  SIZE_1280X720;
			break;
		case CVBS_NTSC:
		case MAIN_NTSC_SUB_NTSC:
		case MAIN_NTSC_SUB_PAL:
		case MAIN_NTSC_SUB_HD:
		case MAIN_NTSC_SUB_FHD:
			user_size =  SIZE_720X240;
			break;
		case CVBS_PAL:
		case MAIN_PAL_SUB_NTSC:
		case MAIN_PAL_SUB_PAL:
		case MAIN_PAL_SUB_HD:
		case MAIN_PAL_SUB_FHD:
			user_size = SIZE_720X288;
			break;
		case FHD_25FPS:
		case FHD_30FPS:
		case MAIN_FHD_SUB_NTSC:
		case MAIN_FHD_SUB_PAL:
		case MAIN_FHD_SUB_HD:
		case MAIN_FHD_SUB_FHD:
			user_size = SIZE_1920X1080;
			break;
		case TVI_INPUT:
		case CVI_INPUT:
		case PVI_INPUT:
		default:
			break;
	}

	return user_size;
}

static void rn6752m_sensor_init(enum INPUT_SIZE video_size, kal_uint8 lane_num)
{
	printk("%s video_size = %d\n",__func__, video_size);
	mutex_lock(&rn6752m_i2c_mutex);
	rn6752m_pre_initial(1);
	switch(video_size)
	{
		case SIZE_720X240:
			rn6752m_720x480p60_init(lane_num);
			break;
		case SIZE_720X288:
			rn6752m_720x576p50_init(lane_num);
			break;
		case SIZE_1280X720:
			rn6752m_1280x720p25_init(lane_num);
			break;
		case SIZE_1920X1080:
			rn6752m_1920x1080p25_init(lane_num);
			break;
		default:
			break;
	}

	rn6752m_color_bar_test(video_size);
	mutex_unlock(&rn6752m_i2c_mutex);
}

static enum CAMERA_STATUS rn6752m_video_detect(void)
{
	UINT8 type = 0;

	mutex_lock(&rn6752m_i2c_mutex);
	rn6752m_iic_write(0xFF,0x00);
	type = rn6752m_iic_read(0x00);
	mutex_unlock(&rn6752m_i2c_mutex);

	if((type & 0x10) == 0x00)
		return CAMERA_PLUG_IN;
	else
		return CAMERA_PLUG_OUT;
}

#ifdef SIGNAL_CHECK_THREAD
static int sig_check_handler(void *unused);
#endif

static enum INPUT_SIZE rn6752m_get_video_type(void)
{
	int count = 0;
	UINT8 type = 0,last_type = 0;
	enum INPUT_SIZE size = SIZE_1280X720;

	msleep(2000);
	mutex_lock(&rn6752m_i2c_mutex);
	rn6752m_iic_write(0xFF,0x00);
	type = rn6752m_iic_read(0x00);
	mutex_unlock(&rn6752m_i2c_mutex);

	while(count < 5)
	{
		msleep(100);
		mutex_lock(&rn6752m_i2c_mutex);
		rn6752m_iic_write(0xFF,0x00);
		last_type = rn6752m_iic_read(0x00);
		mutex_unlock(&rn6752m_i2c_mutex);

		if(type != last_type)
		{
			type = last_type;
			count = 0;
		}
		else
			count++;
	}
	count = 0;

	if((type & 0x10) == 0x00)
	{
		if(type & 0x01)
		{
			if((type & 0xE0) == 0x00)
			{
				size = SIZE_720X240;
			}
		}
		else
		{
			if((type & 0xE0) == 0x00)
			{
				size = SIZE_720X288;
			}
			else if((type & 0xE0) == 0x20)
			{
				size = SIZE_1280X720;
			}
			else if((type & 0xE0) == 0x40)
			{
				size = SIZE_1920X1080;
			}
		}
	}
	else
		size = SIZE_1280X720;

	return size;
}

extern int camera_work_status;
static UINT32 rn6752m_open(void)
{
	UINT32 ret;
	UINT32 sensor_id;
	UINT32 retry_time = 0;
	enum INPUT_PORT src = SRC_CAMERA;
	static bool first_open = true;

	if(first_open)
	{
		first_open = false;
		return ERROR_NONE;
	}

	camera_work_status |= 0x01;
	if((input_src & 0x01) == 0x00)
		src = SRC_CAMERA;
	else
		src = SRC_AUX;

	RN6752M_LOG("Open.");
	RN6752M_LOG("sensor_id_flag = %d",sensor_id_flag);

	ret = rn6752m_get_sensor_id(&sensor_id);
	if (ERROR_NONE != ret) {
		return ret;
	}

	rn6752m_src_switch(src);
	if(src == SRC_CAMERA)
	{
		if(g_user_select_360_camtype == TVI_INPUT || g_user_select_360_camtype == CVI_INPUT || g_user_select_360_camtype == PVI_INPUT)
		{
			while(retry_time < 20)
			{
				if(rn6752m_video_detect() == CAMERA_PLUG_IN)
					break;
				retry_time++;
				mdelay(100);
			}

			camera_video_size = rn6752m_get_video_type();
		}
		else
		{
			camera_video_size = rn6752m_get_user_set_size(g_user_select_360_camtype);
		}
		g_camera_isAHDcam = camera_video_size;
		rn6752m_sensor_init(camera_video_size, rn6752m_mipi_lane);
	}
	else
	{
#if 0
		while(retry_time < 20)
		{
			if(rn6752m_video_detect() == CAMERA_PLUG_IN)
				break;
			retry_time++;
			mdelay(100);
		}

		aux_video_size = rn6752m_get_video_type();
#else
		aux_video_size = rn6752m_get_user_set_size(g_user_select_360_camtype);
#endif
		g_aux_isAHDcam = aux_video_size;
		rn6752m_sensor_init(aux_video_size, rn6752m_mipi_lane);
	}

#ifdef SIGNAL_CHECK_THREAD
	if(IS_ERR_OR_NULL(signal_check_thread))
	{
            signal_check_thread = kthread_run(sig_check_handler, 0, "signal check");
            if (IS_ERR(signal_check_thread))
	            RN6752M_LOG( "sig check failed to create kernel thread:");
	}
#else
	g_camera_sig_check = 1;
	g_aux_sig_check = 1;
#endif

	return ERROR_NONE;
}

static UINT32 rn6752m_get_height(void)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	UINT32 height = 720;

	if((input_src & 0x01) == 0x00)
		size = camera_video_size;
	else
		size = aux_video_size;

	switch(size)
	{
		case SIZE_720X480:
		case SIZE_720X240:
		case SIZE_960X480:
    			height = 240;
			break;
		case SIZE_720X576:
		case SIZE_720X288:
		case SIZE_960X576:
    			height = 288;
			break;
		case SIZE_1280X720:
			height = 720;
			break;
		case SIZE_1280X960:
    			height = 960;
			break;
		case SIZE_1920X1080:
    			height = 1080;
			break;
		default:
			break;
	}
	return height - RN6752M_HEIGHT_CUT;
}

static UINT32 rn6752m_get_width(void)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	UINT32 width = 1280;

	if((input_src & 0x01) == 0x00)
		size = camera_video_size;
	else
		size = aux_video_size;

	switch(size)
	{
		case SIZE_720X480:
		case SIZE_720X240:
		case SIZE_720X576:
		case SIZE_720X288:
			width = 720; 
			break;
		case SIZE_960X480:
		case SIZE_960X576:
			width = 960; 
			break;
		case SIZE_1280X720:
		case SIZE_1280X960:
			width = 1280;
			break;
		case SIZE_1920X1080:
    			width = 1920; 
			break;
		default:
			break;
	}
	return width - RN6752M_WIDTH_CUT;
}

static UINT32 rn6752m_get_info(enum MSDK_SCENARIO_ID_ENUM scenario_id,
		MSDK_SENSOR_INFO_STRUCT *p_sensor_info,
		MSDK_SENSOR_CONFIG_STRUCT *p_sensor_config_data)
{
    RN6752M_LOG("Get info. scenario ID = %d", scenario_id);

    p_sensor_info->SensorResetActiveHigh = FALSE;
    p_sensor_info->SensorResetDelayCount = 5;

    p_sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;
    p_sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW; /* not use */
    p_sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW; // inverse with datasheet
    p_sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
    p_sensor_info->SensorInterruptDelayLines = 4; /* not use */

    p_sensor_info->SensroInterfaceType = SENSOR_INTERFACE_TYPE_MIPI;
    p_sensor_info->MIPIsensorType = MIPI_OPHY_NCSI2; // MIPI_OPHY_NCSI2
    p_sensor_info->SettleDelayMode = MIPI_SETTLEDELAY_AUTO;
    p_sensor_info->SensorOutputDataFormat = SENSOR_OUTPUT_FORMAT_UYVY;

    p_sensor_info->CaptureDelayFrame = 2;
    p_sensor_info->PreviewDelayFrame = 2;
    p_sensor_info->VideoDelayFrame = 2;
    p_sensor_info->HighSpeedVideoDelayFrame = 2;
    p_sensor_info->SlimVideoDelayFrame = 2;

    p_sensor_info->SensorMasterClockSwitch = 0; /* not use */
    p_sensor_info->SensorDrivingCurrent = ISP_DRIVING_6MA;

    p_sensor_info->AEShutDelayFrame = 0;          /* The frame of setting shutter default 0 for TG int */
    p_sensor_info->AESensorGainDelayFrame = 0;    /* The frame of setting sensor gain */
    p_sensor_info->AEISPGainDelayFrame = 2;
    p_sensor_info->IHDR_Support = 0;
    p_sensor_info->IHDR_LE_FirstLine = 0;
    p_sensor_info->SensorModeNum = 5;

    p_sensor_info->SensorMIPILaneNumber = rn6752m_mipi_lane;
    p_sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0; 
    p_sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = 85;
    p_sensor_info->SensorClockFreq = 26;
    p_sensor_info->SensorClockDividCount = 3;	/* not use */
    p_sensor_info->SensorClockRisingCount = 0;
    p_sensor_info->SensorClockFallingCount = 2;	/* not use */
    p_sensor_info->SensorPixelClockCount = 3;	/* not use */
    p_sensor_info->SensorDataLatchCount = 2;	/* not use */

    p_sensor_info->SensorWidthSampling = 0;		// 0 is default 1x
    p_sensor_info->SensorHightSampling = 0;		// 0 is default 1x
    p_sensor_info->SensorPacketECCOrder = 1;
	
    p_sensor_info->SensorGrabStartX = RN6752M_WIDTH_CUT;
    p_sensor_info->SensorGrabStartY = RN6752M_HEIGHT_CUT;

	return ERROR_NONE;
}

static UINT32 rn6752m_get_resolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *p_sensor_resolution)
{
	RN6752M_LOG("camera_video_size = %d aux_video_size = %d",camera_video_size,aux_video_size);

	p_sensor_resolution->SensorFullWidth = rn6752m_get_width(); 
	p_sensor_resolution->SensorFullHeight = rn6752m_get_height();
	p_sensor_resolution->SensorPreviewWidth = rn6752m_get_width();
	p_sensor_resolution->SensorPreviewHeight = rn6752m_get_height();
	p_sensor_resolution->SensorVideoWidth = rn6752m_get_width();
	p_sensor_resolution->SensorVideoHeight = rn6752m_get_height();

	return ERROR_NONE;
}


static kal_uint32 rn6752m_get_default_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 *framerate)
{
	enum INPUT_SIZE size = SIZE_1280X720;

	if((input_src & 0x01) == 0x00)
		size = camera_video_size;
	else
		size = aux_video_size;

	switch(size)
	{
		case SIZE_720X240:
			*framerate = 600;
			break;
		case SIZE_720X288:
			*framerate = 500;
			break;
		case SIZE_1280X720:
		case SIZE_1920X1080:
			*framerate = 250;
			break;
		default:
			*framerate = 250;
			break;
	}
	return ERROR_NONE;
}

static kal_uint32 rn6752m_streaming_control(kal_bool enable)
{
	RN6752M_LOG("enable = %d",enable);
	mutex_lock(&rn6752m_i2c_mutex);
	if(enable)
	{
		// Start mipi
		rn6752m_iic_write(0xFF, 0x08);// switch to mipi csi1
		rn6752m_iic_write(0x6C, 0x01);// enable ch output
		
	}
	else
	{
		// Stop mipi
		rn6752m_iic_write(0xFF, 0x08);// switch to mipi csi1
		rn6752m_iic_write(0x6C, 0x00);// disable ch output
		
	}
	mutex_unlock(&rn6752m_i2c_mutex);

	return ERROR_NONE;
}

extern enum IMGSENSOR_RETURN imgsensor_hw_power_custom(enum IMGSENSOR_SENSOR_IDX sensor_idx, enum IMGSENSOR_HW_POWER_STATUS pwr_status, char *curr_sensor_name);
static void rn6752m_get_camera_type(UINT32 *ptype)
{
	enum CAM_STATUS_TYPE ret = CAM_NO_SIGNAL;
	UINT8 type= 0;
	UINT32 sensor_id;
	enum IMGSENSOR_RETURN power_ret = IMGSENSOR_RETURN_ERROR;

	imgsensor_hw_power_custom(IMGSENSOR_SENSOR_IDX_MAIN, IMGSENSOR_HW_POWER_STATUS_OFF, SENSOR_DRVNAME_RN6752M_MIPI_YUV);
	mdelay(100);
	power_ret = imgsensor_hw_power_custom(IMGSENSOR_SENSOR_IDX_MAIN, IMGSENSOR_HW_POWER_STATUS_ON, SENSOR_DRVNAME_RN6752M_MIPI_YUV);
	if(power_ret == IMGSENSOR_RETURN_ERROR)
	{
		RN6752M_LOG("power on fail");
		ret = CAM_HARDWARE_ERROR;
		goto exit;
	}

	rn6752m_get_sensor_id(&sensor_id);
	if(sensor_id != RN6752M_SENSOR_ID)
	{
		RN6752M_LOG("read sensor id fail");
		ret = CAM_HARDWARE_ERROR;
		goto exit;
	}

	if((input_src & 0x01) == 0x00)
		rn6752m_src_switch(SRC_CAMERA);
	else
		rn6752m_src_switch(SRC_AUX);;

	mdelay(2000);
	rn6752m_iic_write(0xFF,0x00);
	type = rn6752m_iic_read(0x00);
	if((type & 0x10) == 0x00)
	{
		if(type & 0x01)
		{
			if((type & 0xE0) == 0x00)
				ret = CVBS_NTSC_60HZ;
			else
				ret = CAM_TYPE_UNKNOW;
		}
		else
		{
			if((type & 0xE0) == 0x00)
				ret = CVBS_PAL_50HZ;
			else if((type & 0xE0) == 0x20)
				ret = AHD_720P_25HZ;
			else if((type & 0xE0) == 0x40)
				ret = AHD_1080P_25HZ;
			else
				ret= CAM_TYPE_UNKNOW;
		}
	}
	else
		ret = CAM_NO_SIGNAL;

	RN6752M_LOG("cam video type is %d",ret);
exit:
	*ptype = (UINT32)ret;
}

static void rn6752m_camera_param_set(MSDK_SENSOR_REG_INFO_STRUCT *pdata)
{
	mutex_lock(&rn6752m_i2c_mutex);
	rn6752m_iic_write(0xFF, 0x00);
	rn6752m_iic_write((UINT8)(pdata->RegAddr), (UINT8)(pdata->RegData));
	mutex_unlock(&rn6752m_i2c_mutex);
}

static void rn6752m_signal_check(unsigned long long *signal)
{
#ifdef SIGNAL_CHECK_THREAD
	if(rn6752m_video_detect())
		*signal = 1;
	else
		*signal = 0;
#else
	*signal = 1;
#endif
}

static UINT32 rn6752m_feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
		UINT8 *p_feature_data, UINT32 *p_feature_para_len)
{
	UINT16 *p_feature_return_para_16 = (UINT16 *)p_feature_data;
	UINT32 *p_feature_return_para_32 = (UINT32 *)p_feature_data;
	MSDK_SENSOR_REG_INFO_STRUCT *p_sensor_reg_data = (MSDK_SENSOR_REG_INFO_STRUCT *)p_feature_data;
 	unsigned long long *feature_data = (unsigned long long *)p_feature_data;
	struct SENSOR_WINSIZE_INFO_STRUCT *wininfo;
	enum INPUT_SIZE video_src = SIZE_1280X720;

	//RN6752M_LOG("[IN]Feature control. feature_id = %d", feature_id);
	
	switch (feature_id) {
	case SENSOR_FEATURE_GET_RESOLUTION:
		*p_feature_return_para_16++ = rn6752m_get_width();
		*p_feature_return_para_16 = rn6752m_get_height();
		*p_feature_para_len = 4;
		break;
	case SENSOR_FEATURE_GET_PERIOD:
		*p_feature_return_para_16++ = 3960; // Line length
		*p_feature_return_para_16 = 750; // Frame length
		*p_feature_para_len = 4;
		break;
	case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
		*p_feature_return_para_32 = 75000000; //
		*p_feature_para_len = 4;
		break;
	case SENSOR_FEATURE_SET_REGISTER:
		rn6752m_iic_write(p_sensor_reg_data->RegAddr, p_sensor_reg_data->RegData);
		break;
	case SENSOR_FEATURE_GET_REGISTER:
		p_sensor_reg_data->RegData = rn6752m_iic_read(p_sensor_reg_data->RegAddr);
		break;
	case SENSOR_FEATURE_SET_VIDEO_MODE:
		break;
	case SENSOR_FEATURE_SET_YUV_CMD:
		break;
	case SENSOR_FEATURE_CHECK_SENSOR_ID:
		rn6752m_get_sensor_id(p_feature_return_para_32);
		break;
	case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
		rn6752m_get_default_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM) *feature_data,
						  (MUINT32 *) (uintptr_t) (*(feature_data + 1)));
		break;
	case SENSOR_FEATURE_GET_CROP_INFO:
		wininfo = (struct SENSOR_WINSIZE_INFO_STRUCT *) (uintptr_t) (*(feature_data + 1));
		if((input_src & 0x01) == 0x00)
			video_src = camera_video_size;
		else
			video_src = aux_video_size;
		switch(video_src)
		{
			case SIZE_1920X1080:
				memcpy((void *)wininfo, (void *)&rn6752m_imgsensor_winsize_info[0],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
				break;
			case SIZE_1280X720:
				memcpy((void *)wininfo, (void *)&rn6752m_imgsensor_winsize_info[1],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
				break;
			case SIZE_720X240:
				memcpy((void *)wininfo, (void *)&rn6752m_imgsensor_winsize_info[2],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
				break;
			case SIZE_720X288:
				memcpy((void *)wininfo, (void *)&rn6752m_imgsensor_winsize_info[3],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
				break;
			default:
				break;
		}
		break;
	case SENSOR_FEATURE_GET_PIXEL_RATE:
		if((input_src & 0x01) == 0x00)
			video_src = camera_video_size;
		else
			video_src = aux_video_size;
		switch(video_src)
		{
			case SIZE_1920X1080:
				*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 120000000/(1920-80)*1920;
				break;
			case SIZE_1280X720:
				*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 75000000/(1280-80)*1280;
				break;
			case SIZE_720X240:
				*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 75000000/(720-80)*720;
				break;
			case SIZE_720X288:
				*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 75000000/(720-80)*720;
				break;
			default:
				break;
		}
		break;
	case SENSOR_FEATURE_GET_MIPI_PIXEL_RATE:
		if((input_src & 0x01) == 0x00)
			video_src = camera_video_size;
		else
			video_src = aux_video_size;
		switch(video_src)
		{
			case SIZE_1920X1080:
				*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 120000000;
				break;
			case SIZE_1280X720:
				*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 75000000;
				break;
			case SIZE_720X240:
				*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 75000000;
				break;
			case SIZE_720X288:
				*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 75000000;
				break;
			default:
				break;
		}
		break;
	case SENSOR_FEATURE_SET_STREAMING_SUSPEND:
		rn6752m_streaming_control(KAL_FALSE);
		break;
	case SENSOR_FEATURE_SET_STREAMING_RESUME:
		rn6752m_streaming_control(KAL_TRUE);
		break;
	case SENSOR_FEATURE_SET_CAMERA_SRC_SWITCH:
		rn6752m_src_switch(*feature_data?SRC_AUX:SRC_CAMERA);
		break;
	case SENSOR_FEATURE_GET_CAMERA_TYPE:
		rn6752m_get_camera_type(p_feature_return_para_32);
		break;
	case SENSOR_FEATURE_SET_CAMERA_PARA:
		rn6752m_camera_param_set(p_sensor_reg_data);
		break;
	case SENSOR_FEATURE_SET_SIGNAL_CHECK:
		rn6752m_signal_check(feature_data);
		break;
	default:
		break;
	}
	//RN6752M_LOG("[OUT]Feature control. feature_id = %d", feature_id);
	return ERROR_NONE;
}

static UINT32 rn6752m_control(enum MSDK_SCENARIO_ID_ENUM scenario_id,
		MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *p_image_window,
		MSDK_SENSOR_CONFIG_STRUCT *p_sensor_config_data)
{
    RN6752M_LOG("Control. scenario ID = %d", scenario_id);
    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            rn6752m_preview(p_image_window, p_sensor_config_data);
            break;
        default:
            rn6752m_preview(p_image_window, p_sensor_config_data);
            break;
    }
    return ERROR_NONE;
}

/*************************************************************************
* FUNCTION
*	rn6752m_close
*
* DESCRIPTION
*
*
* PARAMETERS
*	None
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static UINT32 rn6752m_close(void)
{
	RN6752M_LOG("Close.");
	camera_work_status &= 0x10;
	rn6752m_preview_ext(NULL, NULL);
	return ERROR_NONE;
}

static struct SENSOR_FUNCTION_STRUCT rn6752m_sensor_func = {
	rn6752m_open,
	rn6752m_get_info,
	rn6752m_get_resolution,
	rn6752m_feature_control,
	rn6752m_control,
	rn6752m_close
};

UINT32 RN6752M_MIPI_YUV_SensorInit(struct SENSOR_FUNCTION_STRUCT **pfFunc)
{
	if (NULL != pfFunc)
		*pfFunc = &rn6752m_sensor_func;
	return ERROR_NONE;
}

void get_rn6752m_default_param(int *brgt, int *sat, int *cont)
{
	*brgt = 128;
	*sat = 128;
	*cont = 128;
}


extern struct IMGSENSOR *pgimgsensor;

#ifdef SIGNAL_CHECK_THREAD
static int sig_check_handler(void *unused)
{
	struct IMGSENSOR_SENSOR *psensor = &pgimgsensor->sensor[IMGSENSOR_SENSOR_IDX_MAIN];
	unsigned long long feature_data = 0;

	do{
		imgsensor_sensor_feature_control(psensor, SENSOR_FEATURE_SET_SIGNAL_CHECK, (MUINT8 *)&feature_data, (MUINT32 *)sizeof(unsigned long long));
		g_camera_sig_check = (int)feature_data;
		g_aux_sig_check = g_camera_sig_check;
		msleep(100);
	}while(!kthread_should_stop());

	return 0;
}
#endif

void rn6752m_param_set(int mode, int para)
{
	struct IMGSENSOR_SENSOR *psensor = &pgimgsensor->sensor[IMGSENSOR_SENSOR_IDX_MAIN];
	MSDK_SENSOR_REG_INFO_STRUCT sensorReg = {.RegAddr = 0xff, .RegData = 0x00};

	if(para < 0 || para > 255)
		return;

	if(g_camera_sig_check == 0 &&  g_aux_sig_check == 0) // no signal can not set param
		return;

	switch(mode)
	{
		case 1://contrast
			sensorReg.RegAddr = 0x02;
			sensorReg.RegData = para;
			break;
		case 2://Brit
			sensorReg.RegAddr = 0x01;
			sensorReg.RegData = para - 128;
			break;
		case 3://satu
			sensorReg.RegAddr = 0x03;
			sensorReg.RegData = para;
			break;
		case 4://hue
			sensorReg.RegAddr = 0x04;
			sensorReg.RegData = para;
			break;
		case 5://enhL
			sensorReg.RegAddr = 0x05;
			sensorReg.RegData = para;
			break;
		default:
			break;
	}
	imgsensor_sensor_feature_control(psensor, SENSOR_FEATURE_SET_CAMERA_PARA, (MUINT8 *)&sensorReg, (MUINT32 *)sizeof(MSDK_SENSOR_REG_INFO_STRUCT));
}

int rn6752m_get_cam_type(void)
{
	int type;

	struct IMGSENSOR_SENSOR *psensor = &pgimgsensor->sensor[IMGSENSOR_SENSOR_IDX_MAIN];
	unsigned long long feature_data = 0;

#ifdef SIGNAL_CHECK_THREAD
	if(!IS_ERR_OR_NULL(signal_check_thread))
	{
		kthread_stop(signal_check_thread);	
        	signal_check_thread = NULL;
	}
	mdelay(100);
#endif

	imgsensor_sensor_feature_control(psensor, SENSOR_FEATURE_GET_CAMERA_TYPE, (MUINT8 *)&feature_data, (MUINT32 *)sizeof(unsigned long long));
	type = (int)feature_data;

	return type;
}

void rn6752m_src_switch_by_user(int channel)
{
	struct IMGSENSOR_SENSOR *psensor = &pgimgsensor->sensor[IMGSENSOR_SENSOR_IDX_MAIN];
	unsigned long long feature_data = 0;

	RN6752M_LOG("bbl-debug channel = %d",channel);

	if(channel)	//src_aux
		feature_data = 1;

	imgsensor_sensor_feature_control(psensor, SENSOR_FEATURE_SET_CAMERA_SRC_SWITCH, (MUINT8 *)&feature_data, (MUINT32 *)sizeof(unsigned long long));
}


/* Copyright Statement:
*
* This software/firmware and related documentation ("MediaTek Software") are
* protected under relevant copyright laws. The information contained herein
* is confidential and proprietary to MediaTek Inc. and/or its licensors.
* Without the prior written permission of MediaTek inc. and/or its licensors,
* any reproduction, modification, use or disclosure of MediaTek Software,
* and information contained herein, in whole or in part, shall be strictly prohibited.
*/
/* MediaTek Inc. (C) 2010. All rights reserved.
*
* BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
* THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
* RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
* AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
* NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
* SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
* SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
* THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
* THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
* CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
* SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
* STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
* CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
* AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
* OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
* MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* The following software/firmware and/or related documentation ("MediaTek Software")
* have been modified by MediaTek Inc. All revisions are subject to any receiver's
* applicable license agreements with MediaTek Inc.
*/

/*****************************************************************************
*  Copyright Statement:
*  --------------------
*  This software is protected by Copyright and the information contained
*  herein is confidential. The software may not be copied and the information
*  contained herein may not be used or disclosed except with the written
*  permission of MediaTek Inc. (C) 2008
*
*  BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
*  THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
*  RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO BUYER ON
*  AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
*  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
*  NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
*  SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
*  SUPPLIED WITH THE MEDIATEK SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH
*  THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO
*  NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S
*  SPECIFICATION OR TO CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
*  BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
*  LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
*  AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
*  OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY BUYER TO
*  MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
*  THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
*  WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT OF
*  LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING THEREOF AND
*  RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN FRANCISCO, CA, UNDER
*  THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE (ICC).
*
*****************************************************************************/
#include "lcm_drv.h"
// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH     (1280)
#define FRAME_HEIGHT    (480)
#define LCM_DSI_CMD_MODE						0
static struct LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    		(lcm_util.set_reset_pin((v)))
#define UDELAY(n) 			(lcm_util.udelay(n))
#define MDELAY(n) 			(lcm_util.mdelay(n))

#ifdef BUILD_LK
#define LCD_DEBUG(fmt, args...) printf(fmt, ##args)		
#else
#define LCD_DEBUG(fmt, args...) printk(fmt, ##args)
#endif

#define WRITE_REGISTER_UINT32_WW(reg, val)	((*(volatile uint32_t *const)(reg)) = (val))
#define OUTREG32_WW(x, y)	WRITE_REGISTER_UINT32_WW((uint32_t *)((void *)(x)), (uint32_t)(y))
// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------
extern int icn6202_i2c_write_bytes_kernel(unsigned char reg, unsigned char data);
extern int icn6202_i2c_read_bytes_kernel(unsigned char reg);
extern void lcm_vdd_control(bool en);

extern void lcm_reset_pin_set(bool en);
extern void usb_mode_swith_by_id(bool en);
extern void arm_sleep_status_indicator(bool en);
extern void set_usb_to_device_mode(void);
extern void wwc2_gpio_sigio(void);

// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	//LCD_DEBUG("\t\t 9881c [lcm_set_util_funcs]\n");

	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}


static void lcm_get_params(struct LCM_PARAMS *params)
{
	//LCD_DEBUG("\t\t hx8394f [lcm_get_params]\n");

	memset(params, 0, sizeof(struct LCM_PARAMS));

	params->type   = LCM_TYPE_DSI;

	params->width  = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	// enable tearing-free
	//params->dbi.te_mode 			= LCM_DBI_TE_MODE_VSYNC_ONLY;
	//params->dbi.te_edge_polarity		= LCM_POLARITY_RISING;
	//params->dbi.te_mode 				= LCM_DBI_TE_MODE_DISABLED;

#if (LCM_DSI_CMD_MODE)
	params->dsi.mode   = CMD_MODE;
#else
	params->dsi.mode   = SYNC_PULSE_VDO_MODE;
#endif

	// DSI
	/* Command mode setting */
	params->dsi.LANE_NUM				= LCM_FOUR_LANE;
	//The following defined the fomat for data coming from LCD engine.
	//params->dsi.data_format.color_order	= LCM_COLOR_ORDER_RGB;
	//params->dsi.data_format.trans_seq   		= LCM_DSI_TRANS_SEQ_MSB_FIRST;
	//params->dsi.data_format.padding     		= LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format      		= LCM_DSI_FORMAT_RGB888;

	params->dsi.cont_clock = 1; //fangjie add
	params->dsi.noncont_clock = 0; //fangjie add, not must

	// Highly depends on LCD driver capability.
	//params->dsi.packet_size=256;

	// Video mode setting	

	params->dsi.intermediat_buffer_num = 2;	
	params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;
	//params->dsi.word_count=480*3;
	//here is for esd protect by legen
	//params->dsi.noncont_clock = true;
	//params->dsi.noncont_clock_period=2;
	params->dsi.lcm_ext_te_enable=false;
	//for esd protest end by legen
	//params->dsi.word_count=FRAME_WIDTH*3;	
	params->dsi.vertical_sync_active=1;  //4
	params->dsi.vertical_backporch=40;	//16
	params->dsi.vertical_frontporch=40;
	params->dsi.vertical_active_line=FRAME_HEIGHT;

	//params->dsi.line_byte=2180;		
	params->dsi.horizontal_sync_active=13;  
	params->dsi.horizontal_backporch=60;   //50  60 
	params->dsi.horizontal_frontporch=60;  //50   200
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;	

	//params->dsi.HS_TRAIL= 7;  // 4.3406868
	//params->dsi.HS_PRPR = 4;	
	//params->dsi.CLK_TRAIL= 50;
 	//params->dsi.ssc_range = 7;/
	params->dsi.ssc_disable = 1;
 	params->dsi.PLL_CLOCK=135;
}

static void init_icn6202_registers(void)
{
#ifdef BUILD_LK
    printf("[LK/LCM] init_icn6202_registers() \n");
#else
    printk("[LCM] init_icn6202_registers() enter\n");
#endif		
//for ICN6202
	icn6202_i2c_write_bytes_kernel(0x20, 0x00);
	icn6202_i2c_write_bytes_kernel(0x21, 0xe0);
	icn6202_i2c_write_bytes_kernel(0x22, 0x15);
	icn6202_i2c_write_bytes_kernel(0x23, 0x3c);
	icn6202_i2c_write_bytes_kernel(0x24, 0x0d);
	icn6202_i2c_write_bytes_kernel(0x25, 0x3c);		
	icn6202_i2c_write_bytes_kernel(0x26, 0x00);		
	icn6202_i2c_write_bytes_kernel(0x27, 0x28);
	icn6202_i2c_write_bytes_kernel(0x28, 0x01);
	icn6202_i2c_write_bytes_kernel(0x29, 0x28);

	icn6202_i2c_write_bytes_kernel(0x34, 0x80);
	icn6202_i2c_write_bytes_kernel(0x36, 0x3c);

	icn6202_i2c_write_bytes_kernel(0xB5, 0xA0);
	icn6202_i2c_write_bytes_kernel(0x5C, 0xFF);
	icn6202_i2c_write_bytes_kernel(0x13, 0x10);
	icn6202_i2c_write_bytes_kernel(0x56, 0x90);

	icn6202_i2c_write_bytes_kernel(0x6B, 0x21);
	icn6202_i2c_write_bytes_kernel(0x69, 0x19);
	icn6202_i2c_write_bytes_kernel(0xB6, 0x20);

	icn6202_i2c_write_bytes_kernel(0x51, 0x20);
	icn6202_i2c_write_bytes_kernel(0x09, 0x10); //display on
}
extern void clkm_set(unsigned int sel, unsigned int div);		//clkm4 26M sel:0x1000000 div:0x0
static void lcm_init(void)
{
	clkm_set(0x10000, 0x0);
    //add by liuxin 6202 reset pin control ---20170908
	lcm_reset_pin_set(0);
	MDELAY(20);
	lcm_reset_pin_set(1);
	MDELAY(20);
	lcm_reset_pin_set(0);
	MDELAY(20);
    //add by liuxin 6202 reset pin control ---20170908	
//STEP3:
	init_icn6202_registers();
	MDELAY(50);
}

static void lcm_suspend(void)
{   
	lcm_reset_pin_set(1);
	set_usb_to_device_mode();
	wwc2_gpio_sigio();
}

static void lcm_resume(void)
{
    lcm_init();
	arm_sleep_status_indicator(0);

	//confirm usb swith to host,when net wake up,not acc
	usb_mode_swith_by_id(0);
}


// ---------------------------------------------------------------------------
//  Get LCM Driver Hooks
// ---------------------------------------------------------------------------
struct LCM_DRIVER car_icn6202_1280x480_lcm_drv =
{
	.name			= "car_icn6202_1280x480",
	.set_util_funcs		= lcm_set_util_funcs,
	.get_params			= lcm_get_params,
	.init					= lcm_init,
	.suspend				= lcm_suspend,
	.resume				= lcm_resume,
};

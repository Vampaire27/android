/* Copyright Statement:
*
* This software/firmware and related documentation ("MediaTek Software") are
* protected under relevant copyright laws. The information contained herein
* is confidential and proprietary to MediaTek Inc. and/or its licensors.
* Without the prior written permission of MediaTek inc. and/or its licensors,
* any reproduction, modification, use or disclosure of MediaTek Software,
* and information contained herein, in whole or in part, shall be strictly prohibited.
*/
/* MediaTek Inc. (C) 2015. All rights reserved.
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
* have been modified by MediaTek Inc. All revisions are subject to any receiver\'s
* applicable license agreements with MediaTek Inc.
*/
#ifndef BUILD_LK
#include <linux/string.h>
#else
#include <string.h>
#endif 

#include "lcm_drv.h"
// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------
#define FRAME_WIDTH  (1024)
#define FRAME_HEIGHT (600)

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------
static struct LCM_UTIL_FUNCS lcm_util = {0};

#define UDELAY(n) (lcm_util.udelay(n))
#define MDELAY(n) (lcm_util.mdelay(n))

// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)	  		lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)				lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)											lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)						lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd)											lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size)   						lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)   

#define  LCM_DSI_CMD_MODE			0
#define REGFLAG_DELAY				0xFFE
#define REGFLAG_END_OF_TABLE		0xFFF   // END OF REGISTERS MARKER

extern void lcm_reset_pin_set(bool en);
extern void lcm_avdd_set(int duty);
extern void lcm_vcom_set(int duty);
extern int get_screen_type(void);
extern void usb_mode_swith_by_id(bool en);
extern void arm_sleep_status_indicator(bool en);
extern void set_usb_to_device_mode(void);
extern void wwc2_gpio_sigio(void);

struct LCM_setting_table
{
    unsigned cmd;
    unsigned char count;
    unsigned char para_list[64];
};

static struct LCM_setting_table lcm_initialization_setting_7_BOE_TN[] = {//C_EK79007+BOE 7.0 TN (97W_G8 5)_4Lane_20160704_Aron,7_boe_tn,gScreen_type=7,avdd 9.6 vcom 3.7,原始7寸屏参数
		//{0xB2, 1, {0x10}},        
		{0x80, 1, {0x8b}},
		{0x81, 1, {0xff}},
		{0x82, 1, {0xaf}},
		{0x83, 1, {0xdf}},
		{0x84, 1, {0x97}},
		{0x85, 1, {0x9c}},
		{0x86, 1, {0xb9}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_9_BOE_TN[] = {//C_EK79007+ BOE 8.95 TN_20150724,9_boe_tn,gScreen_type=9,avdd 9.6 vcom 3.8，原始9寸屏参数
		//{0xB2, 1, {0x10}},        
		{0x80, 1, {0xfb}},
		{0x81, 1, {0xcf}},
		{0x82, 1, {0xaa}},
		{0x83, 1, {0xa7}},
		{0x84, 1, {0xeb}},
		{0x85, 1, {0x22}},
		{0x86, 1, {0xb3}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_10_BOE_TN[] = {//C_EK79007+ BOE 10.1(TN G6)_4-lane_20141212,10_boe_tn,gScreen_type=10,avdd 9.6 vcom 4.0，原始10寸屏参数
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0xbb}},
		{0x81, 1, {0x40}},
		{0x82, 1, {0x04}},
		{0x83, 1, {0xbb}},
		{0x84, 1, {0xb9}},
		{0x85, 1, {0x00}},
		{0x86, 1, {0xbb}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_7_HSD_TN[] = {//temp:7_boe_tn,gScreen_type=17
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x8b}},
		{0x81, 1, {0xff}},
		{0x82, 1, {0xaf}},
		{0x83, 1, {0xdf}},
		{0x84, 1, {0x97}},
		{0x85, 1, {0x9c}},
		{0x86, 1, {0xb9}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_9_HSD_TN[] = {//EK79007+HSD 9.0TN_2lane,gScreen_type=19,avdd 10.2 vcom 3.8
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0xBB}},
		{0x81, 1, {0xFF}},
		{0x82, 1, {0xBB}},
		{0x83, 1, {0xBB}},
		{0x84, 1, {0x87}},
		{0x85, 1, {0xC9}},
		{0x86, 1, {0xBB}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_10_HSD_TN[] = {//C_EK79007+HSD 10.1_TN_4-LANE _G2.2_LANTY20180829,10_hsd_tn,gScreen_type=20,avdd 10.2 vcom 4.2
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0xac}},
		{0x81, 1, {0xbb}},
		{0x82, 1, {0x09}},
		{0x83, 1, {0x78}},
		{0x84, 1, {0x84}},
		{0x85, 1, {0xbb}},
		{0x86, 1, {0x70}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_7_CPT_TN[] = {//C_EK79007_CPT7.0_1024_600_TN_4 LANE_G2.2，7_cpt_tn,gScreen_type=27,,avdd 9.6 vcom 3.8
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x77}},
		{0x81, 1, {0x7C}},
		{0x82, 1, {0xa8}},
		{0x83, 1, {0xfe}},
		{0x84, 1, {0xb0}},
		{0x85, 1, {0x77}},
		{0x86, 1, {0x77}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_9_CPT_TN[] = {//temp:9_boe_tn,gScreen_type=29
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0xfb}},
		{0x81, 1, {0xcf}},
		{0x82, 1, {0xaa}},
		{0x83, 1, {0xa7}},
		{0x84, 1, {0xeb}},
		{0x85, 1, {0x22}},
		{0x86, 1, {0xb3}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_10_CPT_TN[] = {//CPT 10.1+EK79007 MIPI 參數,10_cpt_tn,gScreen_type=30,avdd 9.6 vcom 3.8
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x00}},
		{0x81, 1, {0x00}},
		{0x82, 1, {0x00}},
		{0x83, 1, {0x00}},
		{0x84, 1, {0x00}},
		{0x85, 1, {0x00}},
		{0x86, 1, {0x00}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_7_IVO_TN[] = {//C_EK79007_IVO7.0_1024_600_TN_4 LANE_G2.2,7_ivo_tn,gScreen_type=37,avdd 9.6 vcom 3.8
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x77}},
		{0x81, 1, {0x7c}},
		{0x82, 1, {0xa8}},
		{0x83, 1, {0xfe}},
		{0x84, 1, {0xb0}},
		{0x85, 1, {0x77}},
		{0x86, 1, {0x77}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_9_IVO_TN[] = {//C_EK79007_IVO9.0_1024_600_TN_4 LANE_G2.2_VCOM3.8,9_ivo_tn,gScreen_type=39,avdd 9.6 vcom 3.8
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x47}},
		{0x81, 1, {0x96}},
		{0x82, 1, {0x08}},
		{0x83, 1, {0x77}},
		{0x84, 1, {0xbf}},
		{0x85, 1, {0xc6}},
		{0x86, 1, {0x70}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_10_IVO_TN[] = {//C_EK79007_IVO10.1_1024_600_TN_4 LANE內置VCOM=3.8V,10_ivo_tn,gScreen_type=40,avdd 9.6 vcom 3.8
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x47}},
		{0x81, 1, {0x40}},
		{0x82, 1, {0x09}},
		{0x83, 1, {0x77}},
		{0x84, 1, {0xaf}},
		{0x85, 1, {0x45}},
		{0x86, 1, {0x70}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_7_BOE_IPS[] = {//C_EK79007+BOE 7.0 IPS(97W) 4-lane 20150611，7_boe_ips,gScreen_type=1，avdd 9.6 vcom 3.2
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0xac}},
		{0x81, 1, {0xb8}},
		{0x82, 1, {0x09}},
		{0x83, 1, {0x78}},
		{0x84, 1, {0x7f}},
		{0x85, 1, {0xbb}},
		{0x86, 1, {0x70}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_9_BOE_IPS[] = {//temp:9_hsd_ips,gScreen_type=2
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x88}},
		{0x81, 1, {0xbc}},
		{0x82, 1, {0x38}},
		{0x83, 1, {0x83}},
		{0x84, 1, {0x89}},
		{0x85, 1, {0x88}},
		{0x86, 1, {0x25}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_10_BOE_IPS[] = {//temp:10_cpt_ips,gScreen_type=3
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x58}},
		{0x81, 1, {0x47}},
		{0x82, 1, {0xd4}},
		{0x83, 1, {0x88}},
		{0x84, 1, {0xa9}},
		{0x85, 1, {0xc3}},
		{0x86, 1, {0x82}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_7_HSD_IPS[] = {//C_EK79007AD+HSD 7.0(IPS) MIPI 4-Lane_20151127,7_hsd_ips,gScreen_type=11,avdd 9.6 vcom 3.8
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x9f}},
		{0x81, 1, {0xbc}},
		{0x82, 1, {0x18}},
		{0x83, 1, {0x88}},
		{0x84, 1, {0x4f}},
		{0x85, 1, {0xd2}},
		{0x86, 1, {0x88}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_9_HSD_IPS[] = {//temp:9_hsd_ips,gScreen_type=12
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x88}},
		{0x81, 1, {0xbc}},
		{0x82, 1, {0x38}},
		{0x83, 1, {0x83}},
		{0x84, 1, {0x89}},
		{0x85, 1, {0x88}},
		{0x86, 1, {0x25}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_10_HSD_IPS[] = {//C_EK79007AD+HSD 10.1_IPS_4lane，10_hsd_ips,gScreen_type=13,avdd 10.2 vcom 4.0
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0xB0}},
		{0x81, 1, {0xA0}},
		{0x82, 1, {0x86}},
		{0x83, 1, {0xD3}},
		{0x84, 1, {0x5B}},
		{0x85, 1, {0x2E}},
		{0x86, 1, {0xDD}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_7_CPT_IPS[] = {//C_EK79007_CPT7.0_1024_600_IPS_2 LANE_G2.2，7_cpt_ips,gScreen_type=21,avdd 9.6 vcom 3.15
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x88}},
		{0x81, 1, {0x78}},
		{0x82, 1, {0x84}},
		{0x83, 1, {0x88}},
		{0x84, 1, {0xa8}},
		{0x85, 1, {0x83}},
		{0x86, 1, {0x88}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_9_CPT_IPS[] = {//temp:9_hsd_ips,gScreen_type=22
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x88}},
		{0x81, 1, {0xbc}},
		{0x82, 1, {0x38}},
		{0x83, 1, {0x83}},
		{0x84, 1, {0x89}},
		{0x85, 1, {0x88}},
		{0x86, 1, {0x25}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_10_CPT_IPS[] = {//temp:10_cpt_ips,gScreen_type=23
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x58}},
		{0x81, 1, {0x47}},
		{0x82, 1, {0xd4}},
		{0x83, 1, {0x88}},
		{0x84, 1, {0xa9}},
		{0x85, 1, {0xc3}},
		{0x86, 1, {0x82}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_7_IVO_IPS[] = {//temp:7_boe_ips,gScreen_type=31
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0xac}},
		{0x81, 1, {0xb8}},
		{0x82, 1, {0x09}},
		{0x83, 1, {0x78}},
		{0x84, 1, {0x7f}},
		{0x85, 1, {0xbb}},
		{0x86, 1, {0x70}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_9_IVO_IPS[] = {//temp:9_hsd_ips,gScreen_type=32
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x88}},
		{0x81, 1, {0xbc}},
		{0x82, 1, {0x38}},
		{0x83, 1, {0x83}},
		{0x84, 1, {0x89}},
		{0x85, 1, {0x88}},
		{0x86, 1, {0x25}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_initialization_setting_10_IVO_IPS[] = {//temp:10_cpt_ips,gScreen_type=33
		//{0xB2, 1, {0x10}},
		{0x80, 1, {0x58}},
		{0x81, 1, {0x47}},
		{0x82, 1, {0xd4}},
		{0x83, 1, {0x88}},
		{0x84, 1, {0xa9}},
		{0x85, 1, {0xc3}},
		{0x86, 1, {0x82}},
		{REGFLAG_DELAY, 120, {}},
		{REGFLAG_DELAY, 80, {}},
		{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
    unsigned int i;

    for(i = 0; i < count; i++) {

        unsigned cmd;
        cmd = table[i].cmd;

        switch (cmd) {

            case REGFLAG_DELAY :
                MDELAY(table[i].count);
                break;

            case REGFLAG_END_OF_TABLE :
                break;

            default:
                dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
        }   
    }   

}


// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------
static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}

static void lcm_get_params(struct LCM_PARAMS *params)
{
	memset(params, 0, sizeof(struct LCM_PARAMS));
	
	params->type   = LCM_TYPE_DSI;

	params->width  = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
		
#if (LCM_DSI_CMD_MODE)
	params->dsi.mode   = CMD_MODE;
#else
	params->dsi.mode   = BURST_VDO_MODE;	//SYNC_EVENT_VDO_MODE;		//SYNC_PULSE_VDO_MODE;
#endif
	
	// DSI
	/* Command mode setting */
	// Three lane or Four lane
	params->dsi.LANE_NUM				= LCM_FOUR_LANE;
		
	//The following defined the fomat for data coming from LCD engine.
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq   = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding     = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;

	// Highly depends on LCD driver capability.
	// Not support in MT6573
	params->dsi.packet_size=256;

	// Video mode setting		
	params->dsi.intermediat_buffer_num = 0;

	params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;
	params->dsi.word_count=FRAME_WIDTH*3;
		
	params->dsi.vertical_sync_active				= 1;
	params->dsi.vertical_backporch					= 23;
	params->dsi.vertical_frontporch					= 12;
	params->dsi.vertical_active_line					= FRAME_HEIGHT; 

	params->dsi.horizontal_sync_active				= 10;
	params->dsi.horizontal_backporch				= 160;
	params->dsi.horizontal_frontporch				= 160;
	params->dsi.horizontal_active_pixel				= FRAME_WIDTH;
	
	params->dsi.esd_check_enable 		= 0;
	params->dsi.cont_clock 	= 1;
	params->dsi.ssc_disable 	= 1;
	params->dsi.PLL_CLOCK 	= 170;
		
}

static void lcm_init(void)
{
	int type = get_screen_type();
	lcm_reset_pin_set(1);
	MDELAY(30);
	lcm_reset_pin_set(0);
	MDELAY(30);
	lcm_reset_pin_set(1);
	MDELAY(120);
	printk("gScreen_type_kernel = %d\n",type);   
	switch(type) 
	{     
       case 7: 
            push_table(lcm_initialization_setting_7_BOE_TN, sizeof(lcm_initialization_setting_7_BOE_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 9: 
            push_table(lcm_initialization_setting_9_BOE_TN, sizeof(lcm_initialization_setting_9_BOE_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 10: 
            push_table(lcm_initialization_setting_10_BOE_TN, sizeof(lcm_initialization_setting_10_BOE_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 17: 
            push_table(lcm_initialization_setting_7_HSD_TN, sizeof(lcm_initialization_setting_7_HSD_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 19: 
            push_table(lcm_initialization_setting_9_HSD_TN, sizeof(lcm_initialization_setting_9_HSD_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 20: 
            push_table(lcm_initialization_setting_10_HSD_TN, sizeof(lcm_initialization_setting_10_HSD_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 27: 
            push_table(lcm_initialization_setting_7_CPT_TN, sizeof(lcm_initialization_setting_7_CPT_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 29: 
            push_table(lcm_initialization_setting_9_CPT_TN, sizeof(lcm_initialization_setting_9_CPT_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 30: 
            push_table(lcm_initialization_setting_10_CPT_TN, sizeof(lcm_initialization_setting_10_CPT_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 37: 
            push_table(lcm_initialization_setting_7_IVO_TN, sizeof(lcm_initialization_setting_7_IVO_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 39: 
            push_table(lcm_initialization_setting_9_IVO_TN, sizeof(lcm_initialization_setting_9_IVO_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 40: 
            push_table(lcm_initialization_setting_10_IVO_TN, sizeof(lcm_initialization_setting_10_IVO_TN) / sizeof(struct LCM_setting_table), 1);
            break;
        case 1: 
            push_table(lcm_initialization_setting_7_BOE_IPS, sizeof(lcm_initialization_setting_7_BOE_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 2: 
            push_table(lcm_initialization_setting_9_BOE_IPS, sizeof(lcm_initialization_setting_9_BOE_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 3: 
            push_table(lcm_initialization_setting_10_BOE_IPS, sizeof(lcm_initialization_setting_10_BOE_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 11: 
            push_table(lcm_initialization_setting_7_HSD_IPS, sizeof(lcm_initialization_setting_7_HSD_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 12: 
            push_table(lcm_initialization_setting_9_HSD_IPS, sizeof(lcm_initialization_setting_9_HSD_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 13: 
            push_table(lcm_initialization_setting_10_HSD_IPS, sizeof(lcm_initialization_setting_10_HSD_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 21: 
            push_table(lcm_initialization_setting_7_CPT_IPS, sizeof(lcm_initialization_setting_7_CPT_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 22: 
            push_table(lcm_initialization_setting_9_CPT_IPS, sizeof(lcm_initialization_setting_9_CPT_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 23: 
            push_table(lcm_initialization_setting_10_CPT_IPS, sizeof(lcm_initialization_setting_10_CPT_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 31: 
            push_table(lcm_initialization_setting_7_IVO_IPS, sizeof(lcm_initialization_setting_7_IVO_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 32: 
            push_table(lcm_initialization_setting_9_IVO_IPS, sizeof(lcm_initialization_setting_9_IVO_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        case 33: 
            push_table(lcm_initialization_setting_10_IVO_IPS, sizeof(lcm_initialization_setting_10_IVO_IPS) / sizeof(struct LCM_setting_table), 1);
            break;
        default:
            push_table(lcm_initialization_setting_10_BOE_TN, sizeof(lcm_initialization_setting_10_BOE_TN) / sizeof(struct LCM_setting_table), 1);
            break;
    }
}

static void lcm_suspend(void)
{
	lcm_reset_pin_set(0);
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

#if (LCM_DSI_CMD_MODE)
static void lcm_update(unsigned int x, unsigned int y, unsigned int width, unsigned int height)
{
	unsigned int x0 = x;
	unsigned int y0 = y;
	unsigned int x1 = x0 + width - 1;
	unsigned int y1 = y0 + height - 1;

	unsigned char x0_MSB = ((x0>>8)&0xFF);
	unsigned char x0_LSB = (x0&0xFF);
	unsigned char x1_MSB = ((x1>>8)&0xFF);
	unsigned char x1_LSB = (x1&0xFF);
	unsigned char y0_MSB = ((y0>>8)&0xFF);
	unsigned char y0_LSB = (y0&0xFF);
	unsigned char y1_MSB = ((y1>>8)&0xFF);
	unsigned char y1_LSB = (y1&0xFF);

	unsigned int data_array[16];

	data_array[0]= 0x00053902;
	data_array[1]= (x1_MSB<<24)|(x0_LSB<<16)|(x0_MSB<<8)|0x2a;
	data_array[2]= (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);
	
	data_array[0]= 0x00053902;
	data_array[1]= (y1_MSB<<24)|(y0_LSB<<16)|(y0_MSB<<8)|0x2b;
	data_array[2]= (y1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0]= 0x00290508; 				//HW bug, so need send one HS packet
	dsi_set_cmdq(data_array, 1, 1);
	
	data_array[0]= 0x002c3909;
	dsi_set_cmdq(data_array, 1, 0);
}
#endif


struct LCM_DRIVER ek79007_wsvgalnl_dsi_vdo_lcm_drv = 
{
	.name				= "EK79007_WSVGALNL_DSI_VDO",
	.set_util_funcs		= lcm_set_util_funcs,
	.get_params			= lcm_get_params,
	.init					= lcm_init,
	.suspend				= lcm_suspend,
	.resume				= lcm_resume,
#if (LCM_DSI_CMD_MODE)
	.update				= lcm_update,
#endif
};

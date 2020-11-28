/*
* Copyright(C)2014 MediaTek Inc.
* Modification based on code covered by the below mentioned copyright
* and/or permission notice(S).
*/

/*****************************************************************************
 *
 * Copyright (c) 2016 mCube, Inc.  All rights reserved.
 *
 * This source is subject to the mCube Software License.
 * This software is protected by Copyright and the information and source code
 * contained herein is confidential. The software including the source code
 * may not be copied and the information contained herein may not be used or
 * disclosed except with the written permission of mCube Inc.
 *
 * All other rights reserved.
 *
 * This code and information are provided "as is" without warranty of any
 * kind, either expressed or implied, including but not limited to the
 * implied warranties of merchantability and/or fitness for a
 * particular purpose.
 *
 * The following software/firmware and/or related documentation ("mCube Software")
 * have been modified by mCube Inc. All revisions are subject to any receiver's
 * applicable license agreements with mCube Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 *****************************************************************************/

/*****************************************************************************
 *** HEADER FILES
 *****************************************************************************/

#include "cust_acc.h"
#include "accel.h"
#include "mc34xx.h"
#include <linux/version.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>

#define MC34XX_SUPPORT_ANDROID_O
#define MC34XX_SUPPORT_INTERRUPT
/*****************************************************************************
 *** CONFIGURATION
 *****************************************************************************/
#define _MC34XX_SUPPORT_CONCURRENCY_PROTECTION_
#define C_MAX_FIR_LENGTH	(32)

/*****************************************************************************
 *** CONSTANT / DEFINITION
 *****************************************************************************/
/**************************
 *** CONFIGURATION
 **************************/
#define MC34XX_DEV_NAME						"MC34XX"
#define MC34XX_MOTION						   "MC34XX_MOTION"
#define MC34XX_DEV_DRIVER_VERSION			  "2.1.6"

/**************************
 *** COMMON
 **************************/
#define MC34XX_AXIS_X	  0
#define MC34XX_AXIS_Y	  1
#define MC34XX_AXIS_Z	  2
#define MC34XX_AXES_NUM	3
#define MC34XX_DATA_LEN	6
#define MC34XX_RESOLUTION_LOW	 		1
#define MC34XX_RESOLUTION_HIGH			2
#define MC34XX_LOW_REOLUTION_DATA_SIZE	 3
#define MC34XX_HIGH_REOLUTION_DATA_SIZE	6
#define MC34XX_INIT_SUCC	(0)
#define MC34XX_INIT_FAIL	(-1)
#define MC34XX_REGMAP_LENGTH		(75)//for mensa
//#define MC34XX_REGMAP_LENGTH	(64)
#define DEBUG_SWITCH		1
#define C_I2C_FIFO_SIZE	 8

/**************************
 *** DEBUG
 **************************/
#if DEBUG_SWITCH
    #define GSE_TAG                  "[Gsensor] "
    #define GSE_FUN(f)               printk(KERN_INFO GSE_TAG"%s\n", __FUNCTION__)
    #define GSE_ERR(fmt, args...)    printk(KERN_ERR GSE_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
    #define GSE_LOG(fmt, args...)    printk(KERN_NOTICE GSE_TAG fmt, ##args)
#else
    #define GSE_TAG
    #define GSE_FUN(f)               do {} while (0)
    #define GSE_ERR(fmt, args...)    do {} while (0)
    #define GSE_LOG(fmt, args...)    do {} while (0)
#endif

/*****************************************************************************
 *** DATA TYPE / STRUCTURE DEFINITION / ENUM
 *****************************************************************************/
enum MCUBE_TRC {
	MCUBE_TRC_FILTER  = 0x01,
	MCUBE_TRC_RAWDATA = 0x02,
	MCUBE_TRC_IOCTL   = 0x04,
	MCUBE_TRC_CALI	= 0X08,
	MCUBE_TRC_INFO	= 0X10,
	MCUBE_TRC_REGXYZ  = 0X20,
};

struct scale_factor {
	u8	whole;
	u8	fraction;
};

struct data_resolution {
	struct scale_factor	scalefactor;
	int					sensitivity;
};

struct data_filter {
	s16	raw[C_MAX_FIR_LENGTH][MC34XX_AXES_NUM];
	int	sum[MC34XX_AXES_NUM];
	int	num;
	int	idx;
};

struct mc34xx_i2c_data {
	/* ================================================ */
	struct i2c_client		  *client;
	struct acc_hw			  *hw;
	struct hwmsen_convert	   cvt;

	/* ================================================ */
	struct data_resolution	 	*reso;
	atomic_t					trace;
	atomic_t					suspend;
	atomic_t					selftest;
	atomic_t					filter;
	s16						 	cali_sw[MC34XX_AXES_NUM + 1];

	/* ================================================ */
	s16						 	offset[MC34XX_AXES_NUM + 1];
	s16						 	data[MC34XX_AXES_NUM + 1];

#ifdef MC34XX_SUPPORT_INTERRUPT
	int					irq;
	struct work_struct		eint_work;
	int 			pin;
#endif
	/* ================================================ */
};
/*****************************************************************************
 *** EXTERNAL FUNCTION
 *****************************************************************************/
/* extern struct acc_hw*	MC34XX_get_cust_acc_hw(void); */

/*****************************************************************************
 *** STATIC FUNCTION
 *****************************************************************************/
static int mc34xx_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int mc34xx_i2c_remove(struct i2c_client *client);
static int mc34xx_i2c_auto_probe(struct i2c_client *client);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
static int mc34xx_suspend(struct device *dev);
static int mc34xx_resume(struct device *dev);
#else
static int mc34xx_suspend(struct i2c_client *client, pm_message_t msg);
static int mc34xx_resume(struct i2c_client *client);
#endif

static int mc34xx_local_init(void);
static int mc34xx_remove(void);
/*****************************************************************************
 *** STATIC VARIBLE & CONTROL BLOCK DECLARATION
 *****************************************************************************/
static unsigned char	s_bResolution   = 0x00;
static unsigned char	s_bPCODE        = 0x00;
static unsigned char	s_bHWID         = 0x00;
static unsigned char	s_bMPOL         = 0x00;
static int	            s_nInitFlag     = MC34XX_INIT_FAIL;
/* Maintain  cust info here */
struct acc_hw accel_cust_mc34xx;
static struct acc_hw *hw = &accel_cust_mc34xx;

static struct acc_init_info  mc34xx_init_info = {
	.name   = MC34XX_DEV_NAME,
	.init   = mc34xx_local_init,
	.uninit = mc34xx_remove,
	};
#ifdef CONFIG_OF
static const struct of_device_id accel_of_match[] = {
	{.compatible = "mediatek,gsensor"},
	{},
};
#endif

#ifdef CONFIG_PM_SLEEP
static const struct dev_pm_ops mc34xx_i2c_pm_ops = 
{
        SET_SYSTEM_SLEEP_PM_OPS(mc34xx_suspend, mc34xx_resume)
};
#endif

static const struct i2c_device_id mc34xx_i2c_id[] = { {MC34XX_DEV_NAME, 0}, {} };
static unsigned short mc34xx_i2c_auto_probe_addr[] = { 0x4C, 0x6C, 0x4D, 0x4E, 0x4F, 0x6D, 0x6E, 0x6F };
static struct i2c_driver	mc34xx_i2c_driver = {
	.driver = {
			 .name = MC34XX_DEV_NAME,
#ifdef CONFIG_OF
			.of_match_table = accel_of_match,
#endif
#ifdef CONFIG_PM_SLEEP
			.pm = &mc34xx_i2c_pm_ops,
#endif
		},
	.probe  = mc34xx_i2c_probe,
	.remove = mc34xx_i2c_remove,
	.id_table = mc34xx_i2c_id,
};

static struct i2c_client		*mc34xx_i2c_client = NULL;
static struct mc34xx_i2c_data   *mc34xx_obj_i2c_data;
//static struct data_resolution	mc34xx_offset_resolution = { {7, 8}, 256 };
static bool	mc34xx_sensor_power;
static struct GSENSOR_VECTOR3D	gsensor_gain;
static char	selftestRes[10] = {0};
static unsigned char	s_baOTP_OffsetData[6] = { 0 };
static DEFINE_MUTEX(mc34xx_i2c_mutex);
#ifdef _MC34XX_SUPPORT_CONCURRENCY_PROTECTION_
	static struct semaphore	s_tSemaProtect;
#endif
static signed char	s_bAccuracyStatus = SENSOR_STATUS_ACCURACY_MEDIUM;
static int mc34xx_mutex_lock(void);
static void mc34xx_mutex_unlock(void);
static void mc34xx_mutex_init(void);
/*****************************************************************************
 *** MACRO
 *****************************************************************************/
#ifdef _MC34XX_SUPPORT_CONCURRENCY_PROTECTION_
static void mc34xx_mutex_init(void)
{
	sema_init(&s_tSemaProtect, 1);
}

static int mc34xx_mutex_lock(void)
{
	if (down_interruptible(&s_tSemaProtect))
			return (-ERESTARTSYS);
	return 0;
}

static void mc34xx_mutex_unlock(void)
{
	up(&s_tSemaProtect);
}
#else
	#define mc34xx_mutex_lock()				do {} while (0)
	#define mc34xx_mutex_lock()				do {} while (0)
	#define mc34xx_mutex_unlock()			  do {} while (0)
#endif

#define IS_MERAK()	((0xC0 == s_bHWID) || (0x40 == s_bHWID) || (0x20 == s_bHWID))
#define IS_MENSA()	(0xA0 == s_bHWID)

/*****************************************************************************
 *** FUNCTION
 *****************************************************************************/

/**************I2C operate API*****************************/
static int mc34xx_i2c_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
	u8 beg = addr;
	int err;
	struct i2c_msg msgs[2] = {{0}, {0} };

	mutex_lock(&mc34xx_i2c_mutex);

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &beg;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data;

	if (!client) {
		mutex_unlock(&mc34xx_i2c_mutex);
		return -EINVAL;
	} else if (len > C_I2C_FIFO_SIZE) {
		GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		mutex_unlock(&mc34xx_i2c_mutex);
		return -EINVAL;
	}
	err = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (err != 2) {
		GSE_ERR("i2c_transfer error: (%d %p %d) %d\n", addr, data, len, err);
		err = -EIO;
	} else
		err = 0;

	mutex_unlock(&mc34xx_i2c_mutex);
	return err;

}

static int mc34xx_i2c_write_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{   /*because address also occupies one byte, the maximum length for write is 7 bytes*/
	int err, idx, num;
	char buf[C_I2C_FIFO_SIZE];

	err = 0;
	mutex_lock(&mc34xx_i2c_mutex);
	if (!client) {
		mutex_unlock(&mc34xx_i2c_mutex);
		return -EINVAL;
	} else if (len >= C_I2C_FIFO_SIZE) {
		GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		mutex_unlock(&mc34xx_i2c_mutex);
		return -EINVAL;
	}

	num = 0;
	buf[num++] = addr;
	for (idx = 0; idx < len; idx++)
		buf[num++] = data[idx];

	err = i2c_master_send(client, buf, num);
	if (err < 0) {
		GSE_ERR("send command error!!\n");
		mutex_unlock(&mc34xx_i2c_mutex);
		return -EFAULT;
	}
	err = 0;

	mutex_unlock(&mc34xx_i2c_mutex);
	return err;
}

/*****************************************
*** MC34XX_ValidateSensorIC
*****************************************/
static int MC34XX_ValidateSensorIC(unsigned char *pbPCode, unsigned char *pbHwID)
{
	GSE_LOG("[%s] raw data pbHwID=%02X,pbPCode=%02X\n", __func__,*pbHwID,*pbPCode);
	
	*pbPCode = *pbPCode & 0xF1;
	*pbHwID  = *pbHwID  & 0xF0;
	
	if (0xA0 == *pbHwID) {
		if ((MC34XX_PCODE_3416 == *pbPCode) || (MC34XX_PCODE_3436 == *pbPCode))
			return MC34XX_RETCODE_SUCCESS;
	}else if((0xC0 == *pbHwID) || (0x40 == *pbHwID) || (0x20 == *pbHwID))
		{
			if ((MC34XX_PCODE_3413 == *pbPCode) || (MC34XX_PCODE_3433 == *pbPCode))
				return MC34XX_RETCODE_SUCCESS;
		}

    return MC34XX_RETCODE_ERROR_IDENTIFICATION;
}

/*****************************************
 *** MC34XX_ReadRegMap
 *****************************************/
static int MC34XX_ReadRegMap(struct i2c_client *p_i2c_client, u8 *pbUserBuf)
{
	u8	 _baData[MC34XX_REGMAP_LENGTH] = { 0 };
	int	_nIndex = 0;

	GSE_LOG("[%s]\n", __func__);

	for (_nIndex = 0; _nIndex < MC34XX_REGMAP_LENGTH; _nIndex++) {
		mc34xx_i2c_read_block(p_i2c_client, _nIndex, &_baData[_nIndex], 1);

		if (NULL != pbUserBuf)
			pbUserBuf[_nIndex] = _baData[_nIndex];

		GSE_LOG("[Gsensor] REG[0x%02X] = 0x%02X\n", _nIndex, _baData[_nIndex]);
	}

	//mcube_write_log_data(p_i2c_client, _baData);

	return 0;
}

/*****************************************
 *** MC34XX_ReadData
 *****************************************/
static int	MC34XX_ReadData(struct i2c_client *pt_i2c_client, s16 waData[MC34XX_AXES_NUM])
{
	u8	_baData[MC34XX_DATA_LEN] = { 0 };

	if (MC34XX_RESOLUTION_LOW == s_bResolution)
	{
		if (mc34xx_i2c_read_block(pt_i2c_client, MC34XX_REG_XOUT, _baData, MC34XX_LOW_REOLUTION_DATA_SIZE))
		{
			GSE_ERR("ERR: fail to read data via I2C!\n");
		
			return (MC34XX_RETCODE_ERROR_I2C);
		}
		waData[MC34XX_AXIS_X] = ((signed char) _baData[0]);
		waData[MC34XX_AXIS_Y] = ((signed char) _baData[1]);
		waData[MC34XX_AXIS_Z] = ((signed char) _baData[2]);
	}
	else if (MC34XX_RESOLUTION_HIGH == s_bResolution)
	{
		if (mc34xx_i2c_read_block(pt_i2c_client, MC34XX_REG_XOUT_EX_L, _baData, MC34XX_HIGH_REOLUTION_DATA_SIZE))
		{
			GSE_ERR("ERR: fail to read data via I2C!\n");
	
			return (MC34XX_RETCODE_ERROR_I2C);
		}
	
		waData[MC34XX_AXIS_X] = ((signed short) ((_baData[0]) | (_baData[1]<<8)));
		waData[MC34XX_AXIS_Y] = ((signed short) ((_baData[2]) | (_baData[3]<<8)));
		waData[MC34XX_AXIS_Z] = ((signed short) ((_baData[4]) | (_baData[5]<<8)));
	}

	//GSE_LOG("[%s][raw_data] X: %4d, Y: %4d, Z: %4d\n",
	//		__func__, waData[MC34XX_AXIS_X], waData[MC34XX_AXIS_Y], waData[MC34XX_AXIS_Z]);

 

	return MC34XX_RETCODE_SUCCESS;
}

/*****************************************
 *** MC34XX_ResetCalibration
 *****************************************/
static int MC34XX_ResetCalibration(struct i2c_client *client)
{
	struct mc34xx_i2c_data *obj = i2c_get_clientdata(client);

	obj->cali_sw[MC34XX_AXIS_X] = 0x00;
	obj->cali_sw[MC34XX_AXIS_Y] = 0x00;
	obj->cali_sw[MC34XX_AXIS_Y] = 0x00;

	return 0;
}

/*****************************************
 *** MC34XX_ReadCalibration
 *****************************************/
static int MC34XX_ReadCalibration(struct i2c_client *client, int dat[MC34XX_AXES_NUM])
{
    struct mc34xx_i2c_data *obj = i2c_get_clientdata(client);
	
	dat[obj->cvt.map[MC34XX_AXIS_X]] = obj->cvt.sign[MC34XX_AXIS_X]*obj->cali_sw[MC34XX_AXIS_X];
    dat[obj->cvt.map[MC34XX_AXIS_Y]] = obj->cvt.sign[MC34XX_AXIS_Y]*obj->cali_sw[MC34XX_AXIS_Y];
    dat[obj->cvt.map[MC34XX_AXIS_Z]] = obj->cvt.sign[MC34XX_AXIS_Z]*obj->cali_sw[MC34XX_AXIS_Z]; 

    GSE_LOG("dat[x,y,z] %d %d %d \n",dat[MC34XX_AXIS_X] ,dat[MC34XX_AXIS_Y],dat[MC34XX_AXIS_Z]);

    GSE_LOG("dat[mapxyz] %d %d %d \n",dat[obj->cvt.map[MC34XX_AXIS_X]] ,dat[obj->cvt.map[MC34XX_AXIS_Y]],dat[obj->cvt.map[MC34XX_AXIS_Z]]);

    return 0;
}

/*********************************************************
 *** MC34XX_CaliConvert
 * 6 direction calibration
 * xyz threshold:(-300mg ~ +300mg) can be calibrated
 *********************************************************/
static int  MC34XX_CaliConvert(struct SENSOR_DATA *cali_data)
{
	struct SENSOR_DATA local_data;
	local_data.x = 0 - cali_data->x;
	local_data.y = 0 - cali_data->y;
	local_data.z = GRAVITY_EARTH_1000 - cali_data->z;
	GSE_LOG("no convert data  %d %d %d \n",local_data.x,local_data.y,local_data.z);
	if(((local_data.x >= 6807) && (local_data.x <= 12807)))
	{
		cali_data->x = GRAVITY_EARTH_1000 - local_data.x;
		cali_data->y = 0 - local_data.y;
		cali_data->z = 0 - local_data.z;
	}else if(((0-local_data.x) > 6807) && (0-local_data.x) <= 12807)
	{
		cali_data->x = 0-(GRAVITY_EARTH_1000 + local_data.x);
		cali_data->y = 0 - local_data.y;
		cali_data->z = 0 - local_data.z;
	}else if(((local_data.y >= 6807) && (local_data.y <= 12807)))
	{
		cali_data->x = 0 - local_data.x;
		cali_data->y = GRAVITY_EARTH_1000 - local_data.y;
		cali_data->z = 0 - local_data.z;
	}else if(((0-local_data.y) > 6807) && (0-local_data.y) <= 12807)
	{
		cali_data->x = 0 - local_data.x;
		cali_data->y = 0-(GRAVITY_EARTH_1000 + local_data.y);
		cali_data->z = 0 - local_data.z;
	}else if(((local_data.x >= 6807) && (local_data.x <= 12807)))
	{
		cali_data->x = 0 - local_data.x;
		cali_data->y = 0 - local_data.y;
		cali_data->z = GRAVITY_EARTH_1000 - local_data.z;
	}else if(((0-local_data.z) > 6807) && (0-local_data.z) <= 12807)
	{
		cali_data->x = 0 - local_data.x;
		cali_data->y = 0 - local_data.y;
		cali_data->z = 0-(GRAVITY_EARTH_1000 + local_data.z);
	}else{
        GSE_LOG("the xyz threshold over:(-300mg ~ +300mg)\n");
        return -EINVAL;
    }
	GSE_LOG("convert data  %d %d %d \n",cali_data->x,cali_data->y,cali_data->z);
    return 0;
}

/*****************************************
 *** MC34XX_WriteCalibration
 *****************************************/
static int MC34XX_WriteCalibration(struct i2c_client *client, int dat[MC34XX_AXES_NUM])
{
	struct mc34xx_i2c_data *obj = i2c_get_clientdata(client);
	int err = 0;
	int cali[MC34XX_AXES_NUM] = {0};

	GSE_LOG("UPDATE dat: (%+3d %+3d %+3d)\n", dat[MC34XX_AXIS_X], dat[MC34XX_AXIS_Y], dat[MC34XX_AXIS_Z]);

	cali[MC34XX_AXIS_X] = dat[MC34XX_AXIS_X];
    cali[MC34XX_AXIS_Y] = dat[MC34XX_AXIS_Y];
    cali[MC34XX_AXIS_Z] = dat[MC34XX_AXIS_Z];
	GSE_LOG("MC34XX_WriteCalibration:cali %d %d %d \n",cali[MC34XX_AXIS_X] ,cali[MC34XX_AXIS_Y],cali[MC34XX_AXIS_Z]);

    obj->cali_sw[MC34XX_AXIS_X] = obj->cvt.sign[MC34XX_AXIS_X]*(cali[obj->cvt.map[MC34XX_AXIS_X]]);
    obj->cali_sw[MC34XX_AXIS_Y] = obj->cvt.sign[MC34XX_AXIS_Y]*(cali[obj->cvt.map[MC34XX_AXIS_Y]]);
    obj->cali_sw[MC34XX_AXIS_Z] = obj->cvt.sign[MC34XX_AXIS_Z]*(cali[obj->cvt.map[MC34XX_AXIS_Z]]);

	GSE_LOG("UPDATE dat:obj->cali_sw: (%3d %3d %3d)\n", obj->cali_sw[MC34XX_AXIS_X], obj->cali_sw[MC34XX_AXIS_Y], obj->cali_sw[MC34XX_AXIS_Z]);
	GSE_LOG("MC34XX_WriteCalibration:dat[map x,y,x] %d %d %d \n",dat[obj->cvt.map[MC34XX_AXIS_X]] ,dat[obj->cvt.map[MC34XX_AXIS_Y]],dat[obj->cvt.map[MC34XX_AXIS_Z]]);

	return err;
}

/*****************************************
 *** MC34XX_SetPowerMode
 *****************************************/
static int MC34XX_SetPowerMode(struct i2c_client *client, bool enable)
{
	u8 databuf[2] = {0};
	int res = 0;
	u8 addr = MC34XX_REG_MODE_FEATURE;
	struct mc34xx_i2c_data *obj = i2c_get_clientdata(client);

	if (enable == mc34xx_sensor_power)
		GSE_LOG("Sensor power status should not be set again!!!\n");

	if (mc34xx_i2c_read_block(client, addr, databuf, 1)) {
		GSE_ERR("read power ctl register err!\n");
		return MC34XX_RETCODE_ERROR_I2C;
	}

	GSE_LOG("set power read MC34XX_REG_MODE_FEATURE =%02x\n", databuf[0]);

	if (enable) {
		databuf[0] = 0x41;
		res = mc34xx_i2c_write_block(client, MC34XX_REG_MODE_FEATURE, databuf, 1);
		mc34xx_i2c_read_block(client, MC34X6_REG_INTR_STAT_2, databuf, 1);//clear irq
	} else {
		databuf[0] = 0x41;
		res = mc34xx_i2c_write_block(client, MC34XX_REG_MODE_FEATURE, databuf, 1);
	}

	if (res < 0) {
		GSE_LOG("fwq set power mode failed!\n");
		return MC34XX_RETCODE_ERROR_I2C;
	} else if (atomic_read(&obj->trace) & MCUBE_TRC_INFO)
		GSE_LOG("fwq set power mode ok %d!\n", databuf[1]);

	mc34xx_sensor_power = enable;

	return MC34XX_RETCODE_SUCCESS;
}

/*****************************************
 *** MC34XX_SetResolution
 *****************************************/
static void MC34XX_SetResolution(void)
{
    GSE_LOG("[%s]\n", __FUNCTION__);

    switch (s_bPCODE)
    {
    case MC34XX_PCODE_3433:
    case MC34XX_PCODE_3436:
         s_bResolution = MC34XX_RESOLUTION_LOW;
         break;
    case MC34XX_PCODE_3413:
    case MC34XX_PCODE_3416:
         s_bResolution = MC34XX_RESOLUTION_HIGH;
         break;
    default:
         GSE_ERR("ERR: no resolution assigned!\n");
         break;
    }
    if (MC34XX_RESOLUTION_HIGH == s_bResolution)
    {
        GSE_LOG("[%s] s_bResolution: %d(MC34XX_RESOLUTION_HIGH)\n", __FUNCTION__, s_bResolution);
    }else if(MC34XX_RESOLUTION_LOW == s_bResolution){
        GSE_LOG("[%s] s_bResolution: %d(MC34XX_RESOLUTION_LOW)\n", __FUNCTION__, s_bResolution);
    }
}

/*****************************************
 *** MC34XX_SetSampleRate
 *****************************************/
static void MC34XX_SetSampleRate(struct i2c_client *pt_i2c_client)
{
	unsigned char	_baDataBuf[2] = { 0 },_baData2Buf[2] = { 0 };

	GSE_LOG("[%s]\n", __func__);

	_baDataBuf[0] = MC34XX_REG_SAMPLE_RATE;
	_baDataBuf[1] = 0x00;

	if (IS_MERAK()) {
		mc34xx_i2c_read_block(pt_i2c_client, MC34XX_REG_MCLK_POLARITY, _baData2Buf, 1);
		GSE_LOG("[%s MERAK] REG(0x2A) = 0x%02X\n", __func__, _baData2Buf[0]);     
		_baData2Buf[0] = (_baData2Buf[0] & 0xC0);

		mc34xx_i2c_read_block(pt_i2c_client, MC34XX_REG_SAMPLE_RATE, _baDataBuf, 1);

		switch (_baData2Buf[0]) {
			case 0x00:
				_baDataBuf[0] = ((_baDataBuf[0] & 0xF0) | 0x00);//CLK =2.56MHz,ODR=128Hz
				break;
			case 0x40:
				_baDataBuf[0] = ((_baDataBuf[0] & 0xF0) | 0x08);//CLK =1.28MHz,ODR=128Hz
				break;
			case 0x80:
				_baDataBuf[0] = ((_baDataBuf[0] & 0xF0) | 0x09);//CLK =640KHz,ODR=128Hz
				break;
			case 0xC0:
				_baDataBuf[0] = ((_baDataBuf[0] & 0xF0) | 0x0A);//CLK =320KHz,ODR=128Hz
				break;
			default:
				GSE_ERR("[%s] no chance to get here... check code!\n", __func__);
				break;
		}
        GSE_LOG("[%s MERAK] REG(0x08) = 0x%02X\n", __func__, _baDataBuf[0]);	
	}
	else if (IS_MENSA()){
		mc34xx_i2c_read_block(pt_i2c_client, MC34XX_REG_SAMPLE_RATE, _baDataBuf, 1);
		_baDataBuf[0] = ((_baDataBuf[0] & 0xF8) | 0x05);//CLK =2.56MHz,OSR=64,ODR=2048Hz
		GSE_LOG("[%s MENSA] REG(0x08) = 0x%02X\n", __func__, _baDataBuf[0]);			

	} else
		_baDataBuf[0] = 0x00;

	mc34xx_i2c_write_block(pt_i2c_client, MC34XX_REG_SAMPLE_RATE, _baDataBuf, 1);
}

/*****************************************
 *** MC34XX_LowPassFilter
 *****************************************/
static void MC34XX_LowPassFilter(struct i2c_client *pt_i2c_client)
{
	unsigned char	_baDataBuf[2] = { 0 };
	int res = 0;
	if (IS_MENSA())
	{
		res = mc34xx_i2c_read_block(pt_i2c_client, MC34XX_REG_LPF_RANGE_RES, _baDataBuf, 1);
		GSE_LOG("[%s MENSA] Read REG(0x20) = 0x%02X\n", __func__, _baDataBuf[0]);			
		_baDataBuf[0] = ((_baDataBuf[0] & 0xF0) | 0x09);// 128Hz @ 2048 Hz ODR	
		res = mc34xx_i2c_write_block(pt_i2c_client, MC34XX_REG_LPF_RANGE_RES, _baDataBuf, 1);
		if (res < 0)
			GSE_LOG("[%s] MENSA LPF failed.\n",__func__);
		else
			GSE_LOG("[%s] Write REG(0x20) = 0x%02X,MENSA LPF enable success.\n",__func__,_baDataBuf[0]);
	}else if(IS_MERAK()){
			GSE_LOG("[%s] MERAK not support LPF.\n",__func__);
	}
}
/*****************************************
 *** MC34XX_ConfigResRange
 *****************************************/
static void MC34XX_ConfigResRange(struct i2c_client *pt_i2c_client)
{
	unsigned char	_baDataBuf[2] = { 0 };
	int res = 0;

	mc34xx_i2c_read_block(pt_i2c_client, MC34XX_REG_LPF_RANGE_RES, _baDataBuf, 1);
	GSE_LOG("[%s] Read REG(0x20) = 0x%02X\n", __func__, _baDataBuf[0]);

    if (IS_MERAK())
    {
        if (MC34XX_RESOLUTION_LOW == s_bResolution)
            _baDataBuf[0] = ((_baDataBuf[0] & 0x88) | 0x02);//8bit,(+2g~-2g)
        else
            _baDataBuf[0] = ((_baDataBuf[0] & 0x88) | 0x25);//14bit,(+8g~-8g)
    }
	else if (IS_MENSA()){
		if (MC34XX_RESOLUTION_LOW == s_bResolution)
			_baDataBuf[0] = ((_baDataBuf[0] & 0x0F) | 0x80);//8bit,(+2g~-2g)
		else
			_baDataBuf[0] = ((_baDataBuf[0] & 0x0F) | 0x20);//16bit,(+8g~-8g)
	}

	res = mc34xx_i2c_write_block(pt_i2c_client, MC34XX_REG_LPF_RANGE_RES, _baDataBuf, 1);
	if (res < 0)
		GSE_ERR("MC34XX_ConfigResRange fail\n");

	GSE_LOG("[%s] Write REG(0x20)resolution and range set 0x%02X\n", __func__, _baDataBuf[0]);
}

/*****************************************
 *** MC34XX_SetGain
 *****************************************/
static void MC34XX_SetGain(void)
{

	if (IS_MERAK())
	{
		if (MC34XX_RESOLUTION_LOW == s_bResolution)
    	{
			 gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = (1 << 8)/(1 << 2);//8bit,(+2g~-2g)-->64;
		}else if(MC34XX_RESOLUTION_HIGH == s_bResolution)
		{
				gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = (1 << 14)/(1 << 4);//14bit,(+8g~-8g)-->1024
		}
	}else if(IS_MENSA()){

		if (MC34XX_RESOLUTION_LOW == s_bResolution)
    	{
			 gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = (1 << 8)/(1 << 2);//8bit,(+2g~-2g)-->64;
		}else if(MC34XX_RESOLUTION_HIGH == s_bResolution){
				gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = (1 << 16)/(1 << 4);//16bit,(+8g~-8g)-->4096
		}
	}

	GSE_LOG("[%s] gain: %d / %d / %d\n", __func__, gsensor_gain.x, gsensor_gain.y, gsensor_gain.z);
}

/*****************************************
 *** MC34XX_Init
 *****************************************/
#ifdef MC34XX_SUPPORT_INTERRUPT
#ifdef CONFIG_ACCEL_HIT_HAPPEN_DETECT
extern bool is_acc_off(void);
static bool is_irq_enabled = false;
extern unsigned int accel_hit_threshold;
extern unsigned int accel_interrupt_threshold;
extern void hit_input_event_report(void);
void enable_gsensor_irq(void)
{
	unsigned char	_baDataBuf[2] = {0};
	int count = 0;
	int pre_pin_value = 0;
	int pin_value = 0;

	if(mc34xx_i2c_client == NULL)
		return;

	if(accel_hit_threshold == 0)
		return;

	_baDataBuf[0] = 0x43;
	mc34xx_i2c_write_block(mc34xx_i2c_client, MC34XX_REG_MODE_FEATURE, _baDataBuf, 1);

	_baDataBuf[0] = accel_interrupt_threshold&0xff;
	mc34xx_i2c_write_block(mc34xx_i2c_client, MC34X6_REG_AM_THRESHOLD_LSB, _baDataBuf, 1);

	_baDataBuf[0] = (accel_interrupt_threshold >> 8)&0xff;
	mc34xx_i2c_write_block(mc34xx_i2c_client, MC34X6_REG_AM_THRESHOLD_MSB, _baDataBuf, 1);

	_baDataBuf[0] = 0x04;
	mc34xx_i2c_write_block(mc34xx_i2c_client, MC34XX_REG_INTERRUPT_ENABLE, _baDataBuf, 1);

	pre_pin_value = gpio_get_value(mc34xx_obj_i2c_data->pin);
	mc34xx_i2c_read_block(mc34xx_i2c_client, MC34X6_REG_INTR_STAT_2, _baDataBuf, 1);//clear irq
	pin_value = gpio_get_value(mc34xx_obj_i2c_data->pin);

	while(pin_value == 0 && count < 20)
	{
		mc34xx_i2c_read_block(mc34xx_i2c_client, MC34X6_REG_INTR_STAT_2, _baDataBuf, 1);//clear irq
		msleep(100);
		pin_value = gpio_get_value(mc34xx_obj_i2c_data->pin);
		count++;
	}

	_baDataBuf[0] = 0x41;
	mc34xx_i2c_write_block(mc34xx_i2c_client, MC34XX_REG_MODE_FEATURE, _baDataBuf, 1);
	printk("bbl %s pre_pin_value = %d, pin_value = %d, count = %d\n",__func__,pre_pin_value, pin_value, count);
	if(is_irq_enabled == false)
	{
		enable_irq(mc34xx_obj_i2c_data->irq);
		is_irq_enabled = true;
	}
}

void disable_gsensor_irq(void)
{
	unsigned char	_baDataBuf[2] = {0};

	if(mc34xx_i2c_client == NULL)
		return;

	if(accel_hit_threshold == 0)
		return;

	_baDataBuf[0] = 0x43;
	mc34xx_i2c_write_block(mc34xx_i2c_client, MC34XX_REG_MODE_FEATURE, _baDataBuf, 1);

	_baDataBuf[0] = 0x00;
	mc34xx_i2c_write_block(mc34xx_i2c_client, MC34XX_REG_INTERRUPT_ENABLE, _baDataBuf, 1);

	_baDataBuf[0] = 0x41;
	mc34xx_i2c_write_block(mc34xx_i2c_client, MC34XX_REG_MODE_FEATURE, _baDataBuf, 1);

	if(is_irq_enabled == true)
	{
		disable_irq(mc34xx_obj_i2c_data->irq);
		is_irq_enabled = false;
	}
}
#endif

static void mc34xx_eint_work(struct work_struct *work)
{
	static int times = 0;
	unsigned char	_baDataBuf[2] = {0};

#ifdef CONFIG_ACCEL_HIT_HAPPEN_DETECT
	if(is_acc_off())
		hit_input_event_report();
#endif
	mc34xx_i2c_read_block(mc34xx_i2c_client, MC34X6_REG_INTR_STAT_2, _baDataBuf, 1);//clear irq

	printk("bbl %s-------------times = %d\n",__func__,times++);

	enable_irq(mc34xx_obj_i2c_data->irq);
}

static irqreturn_t mc34xx_eint_handler(int irq, void *desc)
{
	disable_irq_nosync(mc34xx_obj_i2c_data->irq);
	schedule_work(&mc34xx_obj_i2c_data->eint_work);
	return IRQ_HANDLED;
}

static const struct of_device_id gse1_of_match[] = {
	{ .compatible = "mediatek,gse_1", },
	{},
};

static int mc34xx_setup_eint(struct i2c_client *client)
{
	int ret;
	struct device_node *node = NULL;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_cfg;
	u32 int_debounce = 0;

	GSE_LOG("[%s]\n", __func__);
	/* gpio setting */
	pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(pinctrl)) {
		ret = PTR_ERR(pinctrl);
		GSE_ERR("Cannot find alsps pinctrl!\n");
	}
	pins_cfg = pinctrl_lookup_state(pinctrl, "pin_cfg");
	if (IS_ERR(pins_cfg)) {
		ret = PTR_ERR(pins_cfg);
		GSE_ERR("Cannot find alsps pinctrl pin_cfg!\n");
	}

	pinctrl_select_state(pinctrl, pins_cfg);

	node = of_find_matching_node(node, gse1_of_match);
	if(node)
	{
		of_property_read_u32(node, "debounce", &int_debounce);
		mc34xx_obj_i2c_data->pin = of_get_named_gpio(node, "deb-gpios", 0);
		gpio_set_debounce(mc34xx_obj_i2c_data->pin, int_debounce);
		mc34xx_obj_i2c_data->irq = irq_of_parse_and_map(node, 0);
		GSE_LOG("irq = %d, gsensor_int_pin = %d, int_debounce = %d!!\n",mc34xx_obj_i2c_data->irq, mc34xx_obj_i2c_data->pin, int_debounce);
		if(mc34xx_obj_i2c_data->irq > 0)
		{
			if(request_irq(mc34xx_obj_i2c_data->irq, mc34xx_eint_handler, IRQF_TRIGGER_LOW, "gse-eint", NULL))
			{
				GSE_ERR("irq request error\n");
				return -EINVAL;
			}
			disable_irq(mc34xx_obj_i2c_data->irq);
			enable_irq_wake(mc34xx_obj_i2c_data->irq);
		}
	}
	else
	{
		GSE_ERR("null irq node!!\n");
		return -EINVAL;
	}

	return 0;
}
#else
void enable_gsensor_irq(void)
{
}

void disable_gsensor_irq(void)
{
}
#endif
static int MC34XX_Init(struct i2c_client *client, int reset_cali)
{
	unsigned char	_baDataBuf[2] = { 0 };

	GSE_LOG("[%s]\n", __func__);

	#ifdef _MC34XX_SUPPORT_POWER_SAVING_SHUTDOWN_POWER_
	if (MC34XX_RETCODE_SUCCESS != mc34xx_i2c_auto_probe(client))
		return MC34XX_RETCODE_ERROR_I2C;

	/* GSE_LOG("[%s] confirmed i2c addr: 0x%X\n", __FUNCTION__, client->addr); */
	#endif

	_baDataBuf[0] = 0x43;
	mc34xx_i2c_write_block(client, MC34XX_REG_MODE_FEATURE, _baDataBuf, 1);

	MC34XX_SetResolution();
	MC34XX_SetSampleRate(client);
	MC34XX_LowPassFilter(client);
	MC34XX_ConfigResRange(client);
	MC34XX_SetGain();
	if (IS_MENSA())
	{
		_baDataBuf[0] = 0x04;
		mc34xx_i2c_write_block(client, MC34X6_REG_MOTION_CTRL, _baDataBuf, 1);

	}
	if (IS_MERAK())
	{
		_baDataBuf[0] = 0x00;
		mc34xx_i2c_write_block(client, MC34XX_REG_INTERRUPT_ENABLE, _baDataBuf, 1);
	}
	else if(IS_MENSA())
	{
		_baDataBuf[0] = 0x00;
		mc34xx_i2c_write_block(client, MC34XX_REG_INTERRUPT_ENABLE, _baDataBuf, 1);

	}

	mc34xx_i2c_read_block(client, MC34XX_REG_MCLK_POLARITY, _baDataBuf, 1);
	s_bMPOL = (_baDataBuf[0] & 0x03);

	GSE_LOG("[%s] init ok.\n", __func__);

#ifdef MC34XX_SUPPORT_INTERRUPT
	if(IS_MENSA())
	{
		_baDataBuf[0] = 0x04;
		mc34xx_i2c_write_block(client, MC34X6_REG_MOTION_CTRL, _baDataBuf, 1);
		_baDataBuf[0] = 0x60;
		mc34xx_i2c_write_block(client, MC34X6_REG_AM_THRESHOLD_LSB, _baDataBuf, 1);
		_baDataBuf[0] = 0x00;
		mc34xx_i2c_write_block(client, MC34X6_REG_AM_THRESHOLD_MSB, _baDataBuf, 1);
	
		_baDataBuf[0] = 0x00;
		mc34xx_i2c_write_block(client, MC34XX_REG_INTERRUPT_ENABLE, _baDataBuf, 1);
	}
	INIT_WORK(&mc34xx_obj_i2c_data->eint_work, mc34xx_eint_work);
	mc34xx_setup_eint(client);
#endif

	return MC34XX_RETCODE_SUCCESS;
}

/*****************************************
 *** MC34XX_ReadChipInfo
 *****************************************/
static int MC34XX_ReadChipInfo(struct i2c_client *client, char *buf, int bufsize)
{
	if ((NULL == buf) || (bufsize <= 30))
		return -1;

	if (IS_MERAK())
	{
		sprintf(buf, "MC34X3 Chip");
	}else if(IS_MENSA()){
		sprintf(buf, "MC34X6 Chip");
	}else{
        sprintf(buf, "Unknown Chip");
    }
	return 0;
}

/*****************************************
 *** MC34XX_ReadSensorData
 *****************************************/
static int MC34XX_ReadSensorData(struct i2c_client *pt_i2c_client, char *pbBuf, int nBufSize)
{
	int					   _naAccelData[MC34XX_AXES_NUM] = { 0 };
	struct mc34xx_i2c_data   *_pt_i2c_obj = ((struct mc34xx_i2c_data *) i2c_get_clientdata(pt_i2c_client));

	if ((NULL == pt_i2c_client) || (NULL == pbBuf)) {
		GSE_ERR("ERR: Null Pointer\n");
		return MC34XX_RETCODE_ERROR_NULL_POINTER;
	}

	if (false == mc34xx_sensor_power) {
		if (MC34XX_RETCODE_SUCCESS != MC34XX_SetPowerMode(pt_i2c_client, true))
			GSE_ERR("ERR: fail to set power mode!\n");
	}

	if (MC34XX_RETCODE_SUCCESS != MC34XX_ReadData(pt_i2c_client, _pt_i2c_obj->data)) {
		GSE_ERR("ERR: fail to read data!\n");

		return MC34XX_RETCODE_ERROR_I2C;
	}
	
	//add for sw cali
	_pt_i2c_obj->data[MC34XX_AXIS_X] += _pt_i2c_obj->cali_sw[MC34XX_AXIS_X];
	_pt_i2c_obj->data[MC34XX_AXIS_Y] += _pt_i2c_obj->cali_sw[MC34XX_AXIS_Y];
	_pt_i2c_obj->data[MC34XX_AXIS_Z] += _pt_i2c_obj->cali_sw[MC34XX_AXIS_Z];

	/* output format: mg */
	if (atomic_read(&_pt_i2c_obj->trace) & MCUBE_TRC_INFO)
		GSE_LOG("[%s] raw data: %d, %d, %d\n", __func__, _pt_i2c_obj->data[MC34XX_AXIS_X],
			_pt_i2c_obj->data[MC34XX_AXIS_Y], _pt_i2c_obj->data[MC34XX_AXIS_Z]);

	_naAccelData[(_pt_i2c_obj->cvt.map[MC34XX_AXIS_X])] = (_pt_i2c_obj->cvt.sign[MC34XX_AXIS_X]
		* _pt_i2c_obj->data[MC34XX_AXIS_X]);
	_naAccelData[(_pt_i2c_obj->cvt.map[MC34XX_AXIS_Y])] = (_pt_i2c_obj->cvt.sign[MC34XX_AXIS_Y]
		* _pt_i2c_obj->data[MC34XX_AXIS_Y]);
	_naAccelData[(_pt_i2c_obj->cvt.map[MC34XX_AXIS_Z])] = (_pt_i2c_obj->cvt.sign[MC34XX_AXIS_Z]
		* _pt_i2c_obj->data[MC34XX_AXIS_Z]);

	if (atomic_read(&_pt_i2c_obj->trace) & MCUBE_TRC_INFO)
		GSE_LOG("[%s] map data: %d, %d, %d!\n", __func__, _naAccelData[MC34XX_AXIS_X],
			_naAccelData[MC34XX_AXIS_Y], _naAccelData[MC34XX_AXIS_Z]);

	_naAccelData[MC34XX_AXIS_X] = (_naAccelData[MC34XX_AXIS_X] * GRAVITY_EARTH_1000 / gsensor_gain.x);
	_naAccelData[MC34XX_AXIS_Y] = (_naAccelData[MC34XX_AXIS_Y] * GRAVITY_EARTH_1000 / gsensor_gain.y);
	_naAccelData[MC34XX_AXIS_Z] = (_naAccelData[MC34XX_AXIS_Z] * GRAVITY_EARTH_1000 / gsensor_gain.z);

	if (atomic_read(&_pt_i2c_obj->trace) & MCUBE_TRC_INFO)
		GSE_LOG("[%s] accel data: %d, %d, %d!\n", __func__, _naAccelData[MC34XX_AXIS_X],
			_naAccelData[MC34XX_AXIS_Y], _naAccelData[MC34XX_AXIS_Z]);

	sprintf(pbBuf, "%04x %04x %04x",
		_naAccelData[MC34XX_AXIS_X], _naAccelData[MC34XX_AXIS_Y], _naAccelData[MC34XX_AXIS_Z]);

	return MC34XX_RETCODE_SUCCESS;
}

/*****************************************
 *** MC34XX_ReadRawData
 *****************************************/
static int MC34XX_ReadRawData(struct i2c_client *client, char *buf)
{
	int res = 0;
	s16 sensor_data[3] = {0};

	if (!buf || !client)
		return -EINVAL;

	if (mc34xx_sensor_power == false) {
		res = MC34XX_SetPowerMode(client, true);
		if (res)
			GSE_ERR("Power on MC34XX error %d!\n", res);
	}

	res = MC34XX_ReadData(client, sensor_data);
	if (res) {
		GSE_ERR("I2C error: ret value=%d", res);
		return -EIO;
	}
	sprintf(buf, "%04x %04x %04x", sensor_data[MC34XX_AXIS_X],
	sensor_data[MC34XX_AXIS_Y], sensor_data[MC34XX_AXIS_Z]);

	return 0;
}

/*****************************************
 *** MC34XX_JudgeTestResult
 *****************************************/
static int MC34XX_JudgeTestResult(struct i2c_client *client)
{
	int	res				  = 0;
	//int	self_result		  = 0;
	//s16	acc[MC34XX_AXES_NUM] = { 0 };
	unsigned char	_baData1Buf[2] = { 0 };
	res = mc34xx_i2c_read_block(client, MC34XX_REG_PRODUCT_CODE_L, _baData1Buf, 1);
	if (res) {
		GSE_ERR("I2C error: ret value=%d", res);
		return -EIO;
	}
	
	if ((MC34XX_PCODE_3416 == (_baData1Buf[0] & 0xF1)) ||
	    (MC34XX_PCODE_3436 == (_baData1Buf[0] & 0xF1)) ||
		(MC34XX_PCODE_3413 == (_baData1Buf[0] & 0xF1)) ||
		(MC34XX_PCODE_3433 == (_baData1Buf[0] & 0xF1))
		){
			return MC34XX_RETCODE_SUCCESS;
		}
/*
	res = MC34XX_ReadData(client, acc);
	if (res) {
		GSE_ERR("I2C error: ret value=%d", res);
		return -EIO;
	}

	acc[MC34XX_AXIS_X] = acc[MC34XX_AXIS_X] * 1000 / gsensor_gain.x;
	acc[MC34XX_AXIS_Y] = acc[MC34XX_AXIS_Y] * 1000 / gsensor_gain.y;
	acc[MC34XX_AXIS_Z] = acc[MC34XX_AXIS_Z] * 1000 / gsensor_gain.z;

	self_result = ((acc[MC34XX_AXIS_X] * acc[MC34XX_AXIS_X])
		   + (acc[MC34XX_AXIS_Y] * acc[MC34XX_AXIS_Y])
		   + (acc[MC34XX_AXIS_Z] * acc[MC34XX_AXIS_Z]));

	if ((self_result > 475923) && (self_result < 2185360)) {
		GSE_ERR("MC34XX_JudgeTestResult successful\n");
		return MC34XX_RETCODE_SUCCESS;
	}
*/
	GSE_ERR("MC34XX_JudgeTestResult failed\n");
	return -EINVAL;
}

/*****************************************
 *** show_chipinfo_value
 *****************************************/
static ssize_t show_chipinfo_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = mc34xx_i2c_client;
	char strbuf[MC34XX_BUF_SIZE] = {0};

	GSE_LOG("fwq show_chipinfo_value\n");

	MC34XX_ReadChipInfo(client, strbuf, MC34XX_BUF_SIZE);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);
}

/*****************************************
 *** show_sensordata_value
 *****************************************/
static ssize_t show_sensordata_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = mc34xx_i2c_client;
	char strbuf[MC34XX_BUF_SIZE] = { 0 };

	mc34xx_mutex_lock();
	MC34XX_ReadSensorData(client, strbuf, MC34XX_BUF_SIZE);
	mc34xx_mutex_unlock();
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);
}

/*****************************************
 *** show_selftest_value
 *****************************************/
static ssize_t show_selftest_value(struct device_driver *ddri, char *buf)
{
	return snprintf(buf, 8, "%s\n", selftestRes);
}

/*****************************************
 *** store_selftest_value
 *****************************************/
static ssize_t store_selftest_value(struct device_driver *ddri, const char *buf, size_t count)
{   /*write anything to this register will trigger the process*/
	struct i2c_client *client = mc34xx_i2c_client;
	int num = 0;
	int ret = 0;

	ret = kstrtoint(buf, 10, &num);
	if (ret != 0) {
		GSE_ERR("parse number fail\n");
		return count;
	} else if (0 == num) {
		GSE_ERR("invalid data count\n");
		return count;
	}

	GSE_LOG("NORMAL:\n");
	mc34xx_mutex_lock();
	MC34XX_SetPowerMode(client, true);
	mc34xx_mutex_unlock();
	GSE_LOG("SELFTEST:\n");

	if (!MC34XX_JudgeTestResult(client)) {
		GSE_LOG("SELFTEST : PASS\n");
		strcpy(selftestRes, "y");
	} else {
		GSE_LOG("SELFTEST : FAIL\n");
		strcpy(selftestRes, "n");
	}

	return count;
}

/*****************************************
 *** show_trace_value
 *****************************************/
static ssize_t show_trace_value(struct device_driver *ddri, char *buf)
{
	ssize_t res = 0;
	struct mc34xx_i2c_data *obj = mc34xx_obj_i2c_data;

	GSE_LOG("fwq show_trace_value\n");

	if (obj == NULL) {
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}

	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&obj->trace));
	return res;
}

/*****************************************
 *** store_trace_value
 *****************************************/
static ssize_t store_trace_value(struct device_driver *ddri, const char *buf, size_t count)
{
	struct mc34xx_i2c_data *obj = mc34xx_obj_i2c_data;
	int trace = 0;

	GSE_LOG("fwq store_trace_value\n");

	if (obj == NULL) {
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}

	if (1 == sscanf(buf, "0x%x", &trace))
		atomic_set(&obj->trace, trace);
	else
		GSE_ERR("invalid content: '%s', length = %zu\n", buf, count);

	return count;
}

/*****************************************
 *** show_status_value
 *****************************************/
static ssize_t show_status_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct mc34xx_i2c_data *obj = mc34xx_obj_i2c_data;

	GSE_LOG("fwq show_status_value\n");

	if (obj->hw)
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST:i2c_num=%02X i2c_num=%02X direction=%02X firlen=%02X batch=%02X)\n",
			obj->hw->i2c_num, (int)obj->hw->i2c_addr[0],obj->hw->direction, obj->hw->firlen, obj->hw->is_batch_supported);
	else
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: NULL\n");

	return len;
}

/*****************************************
 *** show_regiter_map
 *****************************************/
static ssize_t show_regiter_map(struct device_driver *ddri, char *buf)
{
	u8		 _bIndex	   = 0;
	u8		 _baRegMap[MC34XX_REGMAP_LENGTH] = { 0 };
	ssize_t	_tLength	  = 0;

	struct i2c_client *client = mc34xx_i2c_client;

	if ((0xA5 == buf[0]) && (0x7B == buf[1]) && (0x40 == buf[2])) {
		mc34xx_mutex_lock();
		MC34XX_ReadRegMap(client, buf);
		mc34xx_mutex_unlock();

		buf[0x21] = s_baOTP_OffsetData[0];
		buf[0x22] = s_baOTP_OffsetData[1];
		buf[0x23] = s_baOTP_OffsetData[2];
		buf[0x24] = s_baOTP_OffsetData[3];
		buf[0x25] = s_baOTP_OffsetData[4];
		buf[0x26] = s_baOTP_OffsetData[5];

		_tLength = 64;
	} else {
		mc34xx_mutex_lock();
		MC34XX_ReadRegMap(client, _baRegMap);
		mc34xx_mutex_unlock();

	for (_bIndex = 0; _bIndex < MC34XX_REGMAP_LENGTH; _bIndex++)
		_tLength += snprintf((buf + _tLength), (PAGE_SIZE - _tLength), "Reg[0x%02X]: 0x%02X\n",
			_bIndex, _baRegMap[_bIndex]);
	}

	return _tLength;
}

/*****************************************
 *** store_regiter_map
 *****************************************/
static ssize_t store_regiter_map(struct device_driver *ddri, const char *buf, size_t count)
{
	return count;
}

/*****************************************
 *** show_chip_orientation
 *****************************************/
static ssize_t show_chip_orientation(struct device_driver *ptDevDrv, char *pbBuf)
{
	ssize_t		  _tLength = 0;
	struct acc_hw   *_ptAccelHw = hw;

	GSE_LOG("[%s] default direction: %d\n", __func__, _ptAccelHw->direction);

	_tLength = snprintf(pbBuf, PAGE_SIZE, "default direction = %d\n", _ptAccelHw->direction);

	return _tLength;
}

/*****************************************
 *** store_chip_orientation
 *****************************************/
static ssize_t store_chip_orientation(struct device_driver *ptDevDrv, const char *pbBuf, size_t tCount)
{
	int _nDirection = 0;
	int ret = 0;
	struct mc34xx_i2c_data   *_pt_i2c_obj = mc34xx_obj_i2c_data;

	ret = kstrtoint(pbBuf, 10, &_nDirection);
	if (ret != 0) {
		if (hwmsen_get_convert(_nDirection, &_pt_i2c_obj->cvt))
			GSE_ERR("ERR: fail to set direction\n");
	}

	GSE_LOG("[%s] set direction: %d\n", __func__, _nDirection);

	return tCount;
}

/*****************************************
 *** show_accuracy_status
 *****************************************/
static ssize_t show_accuracy_status(struct device_driver *ddri, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", s_bAccuracyStatus);
}

/*****************************************
 *** store_accuracy_status
 *****************************************/
static ssize_t store_accuracy_status(struct device_driver *ddri, const char *buf, size_t count)
{
	int	_nAccuracyStatus = 0;
	int ret = 0;

	ret = kstrtoint(buf, 10, &_nAccuracyStatus);
	if (ret != 0) {
		GSE_ERR("incorrect argument\n");
		return count;
	}

	if (SENSOR_STATUS_ACCURACY_HIGH < _nAccuracyStatus) {
		GSE_ERR("illegal accuracy status\n");
		return count;
	}

	s_bAccuracyStatus = ((int8_t) _nAccuracyStatus);

	return count;
}

/*****************************************
 *** show_chip_validate_value
 *****************************************/
static ssize_t show_chip_validate_value(struct device_driver *ptDevDriver, char *pbBuf)
{
	unsigned char	_bChipValidation = 0;

	_bChipValidation = s_bPCODE;

	return snprintf(pbBuf, PAGE_SIZE, "%d\n", _bChipValidation);
}

/*****************************************
 *** store_chip_register
 *****************************************/
static ssize_t store_chip_register(struct device_driver *ptDevDrv, const char *pbBuf, size_t tCount)
{
	int	_nRegister	= 0;
	int	_nValue		= 0;
	unsigned char	_baDataBuf[2] = { 0 };

	struct i2c_client	*_pt_i2c_client = mc34xx_i2c_client;

	sscanf(pbBuf, "%x %x", &_nRegister, &_nValue);

	GSE_LOG("[%s] _nRegister: 0x%02X, _nValue: 0x%02X\n", __func__, _nRegister, _nValue);

	_baDataBuf[0] = ((u8) _nRegister);
	_baDataBuf[1] = ((u8) _nValue);
	mc34xx_i2c_write_block(_pt_i2c_client, _baDataBuf[0], &_baDataBuf[1], 0x02);

	return (tCount);
}

/*****************************************
 *** DRIVER ATTRIBUTE LIST TABLE
 *****************************************/
static DRIVER_ATTR(chipinfo   ,			S_IRUGO, show_chipinfo_value,   NULL);
static DRIVER_ATTR(sensordata ,			S_IRUGO, show_sensordata_value, NULL);
static DRIVER_ATTR(selftest   ,			S_IWUSR | S_IRUGO, show_selftest_value,   store_selftest_value);
static DRIVER_ATTR(trace	  , 		S_IWUSR | S_IRUGO, show_trace_value,	  store_trace_value);
static DRIVER_ATTR(dtsconfig  ,		   	S_IRUGO, show_status_value,	 NULL);
static DRIVER_ATTR(regmap	  , 		S_IWUSR | S_IRUGO, show_regiter_map,	  store_regiter_map);
static DRIVER_ATTR(orientation, 		S_IWUSR | S_IRUGO, show_chip_orientation, store_chip_orientation);
static DRIVER_ATTR(accuracy   , 		S_IWUSR | S_IRUGO, show_accuracy_status , store_accuracy_status);
static DRIVER_ATTR(validate   ,		   	S_IRUGO, show_chip_validate_value, NULL);
static DRIVER_ATTR(reg	      ,			S_IWUSR | S_IRUGO, NULL					, store_chip_register);

static struct driver_attribute   *mc34xx_attr_list[] = {
							   &driver_attr_chipinfo,
							   &driver_attr_sensordata,
							   &driver_attr_selftest,
							   &driver_attr_trace,
							   &driver_attr_dtsconfig,
							   &driver_attr_regmap,
							   &driver_attr_orientation,
							   &driver_attr_accuracy,
							   &driver_attr_validate,
							   &driver_attr_reg,
							   };

/*****************************************
 *** mc34xx_create_attr
 *****************************************/
static int mc34xx_create_attr(struct device_driver *driver)
{
	int idx, err = 0;
	int num = (int)(sizeof(mc34xx_attr_list)/sizeof(mc34xx_attr_list[0]));

	if (driver == NULL)
		return -EINVAL;

	for (idx = 0; idx < num; idx++) {
		err = driver_create_file(driver, mc34xx_attr_list[idx]);
		if (err) {
			GSE_ERR("driver_create_file (%s) = %d\n", mc34xx_attr_list[idx]->attr.name, err);
			break;
		}
	}
	return err;
}

/*****************************************
 *** mc34xx_delete_attr
 *****************************************/
static int mc34xx_delete_attr(struct device_driver *driver)
{
	int idx , err = 0;
	int num = (int)(sizeof(mc34xx_attr_list)/sizeof(mc34xx_attr_list[0]));

	if (driver == NULL)
		return -EINVAL;

	for (idx = 0; idx < num; idx++)
		driver_remove_file(driver, mc34xx_attr_list[idx]);

	return err;
}

/*****************************************
 *** mc34xx_suspend
 *****************************************/
static int mc34xx_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mc34xx_i2c_data *obj = i2c_get_clientdata(client);
	int err = 0;

	GSE_LOG("mc34xx_suspend\n");

	atomic_set(&obj->suspend, 1);
	mc34xx_mutex_lock();
	err = MC34XX_SetPowerMode(client, false);
#ifdef CONFIG_ACCEL_HIT_HAPPEN_DETECT
	if(is_irq_enabled == false)
		enable_gsensor_irq();
#endif
	mc34xx_mutex_unlock();
	if (err) {
		GSE_ERR("write power control fail!!\n");
		return err;
	}
	return err;
}

/*****************************************
 *** mc34xx_resume
 *****************************************/
static int mc34xx_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mc34xx_i2c_data *obj = i2c_get_clientdata(client);
	int err;

	GSE_LOG("mc34xx_resume\n");
	if (obj == NULL) {
		GSE_ERR("null pointer!!\n");
		return -EINVAL;
	}

	mc34xx_mutex_lock();
	err = MC34XX_SetPowerMode(client, true);
	mc34xx_mutex_unlock();
	if (err) {
		GSE_ERR("write power control fail!!\n");
		return err;
	}
	atomic_set(&obj->suspend, 0);
	return 0;
}

/*****************************************
 *** mc34xx_i2c_auto_probe
 *****************************************/

static int mc34xx_i2c_auto_probe(struct i2c_client *client)
{
	#define _MC34XX_I2C_PROBE_ADDR_COUNT_	\
		(sizeof(mc34xx_i2c_auto_probe_addr) / sizeof(mc34xx_i2c_auto_probe_addr[0]))
	unsigned char	_baData1Buf[2] = { 0 };
	unsigned char	_baData2Buf[2] = { 0 };
	int _nCount = 0;
	int _naCheckCount[_MC34XX_I2C_PROBE_ADDR_COUNT_] = { 0 };

	memset(_naCheckCount, 0, sizeof(_naCheckCount));
_I2C_AUTO_PROBE_RECHECK_:
	s_bPCODE  = 0x00;
	s_bHWID   = 0x00;

	for (_nCount = 0; _nCount < _MC34XX_I2C_PROBE_ADDR_COUNT_; _nCount++) {
		client->addr = mc34xx_i2c_auto_probe_addr[_nCount];
		_baData1Buf[0] = 0;

		if (0 > mc34xx_i2c_read_block(client, MC34XX_REG_PRODUCT_CODE_L, _baData1Buf, 1))
			continue;
		_naCheckCount[_nCount]++;

		if (0x00 == _baData1Buf[0]) {
			if (1 == _naCheckCount[_nCount]) {
				mdelay(3);
				goto _I2C_AUTO_PROBE_RECHECK_;
			} else
				continue;
		}

		mc34xx_i2c_read_block(client, MC34XX_REG_CHIPID, _baData2Buf, 1);

		if (MC34XX_RETCODE_SUCCESS == MC34XX_ValidateSensorIC(&_baData1Buf[0], &_baData2Buf[0])) {
			s_bPCODE = _baData1Buf[0];
			s_bHWID  = _baData2Buf[0];
			GSE_LOG("[%s] s_bHWID=%02X,s_bPCODE=%02X\n", __func__, s_bHWID, s_bPCODE);

			return MC34XX_RETCODE_SUCCESS;
		}
	}

	return MC34XX_RETCODE_ERROR_I2C;
	#undef _MC34XX_I2C_PROBE_ADDR_COUNT_
}
/* if use  this typ of enable , Gsensor should report inputEvent(x, y, z ,stats, div) to HAL */
static int mc34xx_accel_open_report_data(int open)
{
	/* should queuq work to report event if  is_report_input_direct=true */
	return 0;
}

/* if use  this typ of enable , Gsensor only enabled but not report inputEvent to HAL */
static int mc34xx_accel_enable_nodata(int en)
{
	int res = 0;
	int retry = 0;
	bool power = false;

	if (1 == en)
		power = true;
	if (0 == en)
		power = false;

	for (retry = 0; retry < 3; retry++) {
		res = MC34XX_SetPowerMode(mc34xx_obj_i2c_data->client, power);
		if (res == 0) {
			GSE_LOG("MC34XX_SetPowerMode done\n");
			break;
		}
		GSE_LOG("MC34XX_SetPowerMode fail\n");
	}

	if (res != 0) {
		GSE_LOG("MC34XX_SetPowerMode fail!\n");
		return -1;
	}
	GSE_LOG("MC34XX_enable_nodata OK!\n");
	return 0;
}

static int mc34xx_accel_set_delay(u64 ns)
{
	int value = 0;

	value = (int)ns/1000/1000;
	GSE_LOG("mc34xx_accel_set_delay (%d), chip only use 1024HZ\n", value);
	return 0;
}

static int mc34xx_accel_get_data(int *x , int *y, int *z, int *status)
{
	char buff[MC34XX_BUF_SIZE] = {0};
	int ret;

	MC34XX_ReadSensorData(mc34xx_obj_i2c_data->client, buff, MC34XX_BUF_SIZE);
	ret = sscanf(buff, "%x %x %x", x, y, z);
	*status = SENSOR_STATUS_ACCURACY_MEDIUM;

	return 0;
}
#if defined(MC34XX_SUPPORT_ANDROID_O)
static int mc34xx_accel_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	int value = 0;

	value = (int)samplingPeriodNs/1000/1000;
	GSE_LOG("mc34xx_batch (%d), chip only use 1024HZ\n", value);
	return 0;
}

static int mc34xx_accel_flush(void)
{
	return acc_flush_report();
}

static int mc34xx_factory_enable_sensor(bool enabledisable, int64_t sample_periods_ms)
{
	int err;

	err = mc34xx_accel_enable_nodata(enabledisable == true ? 1 : 0);
	if (err) {
		GSE_ERR("%s enable sensor failed!\n", __func__);
		return -1;
	}
	err = mc34xx_accel_batch(0, sample_periods_ms * 1000000, 0);
	if (err) {
		GSE_ERR("%s enable set batch failed!\n", __func__);
		return -1;
	}
	return 0;
}
static int mc34xx_factory_get_data(int32_t data[3], int *status)
{
	return mc34xx_accel_get_data(&data[0], &data[1], &data[2], status);

}
static int mc34xx_factory_get_raw_data(int32_t data[3])
{
	char strbuf[MC34XX_BUF_SIZE] = { 0 };

	MC34XX_ReadRawData(mc34xx_i2c_client, strbuf);
	GSE_LOG("support mc34xx_factory_get_raw_data!\n");
	return 0;
}
static int mc34xx_factory_enable_calibration(void)
{
	return 0;
}
static int mc34xx_factory_clear_cali(void)
{
	int err = 0;

	err = MC34XX_ResetCalibration(mc34xx_i2c_client);
	if (err) {
		GSE_ERR("mc34xx_ResetCalibration failed!\n");
		return -1;
	}
	return 0;
}
static int mc34xx_factory_set_cali(int32_t data[3])
{
	int err = 0;
	int cali[3] = { 0 };
    struct SENSOR_DATA nvram_cali_data = {0};

    nvram_cali_data.x = data[0];
    nvram_cali_data.y = data[1];
    nvram_cali_data.z = data[2];

    err = MC34XX_CaliConvert(&nvram_cali_data);//6 directions calibration support
	if(err != 0)
	    return -1;

	cali[MC34XX_AXIS_X] = nvram_cali_data.x * gsensor_gain.x / GRAVITY_EARTH_1000;
	cali[MC34XX_AXIS_Y] = nvram_cali_data.y * gsensor_gain.y / GRAVITY_EARTH_1000;
	cali[MC34XX_AXIS_Z] = nvram_cali_data.z * gsensor_gain.z / GRAVITY_EARTH_1000;
	err = MC34XX_WriteCalibration(mc34xx_i2c_client, cali);
	if (err) {
		GSE_ERR("mc34xx_WriteCalibration failed!\n");
		return -1;
	}
	return 0;
}
static int mc34xx_factory_get_cali(int32_t data[3])
{
	int err = 0;
	int cali[3] = { 0 };
	
	if((err = MC34XX_ReadCalibration(mc34xx_i2c_client, cali)))
		return err;

	data[0] = cali[MC34XX_AXIS_X] * GRAVITY_EARTH_1000 / gsensor_gain.x;
	data[1] = cali[MC34XX_AXIS_Y] * GRAVITY_EARTH_1000 / gsensor_gain.y;
	data[2] = cali[MC34XX_AXIS_Z] * GRAVITY_EARTH_1000 / gsensor_gain.z;
	return 0;
}
static int mc34xx_factory_do_self_test(void)
{
	return 0;
}

static struct accel_factory_fops mc34xx_factory_fops = {
	.enable_sensor = mc34xx_factory_enable_sensor,
	.get_data = mc34xx_factory_get_data,
	.get_raw_data = mc34xx_factory_get_raw_data,
	.enable_calibration = mc34xx_factory_enable_calibration,
	.clear_cali = mc34xx_factory_clear_cali,
	.set_cali = mc34xx_factory_set_cali,
	.get_cali = mc34xx_factory_get_cali,
	.do_self_test = mc34xx_factory_do_self_test,
};

static struct accel_factory_public mc34xx_factory_device = {
	.gain = 1,
	.sensitivity = 1,
	.fops = &mc34xx_factory_fops,
};
#endif
/*****************************************
 *** mc34xx_i2c_probe
 *****************************************/
static int mc34xx_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_client *new_client;
	struct mc34xx_i2c_data *obj;
	struct acc_control_path ctl = {0};
	struct acc_data_path data = {0};
	int err = 1;

	GSE_LOG("mc34xx_i2c_probe\n");

	if (MC34XX_RETCODE_SUCCESS != mc34xx_i2c_auto_probe(client))
		goto exit;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		err = -ENOMEM;
		goto exit;
	}
	
	err = get_accel_dts_func(client->dev.of_node, hw);
	if (err < 0) {
		GSE_ERR("get cust_baro dts info fail\n");
		goto exit_kfree;
	}

	obj->hw = hw;
	err = hwmsen_get_convert(obj->hw->direction, &obj->cvt);
	if (err) {
		GSE_ERR("invalid direction: %d\n", obj->hw->direction);
		goto exit_kfree;
	}

	mc34xx_obj_i2c_data = obj;
	obj->client = client;
	new_client = obj->client;
	i2c_set_clientdata(new_client, obj);
	atomic_set(&obj->trace, 0);

	mc34xx_i2c_client = new_client;

	err = MC34XX_Init(new_client, 1);
	if (err)
		goto exit_init_failed;
	mc34xx_mutex_init();
	
	err = accel_factory_device_register(&mc34xx_factory_device);
	if (err) {
		GSE_ERR("mc34xx_factory_device register failed\n");
		goto exit_mc34xx_factory_device_register_failed;
	}

	ctl.is_use_common_factory = false;
	err = mc34xx_create_attr(&(mc34xx_init_info.platform_diver_addr->driver));
	if (err) {
		GSE_ERR("create attribute err = %d\n", err);
		goto exit_create_attr_failed;
	}

	ctl.open_report_data = mc34xx_accel_open_report_data;
	ctl.enable_nodata = mc34xx_accel_enable_nodata;
	ctl.set_delay  = mc34xx_accel_set_delay;
#if defined(MC34XX_SUPPORT_ANDROID_O)
	ctl.batch = mc34xx_accel_batch;
	ctl.flush = mc34xx_accel_flush;
#endif
	ctl.is_report_input_direct = false;
	ctl.is_support_batch = obj->hw->is_batch_supported;
	err = acc_register_control_path(&ctl);
	if (err) {
		GSE_ERR("register acc control path err\n");
		goto exit_kfree;
	}
	data.get_data = mc34xx_accel_get_data;
	data.vender_div = 1000;
	err = acc_register_data_path(&data);
	if (err) {
		GSE_ERR("register acc data path err= %d\n", err);
		goto exit_kfree;
	}

	GSE_LOG("%s: OK\n", __func__);
	s_nInitFlag = MC34XX_INIT_SUCC;
	return 0;

exit_create_attr_failed:
	accel_factory_device_deregister(&mc34xx_factory_device);
	exit_mc34xx_factory_device_register_failed:
exit_init_failed:
exit_kfree:
	kfree(obj);
	obj = NULL;
exit:
	GSE_ERR("%s: err = %d\n", __func__, err);
	s_nInitFlag = MC34XX_INIT_FAIL;

	return err;
}

/*****************************************
 *** mc34xx_i2c_remove
 *****************************************/
static int mc34xx_i2c_remove(struct i2c_client *client)
{
	int err = 0;

	err = mc34xx_delete_attr(&(mc34xx_init_info.platform_diver_addr->driver));
	if (err)
		GSE_ERR("mc34xx_delete_attr fail: %d\n", err);
	accel_factory_device_deregister(&mc34xx_factory_device);
	mc34xx_i2c_client = NULL;
	i2c_unregister_device(client);
	kfree(i2c_get_clientdata(client));

	return 0;
}

/*****************************************
 *** mc34xx_remove
 *****************************************/
static int mc34xx_remove(void)
{
	GSE_LOG("mc34xx_remove\n");
	i2c_del_driver(&mc34xx_i2c_driver);

	return 0;
}

/*****************************************
 *** mc34xx_local_init
 *****************************************/
static int  mc34xx_local_init(void)
{
	GSE_LOG("mc34xx_local_init\n");


	if (i2c_add_driver(&mc34xx_i2c_driver)) {
		GSE_ERR("add driver error\n");
		return -1;
	}

	if (MC34XX_INIT_FAIL == s_nInitFlag)
		return -1;
	return 0;
}

/*****************************************
 *** mc34xx_init
 *****************************************/
static int __init mc34xx_init(void)
{
	acc_driver_add(&mc34xx_init_info);

	return 0;
}

/*****************************************
 *** mc34xx_exit
 *****************************************/
static void __exit mc34xx_exit(void)
{
	GSE_LOG("mc34xx_exit\n");
}

/*----------------------------------------------------------------------------*/
module_init(mc34xx_init);
module_exit(mc34xx_exit);
/*----------------------------------------------------------------------------*/
MODULE_DESCRIPTION("MC34XX G-Sensor Driver");
MODULE_AUTHOR("mCube-inc");
MODULE_LICENSE("GPL");
MODULE_VERSION(MC34XX_DEV_DRIVER_VERSION);

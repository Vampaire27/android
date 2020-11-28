/*****************************************************************************
 *
 * Filename:
 * ---------
 *     pr2100mipiyuv_Sensor.h
 *
 * Project:
 * --------
 *     ALPS
 *
 * Description:
 * ------------
 *     PR2100 YUV to MIPI IC driver header file
 *
 ****************************************************************************/
#ifndef _PR2100MIPIYUV_SENSOR_H
#define _PR2100MIPIYUV_SENSOR_H

enum INPUT_SIZE{
	SIZE_720X240 = 0,
	SIZE_1280X720,
	SIZE_720X480,
	SIZE_720X288,
	SIZE_720X576,
	SIZE_960X480,
	SIZE_960X576,
	SIZE_1280X960,
	SIZE_1920X1080
};

enum CAM_360_TYPE{
	AHD_25FPS = 1,
	AHD_30FPS,
	CVBS_NTSC,
	CVBS_PAL,
	FHD_25FPS,
	FHD_30FPS,
	TVI_INPUT,
	CVI_INPUT,
	PVI_INPUT,

	MAIN_NTSC_SUB_NTSC = 11,
	MAIN_NTSC_SUB_PAL,
	MAIN_NTSC_SUB_HD,
	MAIN_NTSC_SUB_FHD,

	MAIN_PAL_SUB_NTSC = 21,
	MAIN_PAL_SUB_PAL,
	MAIN_PAL_SUB_HD,
	MAIN_PAL_SUB_FHD,

	MAIN_HD_SUB_NTSC = 31,
	MAIN_HD_SUB_PAL,
	MAIN_HD_SUB_HD,
	MAIN_HD_SUB_FHD,

	MAIN_FHD_SUB_NTSC = 41,
	MAIN_FHD_SUB_PAL,
	MAIN_FHD_SUB_HD,
	MAIN_FHD_SUB_FHD,
};

extern int iReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 * a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
extern int iWriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);

extern int iReadRegI2C_pr2100s(u8 *a_pSendData , u16 a_sizeSendData, u8 * a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
extern int iWriteRegI2C_pr2100s(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);

#endif

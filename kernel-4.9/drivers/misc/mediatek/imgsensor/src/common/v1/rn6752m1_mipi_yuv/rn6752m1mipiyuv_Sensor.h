/*****************************************************************************
 *
 * Filename:
 * ---------
 *     pr2001mipiyuv_Sensor.h
 *
 * Project:
 * --------
 *     ALPS
 *
 * Description:
 * ------------
 *     RN6752M1 YUV to MIPI IC driver header file
 *
 ****************************************************************************/
#ifndef _RN6752M1MIPIYUV_SENSOR_H
#define _RN6752M1MIPIYUV_SENSOR_H

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

enum VIDEO_STANDARD{
	ST_CVBS = 0,
	ST_HD_PVI,
	ST_HD_CVI,
	ST_HDA,
	ST_HDT,
	ST_UNKOWN
};

enum REFRESH_RATE{
	RATE_25HZ = 0x10,
	RATE_30HZ = 0x20,
	RATE_50HZ = 0x30,
	RATE_60HZ = 0x40
};

enum INPUT_PORT{
	SRC_CAMERA = 0,
	SRC_AUX
};

enum CAMERA_STATUS{
	CAMERA_PLUG_OUT = 0,
	CAMERA_PLUG_IN,
	CAMERA_UNKNOW
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

enum CAM_STATUS_TYPE{
	CVBS_NTSC_60HZ = 0,
	CVBS_PAL_50HZ,

	AHD_720P_25HZ = 10,
	AHD_720P_30HZ,
	AHD_720P_50HZ,
	AHD_720P_60HZ,
	AHD_960P_25HZ,
	AHD_960P_30HZ,
	AHD_960P_50HZ,
	AHD_960P_60HZ,
	AHD_1080P_25HZ,
	AHD_1080P_30HZ,

	TVI_720P_25HZ = 20,
	TVI_720P_30HZ,
	TVI_720P_50HZ,
	TVI_720P_60HZ,
	TVI_960P_25HZ,
	TVI_960P_30HZ,
	TVI_960P_50HZ,
	TVI_960P_60HZ,
	TVI_1080P_25HZ,
	TVI_1080P_30HZ,

	CVI_720P_25HZ = 30,
	CVI_720P_30HZ,
	CVI_720P_50HZ,
	CVI_720P_60HZ,
	CVI_960P_25HZ,
	CVI_960P_30HZ,
	CVI_960P_50HZ,
	CVI_960P_60HZ,
	CVI_1080P_25HZ,
	CVI_1080P_30HZ,

	PVI_720P_25HZ =40,
	PVI_720P_30HZ,
	PVI_720P_50HZ,
	PVI_720P_60HZ,
	PVI_960P_25HZ,
	PVI_960P_30HZ,
	PVI_960P_50HZ,
	PVI_960P_60HZ,
	PVI_1080P_25HZ,
	PVI_1080P_30HZ,

	CAM_TYPE_UNKNOW = 50,
	CAM_NO_SIGNAL,
	CAM_HARDWARE_ERROR
};

enum CAM_WORK_STATUS{
	CAM_WORK = 0,
	CAM_STOP
};

extern int iReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 * a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
extern int iWriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);
extern void kdSetI2CSpeed(u16 i2cSpeed);
//#define RN6752M1_CVBS_WIDTH_960

#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include <sys/mman.h>
#include <time.h>
#include "libwwc2StereoWaterMask.h"
#include "waterMaskData.h"

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

enum VIDEO_MIRROR{
	DUAL_NORMAL = 0,
	SUB_MIRROR = 1,
	MAIN_MIRROR = 10,
	DUAL_MIRROR = 11,
};

enum VIDEO_SIZE{
	VIDEO_1920x1080 = 1,
	VIDEO_1280x720,
	VIDEO_960x540,
	VIDEO_854x480,
	VIDEO_720x576,
	VIDEO_720x480,
	VIDEO_640x360,
	VIDEO_720x288,
	VIDEO_720x240
};

enum VIDEO_RESIZE_SCALE
{
	VR1080P_TO_1080P = 0,
	VR1080P_TO_720P,
	VR1080P_TO_540P,
	VR1080P_TO_360P,

	VR720P_TO_720P,
	VR720P_TO_480P,
	VR720P_TO_360P,
	UNDEFINED
};

enum H264_SCALE
{
	H264_ONE,
	H264_ONE_AND_HALF,
	H264_ONE_AND_ONE_THIRD,
	H264_TWO,
	H264_THREE	
};

static int isEnableWaterMark = 0;
static int mirrorFlag = 0;
static unsigned int shareMainMemSize = 0;
static unsigned int shareSubMemSize = 0;
static unsigned int shareMainH264Size = 0;
static unsigned int shareSubH264Size = 0;
static enum H264_SCALE mMainScale = H264_ONE;
static enum H264_SCALE mSubScale = H264_ONE;

static unsigned char *shareMainMem = NULL;
static unsigned char *shareSubMem = NULL;
static unsigned char *shareMainH264 = NULL;
static unsigned char *shareSubH264 = NULL;
static bool isInit = false;
static const unsigned char waterMask_20x32[][20*32] = {ZERO_20X32, ONE_20X32, TWO_20X32, THREE_20X32, FOUR_20X32, FIVE_20X32,\
									SIX_20X32, SEVEN_20X32, EIGHT_20X32, NINE_20X32, COLON_20X32, SLASH_20X32};

static const unsigned char waterMaskFB_40x40[][40*40] = {FRONT_40X40, BACK_40X40};

int getCameraFeatureFlag(const char* node)
{
	char flag[8] = {'\0'};
	FILE *fd = NULL;
	int value = -1;

	fd = fopen(node,"r");
	if(fd)
	{
		fread(&flag,1,8,fd);
		fclose(fd);
		value = atoi(flag);
	}
	return value;
}

static enum INPUT_SIZE getVideoSize(const int sensorId, const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;

	if(sensorId == 0)//main
	{
		switch(cameraType)
		{
			case MAIN_NTSC_SUB_NTSC:
			case MAIN_NTSC_SUB_PAL:
			case MAIN_NTSC_SUB_HD:
			case MAIN_NTSC_SUB_FHD:
			case CVBS_NTSC:
				size = SIZE_720X480;
				break;
			case MAIN_PAL_SUB_NTSC:
			case MAIN_PAL_SUB_PAL:
			case MAIN_PAL_SUB_HD:
			case MAIN_PAL_SUB_FHD:
			case CVBS_PAL:
				size = SIZE_720X576;
				break;
			case MAIN_HD_SUB_NTSC:
			case MAIN_HD_SUB_PAL:
			case MAIN_HD_SUB_HD:
			case MAIN_HD_SUB_FHD:
			case AHD_25FPS:
			case AHD_30FPS:
				size = SIZE_1280X720;
				break;
			case MAIN_FHD_SUB_NTSC:
			case MAIN_FHD_SUB_PAL:
			case MAIN_FHD_SUB_HD:
			case MAIN_FHD_SUB_FHD:
			case FHD_25FPS:
			case FHD_30FPS:
				size = SIZE_1920X1080;
				break;
			default:
				size = SIZE_1280X720;
				break;
		}
	}
	else if(sensorId == 2)//main2
	{
		switch(cameraType)
		{
			case MAIN_NTSC_SUB_NTSC:
			case MAIN_PAL_SUB_NTSC:
			case MAIN_HD_SUB_NTSC:
			case MAIN_FHD_SUB_NTSC:
				size = SIZE_720X480;
				break;
			case MAIN_NTSC_SUB_PAL:
			case MAIN_PAL_SUB_PAL:
			case MAIN_HD_SUB_PAL:
			case MAIN_FHD_SUB_PAL:
				size = SIZE_720X576;
				break;
			case MAIN_NTSC_SUB_HD:
			case MAIN_PAL_SUB_HD:
			case MAIN_HD_SUB_HD:
			case MAIN_FHD_SUB_HD:
				size = SIZE_1280X720;
				break;
			case MAIN_NTSC_SUB_FHD:
			case MAIN_PAL_SUB_FHD:
			case MAIN_HD_SUB_FHD:
			case MAIN_FHD_SUB_FHD:
				size = SIZE_1920X1080;
				break;
			default:
				size = SIZE_1280X720;
				break;
		}
	}
	return size;
}

static enum VIDEO_SIZE getUserSetVideoSize(const enum INPUT_SIZE size)
{
	enum VIDEO_SIZE videoSize = VIDEO_1280x720;
	int videoUserSet = 0;
	int mainVideoUserSet = 0;
	int subVideoUserSet = 0;

	mainVideoUserSet = property_get_int32("wwc2.main.video.user.set",0);
	subVideoUserSet = property_get_int32("wwc2.sub.video.user.set",0);

	if(mainVideoUserSet >= 0 && mainVideoUserSet < 8 && subVideoUserSet >= 0 && subVideoUserSet < 8)
	{
		if(mainVideoUserSet > subVideoUserSet)
			videoUserSet = mainVideoUserSet;
		else
			videoUserSet = subVideoUserSet;
	}

	if(videoUserSet)
	{
		switch(size)
		{
			case SIZE_720X240:
			case SIZE_720X480:
				videoSize = VIDEO_720x480;
				break;
			case SIZE_720X288:
			case SIZE_720X576:
				videoSize = VIDEO_720x576;
				break;
			case SIZE_1280X720:
				if(videoUserSet == VIDEO_1280x720
				||videoUserSet == VIDEO_854x480
				|| videoUserSet == VIDEO_640x360)
					videoSize = (enum VIDEO_SIZE)videoUserSet;
				break;
			case SIZE_1920X1080:
				if(videoUserSet == VIDEO_1920x1080
				|| videoUserSet == VIDEO_1280x720
				|| videoUserSet == VIDEO_960x540
				|| videoUserSet == VIDEO_640x360)
					videoSize = (enum VIDEO_SIZE)videoUserSet;
				break;
			default:
				break;
		}
	}
	else
	{
		switch(size)
		{
			case SIZE_720X240:
			case SIZE_720X480:
				videoSize = VIDEO_720x480;
				break;
			case SIZE_720X288:
			case SIZE_720X576:
				videoSize = VIDEO_720x576;
				break;
			case SIZE_1280X720:
				videoSize = VIDEO_1280x720;
				break;
			case SIZE_1920X1080:
				videoSize = VIDEO_1920x1080;
				break;
			default:
				break;
		}
	}

	return videoSize;
}

static unsigned int getVideoMemSize(const enum VIDEO_SIZE videoSize)
{
	unsigned int shareMemSize = 0;

	switch(videoSize)
	{
		case VIDEO_720x240:
			shareMemSize = 64*0x1000;
			break;
		case VIDEO_720x288:
			shareMemSize = 76*0x1000;
			break;
		case VIDEO_640x360:
			shareMemSize = 85*0x1000;
			break;
		case VIDEO_720x480:
			shareMemSize = 127*0x1000;
			break;
		case VIDEO_720x576:
			shareMemSize = 152*0x1000;
			break;
		case VIDEO_854x480:
			shareMemSize = 151*0x1000;
			break;
		case VIDEO_960x540:
			shareMemSize = 190*0x1000;
			break;
		case VIDEO_1280x720:
			shareMemSize = 338*0x1000;
			break;
		case VIDEO_1920x1080:
			shareMemSize = 760*0x1000;
			break;
		default:
			shareMemSize = 338*0x1000;
			break;
	}
	return shareMemSize;
}

static unsigned int getCaptureMemSize(const enum INPUT_SIZE size)
{
	unsigned int shareMemSize = 0;

	switch(size)
	{
		case SIZE_720X240:
			shareMemSize = 64*0x1000;
			break;
		case SIZE_720X288:
			shareMemSize = 76*0x1000;
			break;
		case SIZE_720X480:
			shareMemSize = 127*0x1000;
			break;
		case SIZE_720X576:
			shareMemSize = 152*0x1000;
			break;
		case SIZE_1280X720:
			shareMemSize = 338*0x1000;
			break;
		case SIZE_1920X1080:
			shareMemSize = 760*0x1000;
			break;
		default:
			shareMemSize = 338*0x1000;
			break;
	}

	return shareMemSize;
}

static unsigned int getH264MemSize(const enum VIDEO_SIZE videoSize)
{
	unsigned int shareMemSize = 0;

	switch(videoSize)
	{
		case VIDEO_720x240:
			shareMemSize = 64*0x1000;
			break;
		case VIDEO_720x288:
			shareMemSize = 76*0x1000;
			break;
		case VIDEO_720x480:
			shareMemSize = 127*0x1000;
			break;
		case VIDEO_720x576:
			shareMemSize = 152*0x1000;
			break;
		case VIDEO_640x360:
		case VIDEO_854x480:
		case VIDEO_960x540:
		case VIDEO_1280x720:
		case VIDEO_1920x1080:
			shareMemSize = 85*0x1000;
			break;
		default:
			shareMemSize = 85*0x1000;
			break;
	}

	return shareMemSize;
}

static enum H264_SCALE getH264Scale(enum VIDEO_SIZE videoSize)
{
	enum H264_SCALE h264Scale = H264_ONE;

	switch(videoSize)
	{
		case VIDEO_1920x1080:
			h264Scale = H264_THREE;
			break;
		case VIDEO_1280x720:
			h264Scale = H264_TWO;
			break;
		case VIDEO_960x540:
			h264Scale = H264_ONE_AND_HALF;
			break;
		case VIDEO_854x480:
			h264Scale = H264_ONE_AND_ONE_THIRD;
			break;
		case VIDEO_640x360:
		case VIDEO_720x240:
		case VIDEO_720x288:
		case VIDEO_720x480:
		case VIDEO_720x576:
			h264Scale = H264_ONE;
			break;
		default:
			h264Scale = H264_ONE;
			break;
	}

	return h264Scale;
}

static enum VIDEO_RESIZE_SCALE getVideoScale(const int sensorId, const enum CAM_360_TYPE cameraType)
{
	enum VIDEO_RESIZE_SCALE videoScale = VR720P_TO_720P;
	enum INPUT_SIZE size = getVideoSize(sensorId, cameraType);
	enum VIDEO_SIZE videoSize = getUserSetVideoSize(size);

	if(size == SIZE_1920X1080)
	{
		switch(videoSize)
		{
			case VIDEO_1920x1080:
				videoScale = VR1080P_TO_1080P;
				break;
			case VIDEO_1280x720:
				videoScale = VR1080P_TO_720P;
				break;
			case VIDEO_960x540:
				videoScale = VR1080P_TO_540P;
				break;
			case VIDEO_640x360:
				videoScale = VR1080P_TO_360P;
				break;
			default:
				break;
		}
	}
	else if(size == SIZE_1280X720)
	{
		switch(videoSize)
		{
			case VIDEO_1280x720:
				videoScale = VR720P_TO_720P;
				break;
			case VIDEO_854x480:
				videoScale = VR720P_TO_480P;
				break;
			case VIDEO_640x360:
				videoScale = VR720P_TO_360P;
				break;
			default:
				break;
		}
	}

	return videoScale;
}

void getStereoCameraSize(int* width, int* height, const int sensorId)
{
	enum CAM_360_TYPE cameraType = (enum CAM_360_TYPE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/360_camtype");
	enum INPUT_SIZE size = getVideoSize(sensorId, cameraType);
	enum VIDEO_SIZE videoSize = getUserSetVideoSize(size);

	if(width && height)
	{
		switch(videoSize)
		{
			case VIDEO_1920x1080:
				*width = 1920;
				*height = 1080;
				break;
			case VIDEO_1280x720:
				*width = 1280;
				*height = 720;
				break;
			case VIDEO_960x540:
				*width = 960;
				*height = 540;
				break;
			case VIDEO_854x480:
				*width = 854;
				*height = 480;
				break;
			case VIDEO_640x360:
				*width = 640;
				*height = 360;
				break;
			case VIDEO_720x240:
				*width = 720;
				*height = 240;
				break;
			case VIDEO_720x288:
				*width = 720;
				*height = 288;
				break;
			case VIDEO_720x480:
				*width = 720;
				*height = 480;
				break;
			case VIDEO_720x576:
				*width = 720;
				*height = 576;
				break;
			default:
				*width = 1280;
				*height = 720;
				break;
		}
	}
}

static void getVideoShareMemSize(unsigned int* memSize, const int sensorId, const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	enum VIDEO_SIZE videoSize =VIDEO_1280x720;

	size = getVideoSize(sensorId, cameraType);
	videoSize = getUserSetVideoSize(size);
	*memSize = getVideoMemSize(videoSize);
}

static void getCaptureShareMemSize(unsigned int* memSize, const int sensorId, const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;

	size = getVideoSize(sensorId, cameraType);
	*memSize = getCaptureMemSize(size);
}

static void getH264ShareMemSize(unsigned int* memSize, const int sensorId, enum H264_SCALE *scale, const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	enum VIDEO_SIZE videoSize =VIDEO_1280x720;

	size = getVideoSize(sensorId, cameraType);
	videoSize = getUserSetVideoSize(size);
	*memSize = getH264MemSize(videoSize);
	*scale = getH264Scale(videoSize);
}

static void getTimeString(char* mTime)
{
	struct tm *p = NULL;
	time_t timer = time(NULL);
	p = localtime(&timer);
	strftime(mTime,20,"%F  %T",p);
}

static void waterMarkFunc(unsigned char* dstStart, const unsigned int dstWidth, unsigned char* source, const unsigned int sourceWidth, const unsigned int sourceHeight)
{
	unsigned int i = 0;
	unsigned int j = 0;
	for(i = 0; i < sourceHeight; i++)
	{
		for(j = 0; j < sourceWidth; j++)
		{
			if(*(source+sourceWidth*i+j) < 128)
			{
				//*(dstStart+(dstWidth*i+j)*2+1) = 0xff;//0x0;UYVY
				*(dstStart+dstWidth*i+j) = 0xff;//0x0;YUV420
			}
		}
	}
}

static void timeWaterMark(unsigned char* imgStartAddr, const unsigned int startPointX, const unsigned int startPointY, const unsigned int width, const unsigned int heigth)
{
	char waterMark[21] = {'\0'};
	unsigned int waterMarkIndex = 0;
	unsigned char *waterMarkData = NULL;
	unsigned char *waterMarkStartAddr = NULL;

	if(startPointX > width || startPointY > heigth)
	{
		ALOGE("point outside limit 0");
		return;
	}

	if(startPointX > width - 400 || startPointY > heigth - 40)
	{
		ALOGE("point outside limit 1");
		return;
	}

	waterMarkStartAddr = imgStartAddr + startPointY*width + startPointX;//YUV420
	//waterMarkStartAddr = imgStartAddr + (startPointY*width + startPointX)*2;//UYVY

	getTimeString(waterMark);
	for(waterMarkIndex = 0; waterMarkIndex < 20; waterMarkIndex++)
	{
		switch(waterMark[waterMarkIndex])
		{
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				waterMarkData = waterMask_20x32[waterMark[waterMarkIndex] - '0'];
				break;
			case '-':
				waterMarkData = waterMask_20x32[11];
				break;
			case ':':
				waterMarkData = waterMask_20x32[10];
				break;
			case ' ':
				waterMarkData = NULL;
				break;
			default:
				break;
		}
		if(waterMarkData)
			waterMarkFunc(waterMarkStartAddr + 20*waterMarkIndex, width, waterMarkData, 20, 32);
	}
}

static void doH264ScaleInt(unsigned char *srcY, unsigned char *srcU, unsigned char *srcV, unsigned char *dst, const unsigned int width, const unsigned int height, const int scale)
{
	const unsigned int uvHeight = height/2;
	const unsigned int uvWidth = width/2;
	const unsigned char *srcYStartAddr = srcY;
	const unsigned char *srcUStartAddr = srcU;
	const unsigned char *srcVStartAddr = srcV;

	unsigned char *y = dst;
	unsigned char *uv = y + (width/scale) * (height/scale);

	unsigned int indexY = 0;
	unsigned int indexUV = 0;
	unsigned int w, h;

	for(h = 0; h < height; h+=scale)//Y
	{
		for(w = 0; w < width; w+=scale)
		*(y+indexY++) = *(srcYStartAddr + width*h + w);
	}

	for(h = 0; h < uvHeight; h+=scale)
	{
		for(w = 0; w < uvWidth; w+=scale)
		{
			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*h + w);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*h +w);
			//*(v+indexV++) = *(srcVStartAddr + uvWidth*h + w);

		}
	}
}

static void doH264ScaleOneHalf(unsigned char *srcY, unsigned char *srcU, unsigned char *srcV, unsigned char *dst, const unsigned int width, const unsigned int height)
{
	const unsigned int uvHeight = height/2;
	const unsigned int uvWidth = width/2;
	const unsigned char *srcYStartAddr = srcY;
	const unsigned char *srcUStartAddr = srcU;
	const unsigned char *srcVStartAddr = srcV;

	unsigned char *y = dst;
	unsigned char *uv = y + (width*2/3) * (height*2/3);
	unsigned int indexY = 0;
	unsigned int indexUV = 0;
	unsigned int w, h;


	for(h = 0; h < height; h+=3)
	{
		for(w = 0; w < width; w+=3)
		{
			*(y+indexY++) = *(srcYStartAddr + width*h + w);
			*(y+indexY++) = *(srcYStartAddr + width*h + w+1);
		}

		for(w = 0; w < width; w+=3)
		{
			*(y+indexY++) = *(srcYStartAddr + width*(h+1) + w);
			*(y+indexY++) = *(srcYStartAddr + width*(h+1) + w+1);
		}
	}

	for(h = 0; h < uvHeight; h+=3)
	{
		for(w = 0; w < uvWidth; w+=3)
		{
			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*h + w);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*h + w);

			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*h + w+1);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*h + w+1);
		}

		for(w = 0; w < uvWidth; w+=3)
		{
			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*(h+1) + w);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*(h+1) + w);

			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*(h+1) + w+1);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*(h+1) + w+1);
		}

	}

}

static void doH264ScaleOneThrid(unsigned char *srcY, unsigned char *srcU, unsigned char *srcV, unsigned char *dst, const unsigned int width, const unsigned int height)
{
	const unsigned int uvHeight = height/2;
	const unsigned int uvWidth = width/2;
	const unsigned char *srcYStartAddr = srcY;
	const unsigned char *srcUStartAddr = srcU;
	const unsigned char *srcVStartAddr = srcV;

	unsigned char *y = dst;
	unsigned char *uv = y + (width*3/4) * (height*3/4);
	unsigned int indexY = 0;
	unsigned int indexUV = 0;
	unsigned int w, h;


	for(h = 0; h < height; h+=4)
	{
		for(w = 0; w < width; w+=4)
		{
			*(y+indexY++) = *(srcYStartAddr + width*h + w);
			if(w+4 < width)
			{
				*(y+indexY++) = *(srcYStartAddr + width*h + w+1);
				*(y+indexY++) = *(srcYStartAddr + width*h + w+2);
			}
		}

		for(w = 0; w < width; w+=4)
		{
			*(y+indexY++) = *(srcYStartAddr + width*(h+1) + w);
			if(w+4 < width)
			{
				*(y+indexY++) = *(srcYStartAddr + width*(h+1) + w+1);
				*(y+indexY++) = *(srcYStartAddr + width*(h+1) + w+2);
			}
		}

		for(w = 0; w < width; w+=4)
		{
			*(y+indexY++) = *(srcYStartAddr + width*(h+2) + w);
			if(w+4 < width)
			{
				*(y+indexY++) = *(srcYStartAddr + width*(h+2) + w+1);
				*(y+indexY++) = *(srcYStartAddr + width*(h+2) + w+2);
			}
		}

	}


	for(h = 0; h < uvHeight; h+=4)
	{
		for(w = 0; w < uvWidth; w+=4)
		{
			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*h + w);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*h + w);

			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*h + w+1);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*h + w+1);

			if(w+4 < uvWidth)
			{
				*(uv+indexUV++) = *(srcVStartAddr + uvWidth*h + w+2);
				*(uv+indexUV++) = *(srcUStartAddr + uvWidth*h + w+2);
			}
		}

		for(w = 0; w < uvWidth; w+=4)
		{
			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*(h+1) + w);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*(h+1) + w);

			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*(h+1) + w+1);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*(h+1) + w+1);

			if(w+4 < uvWidth)
			{
				*(uv+indexUV++) = *(srcVStartAddr + uvWidth*(h+1) + w+2);
				*(uv+indexUV++) = *(srcUStartAddr + uvWidth*(h+1) + w+2);
			}
		}

		for(w = 0; w < uvWidth; w+=4)
		{
			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*(h+2) + w);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*(h+2) + w);

			*(uv+indexUV++) = *(srcVStartAddr + uvWidth*(h+2) + w+1);
			*(uv+indexUV++) = *(srcUStartAddr + uvWidth*(h+2) + w+1);

			if(w+4 < uvWidth)
			{
				*(uv+indexUV++) = *(srcVStartAddr + uvWidth*(h+2) + w+2);
				*(uv+indexUV++) = *(srcUStartAddr + uvWidth*(h+2) + w+2);
			}
		}

	}

}

static void DoH264ScaleFunc(unsigned char *srcY, unsigned char *srcU, unsigned char *srcV, unsigned char *dst, const unsigned int width, const unsigned int height, const enum H264_SCALE scale)
{
	switch(scale)
	{
		case H264_ONE:
			doH264ScaleInt(srcY, srcU, srcV, dst, width, height, 1);
			break;
		case H264_ONE_AND_HALF:
			doH264ScaleOneHalf(srcY, srcU, srcV, dst, width, height);
			break;
		case H264_ONE_AND_ONE_THIRD:
			doH264ScaleOneThrid(srcY, srcU, srcV, dst, width, height);
			break;
		case H264_TWO:
			doH264ScaleInt(srcY, srcU, srcV, dst, width, height, 2);
			break;
		case H264_THREE:
			doH264ScaleInt(srcY, srcU, srcV, dst, width, height, 3);
			break;
	}

}
void doH264MemCopy(unsigned char *src, unsigned char *srcU, unsigned char *srcV, const int sensorId, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstAddr = NULL;
	enum H264_SCALE mscale = H264_ONE;

	if(sensorId == 0)
	{
		dstAddr = shareMainH264;
		mscale = mMainScale;
	}
	else if(sensorId == 2)
	{
		dstAddr = shareSubH264;
		mscale = mSubScale;
	}
	if(dstAddr == NULL)
		return;

	switch(mscale)
	{
		case H264_ONE:
			writeFlag = dstAddr + width*height*3/2;
			break;
		case H264_ONE_AND_HALF:
		case H264_ONE_AND_ONE_THIRD:
		case H264_TWO:
		case H264_THREE:
			writeFlag = dstAddr + 345600;//640x360x1.5
			break;
		default:
			writeFlag = dstAddr + width*height*3/2;
			break;
	}

	if(*writeFlag == 0)
	{
		DoH264ScaleFunc(src, srcU, srcV, dstAddr, width, height, mscale);
		*writeFlag = 1;
	}
}

void doRecordMemCopy(unsigned char *srcY, unsigned char *srcU, unsigned char *srcV, const int sensorId, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstYStartAddr = NULL;
	unsigned char *dstUStartAddr = NULL;
	unsigned char *dstVStartAddr = NULL;

	if(sensorId == 0)
		dstYStartAddr = shareMainMem;
	else if(sensorId == 2)
		dstYStartAddr = shareSubMem;

	if(dstYStartAddr == NULL)
		return;

	writeFlag = dstYStartAddr+ width*height/2*3;

	if(*writeFlag == 0)
	{	

		const unsigned int uvHeight = height/2;
		const unsigned int uvWidth = width/2;
		const unsigned char *srcYStartAddr = srcY;
		const unsigned char *srcUStartAddr = srcU;
		const unsigned char *srcVStartAddr = srcV;
		unsigned int h = 0;

		dstUStartAddr = dstYStartAddr + width*height;
		dstVStartAddr = dstUStartAddr + width*height/4;

		for(h = 0; h < height;h++)//Y
		{
			memcpy(dstYStartAddr+h*width, srcYStartAddr+h*width, width);
		}

		for(h = 0; h < uvHeight;h++)//UV
		{
			memcpy(dstUStartAddr+h*uvWidth, srcUStartAddr+h*uvWidth, uvWidth);
			memcpy(dstVStartAddr+h*uvWidth, srcVStartAddr+h*uvWidth, uvWidth);
		}

		*writeFlag = 1;
	}

}

static void lineMirror(unsigned int* Data,const unsigned int width)
{
	unsigned int y0,y1,u,v;
	unsigned int i;
	unsigned int tmpData = 0;
	const unsigned int halfWidth = width/2;

	for(i = 0; i < halfWidth; i++)
	{
		tmpData = *(Data+i);
		y0 = (*(Data+width-1-i)&0xFF000000) >>16;
		u = *(Data+width-1-i)&0x00FF0000;
		y1 = (*(Data+width-1-i)&0x0000FF00) << 16;
		v = *(Data+width-1-i)&0x000000FF;
		*(Data+i) = u|y0|v|y1;

		y0 = (tmpData&0xFF000000) >>16;
		u = tmpData&0x00FF0000;
		y1 = (tmpData&0x0000FF00) << 16;
		v = tmpData&0x000000FF;
		*(Data+width-1-i) = u|y0|v|y1;
	}
}

static void doMirrorUYVY(unsigned char *src, const unsigned int width, const unsigned int height)
{
	int h = 0;
	int dataWidth = width / 2;

	for(h = 0; h < height; h++)
		lineMirror((unsigned int *)src+h*dataWidth, dataWidth);
}

void doMirrorYUV420(unsigned char *srcY, unsigned char *srcU, unsigned char *srcV, const int sensorId, const unsigned int width, const unsigned int height)
{
	unsigned int i = 0;
	unsigned int j = 0;
	const unsigned int uvHeight = height/ 2;
	const unsigned int uvWidth = width / 2;
	const unsigned int halfYwidth = width / 2;
	const unsigned int halfUvwidth = uvWidth / 2;
	unsigned char tmpData = 0;

	if(((mirrorFlag & 0x10) && sensorId == 0) || (mirrorFlag & 0x01 && sensorId == 2))
	{
		for(i = 0; i < height; i++)
		{
			for(j = 0; j < halfYwidth; j++)
			{
				tmpData = *(srcY+i*width+j);
				*(srcY+i*width+j) = *(srcY+i*width+width-1-j);
				*(srcY+i*width+width-1-j) = tmpData;
			}
	   	}

		for(i = 0; i < uvHeight; i++)
		{
			for(j = 0; j < halfUvwidth; j++)
			{
				tmpData = *(srcU+i*uvWidth+j);
				*(srcU+i*uvWidth+j) = *(srcU+i*uvWidth+uvWidth-1-j);
				*(srcU+i*uvWidth+uvWidth-1-j) = tmpData;
			}
	   	}

		for(i = 0; i < uvHeight; i++)
		{
			for(j = 0; j < halfUvwidth; j++)
			{
				tmpData = *(srcV+i*uvWidth+j);
				*(srcV+i*uvWidth+j) = *(srcV+i*uvWidth+uvWidth-1-j);
				*(srcV+i*uvWidth+uvWidth-1-j) = tmpData;
			}
	   	}
	}
}

void wwc2WaterMask(unsigned char *src, const unsigned int w, const unsigned int h, const int sensorId)//UYVY
{
	if(isEnableWaterMark)
		timeWaterMark(src, 40, h-80, w, h);

	if(sensorId == 0)
		waterMarkFunc(src+w*(h-80)+(w-80), w, waterMaskFB_40x40[1], 40, 40);
	else if(sensorId == 2)
		waterMarkFunc(src+w*(h-80)+(w-80), w, waterMaskFB_40x40[0], 40, 40);
}

static unsigned char *getShareMem(char* shareDev, const unsigned int size)
{
	int fd = 0;
	unsigned char *shm;
	fd = open(shareDev, O_CREAT | O_RDWR, 0666);
	if(fd <= 0)
	{
		ALOGE("bbl open shareDev fail");
		return NULL;
	}

	if(ftruncate(fd, size) < 0)
	{
		ALOGE("bbl ftruncate fail");
		close(fd);
		return NULL;
	}

	shm = (unsigned char *)mmap(NULL, size, (PROT_READ|PROT_WRITE), MAP_SHARED, fd, 0);
	close(fd);
	if(shm == (void *) -1)
	{
		ALOGE("bbl mmap fail  %d, %s", errno, strerror(errno));
		return NULL;
	}

	memset(shm,0,size);
	return shm;
}

void wwc2RecordInit(void)
{
	int recordEnable = 0;
	enum CAM_360_TYPE cameraType = MAIN_HD_SUB_HD;

	if(isInit == false)
	{
		cameraType = (enum CAM_360_TYPE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/360_camtype");
		recordEnable = property_get_int32("wwc2.video.record.enable",0);
		isEnableWaterMark = property_get_int32("wwc2.video.record.watermask",0);
		mirrorFlag = property_get_int32("wwc2.video.record.mirror",0);
		switch(mirrorFlag)
		{
			case DUAL_NORMAL:
				mirrorFlag = 0x00;
				break;
			case SUB_MIRROR:
				mirrorFlag = 0x01;
				break;
			case MAIN_MIRROR:
				mirrorFlag = 0x10;
				break;
			case DUAL_MIRROR:
				mirrorFlag = 0x11;
				break;
		}

		if(recordEnable)
		{
			getVideoShareMemSize(&shareMainMemSize, 0, cameraType);
			getVideoShareMemSize(&shareSubMemSize, 2, cameraType);
			getH264ShareMemSize(&shareMainH264Size, 0, &mMainScale, cameraType);
			getH264ShareMemSize(&shareSubH264Size, 2, &mSubScale, cameraType);

			if(shareMainMem == NULL && shareMainMemSize)
				shareMainMem = getShareMem("/sdcard/.mainImg",shareMainMemSize);
			if(shareSubMem == NULL && shareSubMemSize)
				shareSubMem = getShareMem("/sdcard/.subImg",shareSubMemSize);
			if(shareMainH264 == NULL && shareMainH264Size)
				shareMainH264 = getShareMem("/sdcard/.mainH264",shareMainH264Size);
			if(shareSubH264 == NULL && shareSubH264Size)
				shareSubH264 = getShareMem("/sdcard/.subH264",shareSubH264Size);
		}
		isInit = true;
	}
}

void wwc2RecordUninit(void)
{
	if(shareMainMem)
	{
		munmap(shareMainMem, shareMainMemSize);
		shareMainMem = NULL;
	}

	if(shareSubMem)
	{
		munmap(shareSubMem, shareSubMemSize);
		shareSubMem = NULL;
	}

	if(shareMainH264)
	{
		munmap(shareMainH264, shareMainH264Size);
		shareMainH264 = NULL;
	}

	if(shareSubH264)
	{
		munmap(shareSubH264, shareSubH264Size);
		shareSubH264 = NULL;
	}

	if(!access("/sdcard/.mainImg", F_OK))
		remove("/sdcard/.mainImg");

	if(!access("/sdcard/.subImg", F_OK))
		remove("/sdcard/.subImg");

	if(!access("/sdcard/.mainH264", F_OK))
		remove("/sdcard/.mainH264");

	if(!access("/sdcard/.subH264", F_OK))
		remove("/sdcard/.subH264");

	isInit = false;
}

/*
*****************************************************************************
*****************************WWC2 CAPTURE************************************
*****************************************************************************
*/

static unsigned char *shareMemMainCapture = NULL;
static unsigned char *shareMemSubCapture = NULL;
static unsigned int shareMemMainCaptureSize = 0;
static unsigned int shareMemSubCaptureSize = 0;
static bool isCaptureMainInit = false;
static bool isCaptureSubInit = false;

void wwc2CaptureInit(const int sensorId)
{
	enum CAM_360_TYPE cameraType = MAIN_HD_SUB_HD;
	int recordEnable = 0;
	int rate = 0;

	if(sensorId == 0)
	{
		if(isCaptureMainInit == false)
		{
			recordEnable = property_get_int32("wwc2.video.record.enable",0);
			rate = getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/cam_rate");
			if(recordEnable && rate != 0x20)
			{
				cameraType = (enum CAM_360_TYPE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/360_camtype");
				getCaptureShareMemSize(&shareMemMainCaptureSize, sensorId, cameraType);
				if(shareMemMainCapture == NULL && shareMemMainCaptureSize)
					shareMemMainCapture = getShareMem("/sdcard/.cMainImg",shareMemMainCaptureSize);
			}
			isCaptureMainInit = true;
		}
	}
	else if(sensorId == 2)
	{
		if(isCaptureSubInit == false)
		{
			recordEnable = property_get_int32("wwc2.video.record.enable",0);
			if(recordEnable)
			{
				cameraType = (enum CAM_360_TYPE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/360_camtype");
				getCaptureShareMemSize(&shareMemSubCaptureSize, sensorId, cameraType);
				if(shareMemSubCapture == NULL && shareMemSubCaptureSize)
					shareMemSubCapture = getShareMem("/sdcard/.cSubImg",shareMemSubCaptureSize);
			}
			isCaptureSubInit = true;
		}		
	}
}

void wwc2CaptureUninit(const int sensorId)
{
	if(sensorId == 0)
	{
		if(shareMemMainCapture)
		{
			munmap(shareMemMainCapture, shareMemMainCaptureSize);
			shareMemMainCapture = NULL;
		}

		if(!access("/sdcard/.cMainImg", F_OK))
			remove("/sdcard/.cMainImg");

		shareMemMainCaptureSize = 0;
		isCaptureMainInit = false;
	}
	else if(sensorId == 2)
	{
		if(shareMemSubCapture)
		{
			munmap(shareMemSubCapture, shareMemSubCaptureSize);
			shareMemSubCapture = NULL;
		}

		if(!access("/sdcard/.cSubImg", F_OK))
			remove("/sdcard/.cSubImg");

		shareMemSubCaptureSize = 0;
		isCaptureSubInit = false;
	}
}

static void uyvyToYuv420(unsigned char *src, unsigned char *dst, const unsigned int width, const unsigned int height)
{
	unsigned char *y = dst;
	unsigned char *u = y + width * height;
	unsigned char *v = u + width * height/4 ;
	unsigned int indexY = 0, indexU = 0, indexV = 0;
	unsigned int w, h;
	const unsigned int srcDataWidth = width*2;

	for(h = 0; h < height; h++)
	{
		for(w = 0; w < srcDataWidth; w+=4)
		{
			*(y+indexY++) = *(src + srcDataWidth*h + w + 1);
			*(y+indexY++) = *(src + srcDataWidth*h + w + 3);

			if(h%2 == 0)
			{
				*(u+indexU++) = *(src + srcDataWidth*h +w);
				*(v+indexV++) = *(src + srcDataWidth*h + w+ 2);
			}
		}
	}
}

static void yuv422ToNv21(unsigned char *src, unsigned char *dst, const unsigned int width, const unsigned int height)
{
	unsigned char *y = dst;
	unsigned char *uv = y+width*height;
	unsigned int indexY = 0, indexUv = 0;
	unsigned int w,h;
	const unsigned int srcDataWidth = width*2;

	for(h = 0; h < height; h++)
	{
		for(w = 0; w < srcDataWidth; w+=2)
			*(y+indexY++) = *(src + srcDataWidth*h + w + 1);
	}

	for(h = 0; h < height; h+=2)
	{
		for(w = 0; w < srcDataWidth; w+=4)
		{
			*(uv+indexUv++) = *(src+srcDataWidth*h + w + 2);
			*(uv+indexUv++) = *(src+srcDataWidth*h + w);
		}
	}
}


static void doCaptureMemCopyFunc(unsigned char *src, unsigned char *dst, const unsigned int w, const unsigned int h)
{
	unsigned char *writeFlag = dst + w*h/2*3;
	
	if(*writeFlag == 0)
	{
		yuv422ToNv21(src, dst, w, h);
		*writeFlag = 1;
	}
}

void doCaptureMemCopy(unsigned char *src, const unsigned int width, const unsigned int height, const int sensorId)
{
	unsigned char *dstAddr = NULL;

	if(sensorId == 0)
	{
		dstAddr = shareMemMainCapture;
	}
	else if(sensorId == 2)
	{
		dstAddr = shareMemSubCapture;
	}

	if(dstAddr == NULL)
		return;

	doCaptureMemCopyFunc(src, dstAddr, width, height);
}

#ifdef __cplusplus
}
#endif

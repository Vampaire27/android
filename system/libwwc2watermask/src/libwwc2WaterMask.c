#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include <sys/mman.h>
#include <time.h>
#include "libwwc2WaterMask.h"
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

enum VIDEO_MIRROR{
	DUAL_NORMAL = 0,
	SUB_MIRROR = 1,
	MAIN_MIRROR = 10,
	DUAL_MIRROR = 11,
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

static int isEnableWaterMark = 0;
static int mirrorFlag = 0;
static unsigned int shareMainMemSize = 0;
static unsigned int shareSubMemSize = 0;
static unsigned int shareMainH264Size = 0;
static unsigned int shareSubH264Size = 0;
static unsigned char mainH264Scale = 0;
static unsigned char subH264Scale = 0;

static unsigned char *shareMainMem = NULL;
static unsigned char *shareSubMem = NULL;
static unsigned char *shareMainH264 = NULL;
static unsigned char *shareSubH264 = NULL;
static bool isMainInit = false;
static bool isSubInit = false;
static enum VIDEO_RESIZE_SCALE mainScale = VR720P_TO_720P;
static enum VIDEO_RESIZE_SCALE subScale = VR720P_TO_720P;

static unsigned char *shareMemMainCapture = NULL;
static unsigned char *shareMemSubCapture = NULL;
static unsigned int shareMemMainCaptureSize = 0;
static unsigned int shareMemSubCaptureSize = 0;


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
	else if(sensorId == 1)//sub
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

static enum VIDEO_SIZE getUserSetVideoSize(const int sensorId, const enum INPUT_SIZE size)
{
	enum VIDEO_SIZE videoSize = VIDEO_1280x720;
	int videoUserSet = 2;

	if(sensorId == 0)
		videoUserSet = property_get_int32("wwc2.main.video.user.set",0);
	else
		videoUserSet = property_get_int32("wwc2.sub.video.user.set",0);

	if(videoUserSet > 0 && videoUserSet < 10)
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

static enum VIDEO_RESIZE_SCALE getVideoScale(const int sensorId, const enum CAM_360_TYPE cameraType)
{
	enum VIDEO_RESIZE_SCALE videoScale = VR720P_TO_720P;
	enum INPUT_SIZE size = getVideoSize(sensorId, cameraType);
	enum VIDEO_SIZE videoSize = getUserSetVideoSize(sensorId,size);

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

static void getVideoShareMemSize(unsigned int* memSize, const int sensorId, const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	enum VIDEO_SIZE videoSize =VIDEO_1280x720;

	size = getVideoSize(sensorId, cameraType);
	videoSize = getUserSetVideoSize(sensorId, size);
	*memSize = getVideoMemSize(videoSize);
}

static void getCaptureShareMemSize(unsigned int* memSize, const int sensorId, const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;

	size = getVideoSize(sensorId, cameraType);
	*memSize = getCaptureMemSize(size);
}


static void getShareH264Size(unsigned int* mainH264Size, unsigned int* subH264Size, unsigned char* mainH264Scale, unsigned char* subH264Scale, const enum CAM_360_TYPE cameraType)
{
	if(mainH264Size && mainH264Scale)
	{
		switch(cameraType)
		{
			case MAIN_NTSC_SUB_NTSC:
			case MAIN_NTSC_SUB_PAL:
			case MAIN_NTSC_SUB_HD:
			case MAIN_NTSC_SUB_FHD:
			case CVBS_NTSC:
				*mainH264Size = 127*0x1000;
				*mainH264Scale = 1;
				break;
			case MAIN_PAL_SUB_NTSC:
			case MAIN_PAL_SUB_PAL:
			case MAIN_PAL_SUB_HD:
			case MAIN_PAL_SUB_FHD:
			case CVBS_PAL:
				*mainH264Size = 152*0x1000;
				*mainH264Scale = 1;
				break;
			case MAIN_HD_SUB_NTSC:
			case MAIN_HD_SUB_PAL:
			case MAIN_HD_SUB_HD:
			case MAIN_HD_SUB_FHD:
			case AHD_25FPS:
			case AHD_30FPS:
				*mainH264Size = 85*0x1000;
				*mainH264Scale = 2;
				break;
			case MAIN_FHD_SUB_NTSC:
			case MAIN_FHD_SUB_PAL:
			case MAIN_FHD_SUB_HD:
			case MAIN_FHD_SUB_FHD:
			case FHD_25FPS:
			case FHD_30FPS:
				*mainH264Size = 85*0x1000;
				*mainH264Scale = 3;
				break;
			default:
				*mainH264Size = 85*0x1000;
				*mainH264Scale = 2;
				break;
		}
	}

	if(subH264Size && subH264Scale)
	{
		switch(cameraType)
		{
			case MAIN_NTSC_SUB_NTSC:
			case MAIN_PAL_SUB_NTSC:
			case MAIN_HD_SUB_NTSC:
			case MAIN_FHD_SUB_NTSC:
				*subH264Size = 127*0x1000;
				*subH264Scale = 1;
					break;
			case MAIN_NTSC_SUB_PAL:
			case MAIN_PAL_SUB_PAL:
			case MAIN_HD_SUB_PAL:
			case MAIN_FHD_SUB_PAL:
				*subH264Size = 152*0x1000;
				*subH264Scale = 1;
				break;
			case MAIN_NTSC_SUB_HD:
			case MAIN_PAL_SUB_HD:
			case MAIN_HD_SUB_HD:
			case MAIN_FHD_SUB_HD:
				*subH264Size = 85*0x1000;
				*subH264Scale = 2;
				break;
			case MAIN_NTSC_SUB_FHD:
			case MAIN_PAL_SUB_FHD:
			case MAIN_HD_SUB_FHD:
			case MAIN_FHD_SUB_FHD:
				*subH264Size = 85*0x1000;
				*subH264Scale = 3;
				break;
			default:
				*subH264Size = 85*0x1000;
				*subH264Scale = 2;
				break;
		}
	}
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
				*(dstStart+(dstWidth*i+j)*2+1) = 0xff;//0x0;
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

	//waterMarkStartAddr = imgStartAddr + startPointY*width + startPointX;//YUV420
	waterMarkStartAddr = imgStartAddr + (startPointY*width + startPointX)*2;//UYVY

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
			waterMarkFunc(waterMarkStartAddr + 20*waterMarkIndex*2, width, waterMarkData, 20, 32);
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

static void yuv422ToYuv420(unsigned char *src, unsigned char *dst, const unsigned int width, const unsigned int height)
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

			if((h&1) == 0)
			{
				*(u+indexU++) = *(src + srcDataWidth*h +w);
				*(v+indexV++) = *(src + srcDataWidth*h + w+ 2);
			}
		}
	}
}

static void yuv422ToYuv420OneHalf(unsigned char *src, unsigned char *dst, const unsigned int width, const unsigned int height)
{
	const unsigned int dstWidth = (width%3)?(width*2/3+1):(width*2/3);
	const unsigned int dstHeight = (height%3)?(height*2/3+1):(height*2/3);
	unsigned char *y = dst;
	unsigned char *u = y+dstWidth*dstHeight;
	unsigned char *v = u+dstWidth*dstHeight/4;
	unsigned int indexY = 0, indexU = 0, indexV = 0;
	unsigned int w,h;
	const unsigned int srcDataWidth = width*2;

	for(h = 0; h < height; h+=3)
	{
		for(w = 0; w < srcDataWidth; w+=6)
		{
			*(y+indexY++) = *(src + srcDataWidth*h + w + 1);
			*(y+indexY++) = *(src + srcDataWidth*h + w + 3);
		}

		for(w = 0; w < srcDataWidth; w+=6)
		{
			*(y+indexY++) = *(src + srcDataWidth*(h+1) + w + 1);
			*(y+indexY++) = *(src + srcDataWidth*(h+1) + w + 3);
		}
	}

	for(h = 0; h < height; h+=6)
	{
		for(w = 0; w < srcDataWidth; w+=12)
		{
			*(u+indexU++) = *(src + srcDataWidth*h +w);
			*(v+indexV++) = *(src + srcDataWidth*h + w+ 2);
			if(w+4 < srcDataWidth)
			{
				*(u+indexU++) = *(src + srcDataWidth*h +w+4);
				*(v+indexV++) = *(src + srcDataWidth*h + w+ 6);
			}
		}

		for(w = 0; w < srcDataWidth; w+=12)
		{
			*(u+indexU++) = *(src + srcDataWidth*(h+3) +w);
			*(v+indexV++) = *(src + srcDataWidth*(h+3) + w+ 2);
			if(w+4 < srcDataWidth)
			{
				*(u+indexU++) = *(src + srcDataWidth*(h+3) +w+4);
				*(v+indexV++) = *(src + srcDataWidth*(h+3) + w+ 6);
			}
		}

	}
}
static void yuv422ToYuv420Divide(unsigned char *src, unsigned char *dst, const unsigned int width, const unsigned int height, const unsigned char scale)
{
	unsigned char *y = dst;
	unsigned char *u = y + (width/scale) * (height/scale);
	unsigned char *v = u + (width/scale) * (height/scale)/4 ;

	unsigned int indexY = 0, indexU = 0, indexV = 0;
	unsigned int w, h;
	const unsigned int srcDataWidth = width*2;
	const unsigned int yWidthStep = 2 * scale;
	const unsigned int uvWidthStep = 4 * scale;
	const unsigned int uvHeightStep = 2 * scale;

	for(h = 0; h < height; h+=scale)//Y
	{
		for(w = 0; w < srcDataWidth; w+=yWidthStep)
			*(y+indexY++) = *(src + srcDataWidth*h + w + 1);
	}

	for(h = 0; h < height; h+=uvHeightStep)//UV
	{
		for(w = 0; w < srcDataWidth; w+=uvWidthStep)
		{
			*(u+indexU++) = *(src + srcDataWidth*h +w);
			*(v+indexV++) = *(src + srcDataWidth*h + w+ 2);
		}
	}
}

static void resizeYUV422ToYUV420(enum VIDEO_RESIZE_SCALE scale, unsigned char *src, unsigned char *dst, const unsigned int width, const unsigned int height)
{
	switch(scale)
	{
		case VR1080P_TO_1080P:
		case VR720P_TO_720P:
			yuv422ToYuv420(src, dst, width, height);
			break;
		case VR1080P_TO_720P:
		case VR720P_TO_480P:
			yuv422ToYuv420OneHalf(src, dst, width, height);
			break;
		case VR1080P_TO_540P:
		case VR720P_TO_360P:
			yuv422ToYuv420Divide(src, dst, width, height, 2);
			break;
		case VR1080P_TO_360P:
			yuv422ToYuv420Divide(src, dst, width, height, 3);
			break;
		default:
			yuv422ToYuv420(src, dst, width, height);
			break;
	}
}


static void doRecordMemCopy(unsigned char *src, const int sensorId, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstAddr = NULL;
	enum VIDEO_RESIZE_SCALE scale = VR720P_TO_720P;

	if(sensorId == 0)
	{
		dstAddr = shareMainMem;
		scale = mainScale;
	}
	else if(sensorId == 1)
	{
		dstAddr = shareSubMem;
		scale = subScale;
	}

	if(dstAddr == NULL)
		return;

	switch(scale)
	{
		case VR1080P_TO_1080P:
		case VR720P_TO_720P:
			writeFlag = dstAddr + width*height/2*3;
			break;
		case VR1080P_TO_720P:
			writeFlag = dstAddr + 1382400;//1280x720x1.5
			break;
		case VR1080P_TO_540P:
			writeFlag = dstAddr + 777600;//960x540x1.5
			break;
		case VR1080P_TO_360P:
		case VR720P_TO_360P:
			writeFlag = dstAddr + 345600;//640x360x1.5
			break;
		case VR720P_TO_480P:
			writeFlag = dstAddr + 614880;//854x480x1.5
			break;
		default:
			writeFlag = dstAddr + width*height/2*3;
			break;
	}

	if(*writeFlag == 0)
	{
		resizeYUV422ToYUV420(scale, src, dstAddr, width, height);
		*writeFlag = 1;
	}
}

static void doH264MemCopy(unsigned char *src, const int sensorId, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstAddr = NULL;
	unsigned char scale = 0;

	if(sensorId == 0)
	{
		dstAddr = shareMainH264;
		scale = mainH264Scale;
	}
	else if(sensorId == 1)
	{
		dstAddr = shareSubH264;
		scale = subH264Scale;
	}

	if(dstAddr == NULL)
		return;

	writeFlag = dstAddr + (width/scale)*(height/scale)/2*3;

	if(*writeFlag == 0)
	{
		//yuv422ToYuv420Divide(src, dstAddr, width, height, scale);
		{
			unsigned char *y = dstAddr;
			unsigned char *uv = y + (width/scale) * (height/scale);

			unsigned int indexY = 0, uvIndex = 0;
			unsigned int w, h;
			const unsigned int srcDataWidth = width*2;
			const unsigned int yWidthStep = 2 * scale;
			const unsigned int uvWidthStep = 4 * scale;
			const unsigned int uvHeightStep = 2 * scale;

			for(h = 0; h < height; h+=scale)//Y
			{
				for(w = 0; w < srcDataWidth; w+=yWidthStep)
					*(y+indexY++) = *(src + srcDataWidth*h + w + 1);
			}

			for(h = 0; h < height; h+=uvHeightStep)//UV
			{
				for(w = 0; w < srcDataWidth; w+=uvWidthStep)
				{
					*(uv+uvIndex++) = *(src+srcDataWidth*h +w+2);
					*(uv+uvIndex++) = *(src+srcDataWidth*h +w);
				}
			}
		}
	
		*writeFlag = 1;
	}
}

static void doCaptureMemcopy(unsigned char *src, const int sensorId, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstAddr = NULL;

	if(sensorId == 0)
	{
		dstAddr = shareMemMainCapture;
	}
	else if(sensorId == 1)
	{
		dstAddr = shareMemSubCapture;
	}

	if(dstAddr == NULL)
		return;

	writeFlag = dstAddr + width*height/2*3;
	if(*writeFlag == 0)
	{
		yuv422ToNv21(src, dstAddr, width, height);
		*writeFlag = 1;
	}

}

void doH264MemCopyYuv420(unsigned char *src, const unsigned int stride0, const unsigned int stride1, const unsigned int stride2, const int sensorId, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstAddr = NULL;
	unsigned char mscale = 0;

	if(sensorId == 0)
	{
		dstAddr = shareMainH264;
		mscale = mainH264Scale;
	}
	else if(sensorId == 1)
	{
		dstAddr = shareSubH264;
		mscale = subH264Scale;
	}

	if(dstAddr == NULL)
		return;

	writeFlag = dstAddr + (width/mscale)*(height/mscale)/2*3;

	if(*writeFlag == 0)
	{
		{
			const unsigned char scale = mscale;
			const unsigned int uvHeight = height/2;
			const unsigned int uvWidth = width/2;
			const unsigned char *srcYStartAddr = src;
			const unsigned char *srcUStartAddr = srcYStartAddr + stride0*height+stride1*uvHeight;
			const unsigned char *srcVStartAddr = srcYStartAddr + stride0*height;

			unsigned char *y = dstAddr;
			unsigned char *u = y + (width/scale) * (height/scale);
			unsigned char *v = u + (width/scale) * (height/scale)/4 ;

			unsigned int indexY = 0, indexU = 0, indexV = 0;
			unsigned int w, h;

			for(h = 0; h < height; h+=scale)//Y
			{
				for(w = 0; w < width; w+=scale)
					*(y+indexY++) = *(srcYStartAddr + stride0*h + w);
			}

			for(h = 0; h < uvHeight; h+=scale)
			{
				for(w = 0; w < uvWidth; w+=scale)
				{
					*(u+indexU++) = *(srcUStartAddr + stride2*h +w);
					*(v+indexV++) = *(srcVStartAddr + stride1*h + w);
				}
			}
		}
		*writeFlag = 1;
	}
}

void doRecordMemCopyYuv420(unsigned char *src, const unsigned int stride0, const unsigned int stride1, const unsigned int stride2, const int sensorId, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstYStartAddr = NULL;
	unsigned char *dstUStartAddr = NULL;
	unsigned char *dstVStartAddr = NULL;

	if(sensorId == 0)
		dstYStartAddr = shareMainMem;
	else if(sensorId == 1)
		dstYStartAddr = shareSubMem;

	if(dstYStartAddr == NULL)
		return;

	writeFlag = dstYStartAddr+ width*height/2*3;

	if(*writeFlag == 0)
	{
		const unsigned int uvHeight = height/2;
		const unsigned int uvWidth = width/2;
		const unsigned char *srcYStartAddr = src;
		const unsigned char *srcUStartAddr = srcYStartAddr + stride0*height+stride1*uvHeight;
		const unsigned char *srcVStartAddr = srcYStartAddr + stride0*height;
		unsigned int h = 0;

		dstUStartAddr = dstYStartAddr + width*height;
		dstVStartAddr = dstUStartAddr + width*height/4;

		for(h = 0; h < height;h++)//Y
		{
			memcpy(dstYStartAddr+h*width, srcYStartAddr+h*stride0, width);
		}

		for(h = 0; h < uvHeight;h++)//UV
		{
			memcpy(dstUStartAddr+h*uvWidth, srcUStartAddr+h*stride1, uvWidth);
			memcpy(dstVStartAddr+h*uvWidth, srcVStartAddr+h*stride2, uvWidth);
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

static void doMirror(unsigned char *src, const unsigned int width, const unsigned int height)
{
	int h = 0;
	int dataWidth = width / 2;

	for(h = 0; h < height; h++)
		lineMirror((unsigned int *)src+h*dataWidth, dataWidth);
}

void wwc2WaterMask(unsigned char *src, const unsigned int w, const unsigned int h, const int sensorId)//UYVY
{
	if(h > 80 && w > 500)
	{
		if(((mirrorFlag & 0x10) && sensorId == 0) || (mirrorFlag & 0x01 && sensorId == 1))
		{
			doMirror(src, w, h);
		}

		if(isEnableWaterMark)
			timeWaterMark(src, 40, h-80, w, h);

		if(sensorId == 0 && shareMainMem)
			waterMarkFunc(src+(w*(h-80)+(w-80))*2, w, waterMaskFB_40x40[1], 40, 40);
		else if(sensorId == 1 && shareSubMem)
			waterMarkFunc(src+(w*(h-80)+(w-80))*2, w, waterMaskFB_40x40[0], 40, 40);

		doRecordMemCopy(src, sensorId, w, h);
		doH264MemCopy(src, sensorId, w, h);

		doCaptureMemcopy(src, sensorId, w, h);
	}
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

void wwc2RecordInit(int sensorId)
{
	int recordEnable = 0;
	int isBackCameraMirror = 0;
	enum CAM_360_TYPE cameraType = MAIN_HD_SUB_HD;

	if(sensorId == 0)
	{
		if(isMainInit == false)
		{
			recordEnable = property_get_int32("wwc2.video.record.enable",0);
			if(recordEnable)
			{
				cameraType = (enum CAM_360_TYPE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/360_camtype");
				isEnableWaterMark = property_get_int32("wwc2.video.record.watermask",0);
				getVideoShareMemSize(&shareMainMemSize, sensorId, cameraType);
				if(shareMainMem == NULL && shareMainMemSize)
					shareMainMem = getShareMem("/sdcard/.mainImg",shareMainMemSize);

				getShareH264Size(&shareMainH264Size, NULL, &mainH264Scale, NULL, cameraType);
				if(shareMainH264 == NULL && shareMainH264Size)
					shareMainH264 = getShareMem("/sdcard/.mainH264",shareMainH264Size);

				mainScale = getVideoScale(sensorId, cameraType);

				getCaptureShareMemSize(&shareMemMainCaptureSize, sensorId, cameraType);
				if(shareMemMainCapture == NULL && shareMemMainCaptureSize)
					shareMemMainCapture = getShareMem("/sdcard/.cMainImg",shareMemMainCaptureSize);
			}
			isMainInit = true;
		}
	}
	else if(sensorId == 1)
	{
		if(isSubInit == false)
		{
			recordEnable = property_get_int32("wwc2.video.record.enable",0);
			if(recordEnable)
			{
				cameraType = (enum CAM_360_TYPE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/360_camtype");
				isEnableWaterMark = property_get_int32("wwc2.video.record.watermask",0);
				getVideoShareMemSize(&shareSubMemSize, sensorId, cameraType);
				if(shareSubMem == NULL && shareSubMemSize)
					shareSubMem = getShareMem("/sdcard/.subImg",shareSubMemSize);

				getShareH264Size(NULL, &shareSubH264Size, NULL, &subH264Scale, cameraType);
				if(shareSubH264 == NULL && shareSubH264Size)
					shareSubH264 = getShareMem("/sdcard/.subH264",shareSubH264Size);

				subScale = getVideoScale(sensorId, cameraType);

				getCaptureShareMemSize(&shareMemSubCaptureSize, sensorId, cameraType);
				if(shareMemSubCapture == NULL && shareMemSubCaptureSize)
					shareMemSubCapture = getShareMem("/sdcard/.cSubImg",shareMemSubCaptureSize);
			}
			isSubInit = true;
		}
	}

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
	isBackCameraMirror = getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/cam_mirror");
	if(isBackCameraMirror)
		mirrorFlag |= 0x10;
}

void wwc2RecordUninit(int sensorId)
{
	if(sensorId == 0)
	{
		if(shareMainMem)
		{
			munmap(shareMainMem, shareMainMemSize);
			shareMainMem = NULL;
		}

		if(shareMainH264)
		{
			munmap(shareMainH264, shareMainH264Size);
			shareMainH264 = NULL;
		}

		if(shareMemMainCapture)
		{
			munmap(shareMemMainCapture, shareMemMainCaptureSize);
			shareMemMainCapture = NULL;
		}

		if(!access("/sdcard/.mainImg", F_OK))
			remove("/sdcard/.mainImg");

		if(!access("/sdcard/.mainH264", F_OK))
			remove("/sdcard/.mainH264");

		if(!access("/sdcard/.cMainImg", F_OK))
			remove("/sdcard/.cMainImg");

		shareMainMemSize = 0;
		shareMainH264Size = 0;
		mainH264Scale = 0;
		shareMemMainCaptureSize = 0;
		isMainInit = false;
	}
	else if(sensorId == 1)
	{
		if(shareSubMem)
		{
			munmap(shareSubMem, shareSubMemSize);
			shareSubMem = NULL;
		}

		if(shareSubH264)
		{
			munmap(shareSubH264, shareSubH264Size);
			shareSubH264 = NULL;
		}

		if(shareMemSubCapture)
		{
			munmap(shareMemSubCapture, shareMemSubCaptureSize);
			shareMemSubCapture = NULL;
		}

		if(!access("/sdcard/.subImg", F_OK))
			remove("/sdcard/.subImg");

		if(!access("/sdcard/.subH264", F_OK))
			remove("/sdcard/.subH264");

		if(!access("/sdcard/.cSubImg", F_OK))
			remove("/sdcard/.cSubImg");

		shareSubMemSize = 0;
		shareSubH264Size = 0;
		subH264Scale = 0;
		shareMemSubCaptureSize = 0;
		isSubInit = false;
	}
}

/*
****************************************************************
********************CVBS COMBINE*********************************
****************************************************************
*/
static bool cvbsCombineIsInit = false;
static bool isOddFrame = false;
static unsigned char *preFrameBuffer = NULL;
void wwc2CvbsCombineInit(int sensorId)
{
	enum INPUT_SIZE videoSize = SIZE_1280X720;
	int frameSize = 0;
	int i = 0;

	if(sensorId != 0)
		return;

	if(cvbsCombineIsInit == false)
	{
		videoSize = (enum INPUT_SIZE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/cam_mode");
		switch(videoSize)
		{
			case SIZE_720X240:
				frameSize = 720*240*2;
				break;
			case SIZE_720X288:
				frameSize = 720*288*2;
				break;
			default:
				break;
		}

		if(frameSize)
		{
			preFrameBuffer = (unsigned char*)malloc(frameSize);
			if(preFrameBuffer)
			{
				while(i < frameSize/2)
				{
					*((unsigned short*)preFrameBuffer+i) = 0x1080;
					i++;
				}
			}
		}
		cvbsCombineIsInit = true;
	}
}

void wwc2CvbsCombineUninit(void)
{
	if(preFrameBuffer)
	{
		free(preFrameBuffer);
		preFrameBuffer = NULL;
	}
	cvbsCombineIsInit = false;
}

static int slopeChangeTimes(unsigned char *src, const unsigned height)
{
	int i = 0;
	int times = 0;
	unsigned char curData = 0;
	unsigned char preData = 0;
	unsigned char nextData = 0;

	for(i = 1; i < height - 1; i++)
	{
		curData = *(src+i);
		preData = *(src+i-1);
		nextData = *(src+i+1);

		if((curData > nextData) && (curData > preData))
			times++;
		else if((curData < nextData) && (curData < preData))
			times++;
	}

	return times;
}

static void isOddOrEven(unsigned char *src, const unsigned int width, const unsigned int height)
{
	unsigned char columnY1[576] = {0};
	unsigned char columnY2[576] = {0};
	int y1Times = 0;
	int y2Times = 0;
	int decide1 = 0;
	int decide2 = 0;
	int w = 0;
	int h = 0;

	for(w = width - 9; w < width + 10; w+=2)
	{
		for(h = 0; h < height; h++)
		{
			columnY1[h] = *(src+h*width*2+w);
		}

		for(h = 0; h < height; h++)
		{
			if(h&1)
				columnY2[h] = columnY1[h-1];
			else
				columnY2[h] = columnY1[h+1];
		}

		y1Times = slopeChangeTimes(columnY1, height);
		y2Times = slopeChangeTimes(columnY2, height);

		if(y1Times > y2Times)
			decide1++;
		else if(y2Times > y1Times)
			decide2++;
	}
	if(decide1 == (decide2+10))
		isOddFrame = false;
	else if(decide2 == (decide1+10))
		isOddFrame = true;

}

void wwc2CvbsCombine(unsigned char *src, const unsigned int width, const unsigned int height, const int sensorId)
{
	int h = 0;
	int halfHeight = height/2;
	unsigned char lineBuffer[720*2] = {0};
	int doCombine = 0;

	if(sensorId != 0 || preFrameBuffer == NULL || height < 2)
		return;

	for(h = halfHeight; h > 0; h--)	//copy original img to it odd row
		memcpy(src+2*(h-1)*width*2, src+(h-1)*width*2, width*2);

	for(h = 0; h < halfHeight; h++)	//copy buff_raw img to src even row
		memcpy(src+(2*h+1)*width*2, preFrameBuffer+h*width*2, width*2);

	for(h = 0; h < halfHeight; h++)	// save original img to preFrameBuffer
		memcpy(preFrameBuffer+h*width*2, src+2*h*width*2, width*2);

	isOddOrEven(src, width, height);

	if(isOddFrame == false)
	{
		for(h = 0; h < height; h+=2) //swap odd  and even row
		{
			memcpy(lineBuffer, src+h*width*2, width*2);
			memcpy(src+h*width*2, src+(h+1)*width*2, width*2);
			memcpy(src+(h+1)*width*2, lineBuffer, width*2);
		}
		isOddFrame = true;
	}
	else
		isOddFrame = false;
}
#ifdef __cplusplus
}
#endif

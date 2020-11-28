#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include <sys/mman.h>
#include <time.h>
#include "wwc2_single_camera_record.h"
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
	VR_UNDEFINED
};

enum H264_SCALE
{
	H264_ONE = 1,
	H264_TWO,
	H264_THREE	,
	H264_UNDEFINED
};


static bool isInit = false;
static int isEnableWaterMark = 0;
static unsigned char* shareRecordMem = NULL;
static unsigned char* shareH264Mem = NULL;
static unsigned char* shareCaptureMem = NULL;

static unsigned int shareRecordMemSize = 0;
static unsigned int shareH264MemSize = 0;
static unsigned int shareCaptureMemSize = 0;

static enum VIDEO_RESIZE_SCALE recordScale = VR720P_TO_720P;
static enum H264_SCALE h264Scale = H264_ONE;

static const unsigned char waterMask_20x32[][20*32] = {ZERO_20X32, ONE_20X32, TWO_20X32, THREE_20X32, FOUR_20X32, FIVE_20X32,\
									SIX_20X32, SEVEN_20X32, EIGHT_20X32, NINE_20X32, COLON_20X32, SLASH_20X32};

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


static int getCameraFeatureFlag(const char* node)
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

static enum INPUT_SIZE getInputVideoSize(const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	
	switch(cameraType)
	{
		case AHD_25FPS:
		case AHD_30FPS:
			size = SIZE_1280X720;
			break;
		case FHD_25FPS:
		case FHD_30FPS:
			size = SIZE_1920X1080;
			break;
		case CVBS_NTSC:
			size = SIZE_720X480;
			break;
		case CVBS_PAL:
			size = SIZE_720X576;
			break;
		default:
			size = SIZE_1280X720;
			break;
	}
	
	return size;
}

static enum VIDEO_SIZE getUserSetVideoSize(const enum INPUT_SIZE size)
{
	enum VIDEO_SIZE videoSize = VIDEO_1280x720;
	int videoUserSet = 2;

	videoUserSet = property_get_int32("wwc2.main.video.user.set",0);

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

static unsigned int getRecordShareMemSize(const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	enum VIDEO_SIZE videoSize =VIDEO_1280x720;
	unsigned int memSize = 338*0x1000;

	size = getInputVideoSize(cameraType);
	videoSize = getUserSetVideoSize(size);

	switch(videoSize)
	{
		case VIDEO_720x240:
			memSize = 64*0x1000;
			break;
		case VIDEO_720x288:
			memSize = 76*0x1000;
			break;
		case VIDEO_640x360:
			memSize = 85*0x1000;
			break;
		case VIDEO_720x480:
			memSize = 127*0x1000;
			break;
		case VIDEO_720x576:
			memSize = 152*0x1000;
			break;
		case VIDEO_854x480:
			memSize = 151*0x1000;
			break;
		case VIDEO_960x540:
			memSize = 190*0x1000;
			break;
		case VIDEO_1280x720:
			memSize = 338*0x1000;
			break;
		case VIDEO_1920x1080:
			memSize = 760*0x1000;
			break;
		default:
			memSize = 338*0x1000;
			break;
	}

	return memSize;
}

static unsigned int getH264ShareMemSize(const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	unsigned int memSize = 85*0x1000;

	size = getInputVideoSize(cameraType);

	switch(size)
	{
		case SIZE_1280X720:
		case SIZE_1920X1080:
			memSize = 85*0x1000;
			break;
		case SIZE_720X288:
		case SIZE_720X576:
			memSize = 152*0x1000;
			break;
		case SIZE_720X240:
		case SIZE_720X480:
			memSize = 127*0x1000;
			break;
		default:
			break;
	}

	return memSize;
}

static unsigned int getCaptureShareMemSize(const enum CAM_360_TYPE cameraType)
{
	enum INPUT_SIZE size = SIZE_1280X720;
	unsigned int memSize = 338*0x1000;
	
	size = getInputVideoSize(cameraType);

	switch(size)
	{
		case SIZE_1920X1080:
			memSize = 760*0x1000;
			break;
		case SIZE_1280X720:
			memSize = 338*0x1000;
			break;
		case SIZE_720X576:
			memSize = 152*0x1000;
			break;
		case SIZE_720X480:
			memSize = 127*0x1000;
			break;
		default:
			memSize = 338*0x1000;
			break;
	}

	return memSize;
}

static enum VIDEO_RESIZE_SCALE getRecordScale(const enum CAM_360_TYPE cameraType)
{
	enum VIDEO_RESIZE_SCALE scale = VR720P_TO_720P;
	enum INPUT_SIZE size = SIZE_1280X720;
	enum VIDEO_SIZE videoSize =VIDEO_1280x720;

	size = getInputVideoSize(cameraType);
	videoSize = getUserSetVideoSize(size);

	if(size == SIZE_1920X1080)
	{
		switch(videoSize)
		{
			case VIDEO_1920x1080:
				scale = VR1080P_TO_1080P;
				break;
			case VIDEO_1280x720:
				scale = VR1080P_TO_720P;
				break;
			case VIDEO_960x540:
				scale = VR1080P_TO_540P;
				break;
			case VIDEO_640x360:
				scale = VR1080P_TO_360P;
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
				scale = VR720P_TO_720P;
				break;
			case VIDEO_854x480:
				scale = VR720P_TO_480P;
				break;
			case VIDEO_640x360:
				scale = VR720P_TO_360P;
				break;
			default:
				break;
		}
	}

	return scale;
}

static enum H264_SCALE getH264Scale(const enum CAM_360_TYPE cameraType)
{
	enum H264_SCALE scale = H264_ONE;
	enum INPUT_SIZE size = SIZE_1280X720;

	size = getInputVideoSize(cameraType);

	switch(size)
	{
		case SIZE_1280X720:
			scale = H264_TWO;
			break;
		case SIZE_1920X1080:
			scale = H264_THREE;
			break;
		default:
			break;
	}

	return scale;
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
			yuv422ToYuv420Divide(src, dst, width, height, 1);
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
			yuv422ToYuv420Divide(src, dst, width, height, 1);
			break;
	}
}

static void resizeYuv422ToNv21(const enum H264_SCALE scale, unsigned char *src, unsigned char *dst, const unsigned int width, const unsigned int height)
{
	unsigned char *y = dst;
	unsigned char *uv = y+(width/scale)*(height/scale);
	unsigned int indexY = 0, indexUv = 0;
	unsigned int w,h;
	const unsigned int srcDataWidth = width*2;
	const unsigned int yWidthStep = 2 * scale;
	const unsigned int uvWidthStep = 4 * scale;
	const unsigned int uvHeightStep = 2 * scale;


	for(h = 0; h < height; h+=scale)
	{
		for(w = 0; w < srcDataWidth; w+=yWidthStep)
			*(y+indexY++) = *(src + srcDataWidth*h + w + 1);
	}

	for(h = 0; h < height; h+=uvHeightStep)
	{
		for(w = 0; w < srcDataWidth; w+=uvWidthStep)
		{
			*(uv+indexUv++) = *(src+srcDataWidth*h + w + 2);
			*(uv+indexUv++) = *(src+srcDataWidth*h + w);
		}
	}
}

void doRecordMemCopy(unsigned char *src, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstAddr = shareRecordMem;
	const enum VIDEO_RESIZE_SCALE scale = recordScale;

	if(dstAddr == NULL)
		return;

	if(isEnableWaterMark)
		timeWaterMark(src, 40, height-80, width, height);

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

void doH264MemCopy(unsigned char *src, const unsigned int width, const unsigned int height)
{
	unsigned char *writeFlag = NULL;
	unsigned char *dstAddr = shareH264Mem;
	const enum H264_SCALE scale = h264Scale;

	if(dstAddr == NULL)
		return;

	switch(scale)
	{
		case H264_ONE:
			writeFlag = dstAddr + width*height/2*3;
			break;
		case H264_TWO:
		case H264_THREE:
			writeFlag = dstAddr + 345600;//640x360x1.5
			break;
		default:
			writeFlag = dstAddr + 345600;//640x360x1.5
			break;
	}

	if(*writeFlag == 0)
	{
		resizeYuv422ToNv21(scale, src, dstAddr, width, height);
		*writeFlag = 1;
	}

}

void doCaptureMemCopy(unsigned char *src, const unsigned int width, const unsigned int height)
{
	unsigned char *dstAddr = shareCaptureMem;
	unsigned char *writeFlag = dstAddr + width*height/2*3;
	
	if(dstAddr == NULL)
		return;

	if(*writeFlag == 0)
	{
		resizeYuv422ToNv21(1, src, dstAddr, width, height);
		*writeFlag = 1;
	}
}

void wwc2SingleCameraRecordInit(void)
{
	int recordEnable = 0;
	int isBackCameraMirror = 0;

	enum CAM_360_TYPE  cameraType = AHD_25FPS;

	if(isInit == false)
	{
		recordEnable = property_get_int32("wwc2.video.record.enable",0);
		if(recordEnable)
		{
			cameraType = (enum CAM_360_TYPE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/360_camtype");
			isEnableWaterMark = property_get_int32("wwc2.video.record.watermask",0);
			shareRecordMemSize = getRecordShareMemSize(cameraType);
			shareH264MemSize = getH264ShareMemSize(cameraType);
			shareCaptureMemSize = getCaptureShareMemSize(cameraType);
			recordScale = getRecordScale(cameraType);
			h264Scale = getH264Scale(cameraType);

			if(shareRecordMem == NULL && shareRecordMemSize > 0)
				shareRecordMem = getShareMem("/sdcard/.mainImg", shareRecordMemSize);
			
			if(shareH264Mem == NULL && shareH264MemSize > 0)
				shareH264Mem = getShareMem("/sdcard/.mainH264", shareH264MemSize);

			if(shareCaptureMem == NULL && shareCaptureMemSize > 0)
				shareCaptureMem = getShareMem("/sdcard/.cMainImg", shareCaptureMemSize);
		}
		isInit = true;
	}
}

void wwc2SingleCameraRecordUninit(void)
{
	if(shareRecordMem)
	{
		munmap(shareRecordMem, shareRecordMemSize);
		shareRecordMem = NULL;
		shareRecordMemSize = 0;
	}

	if(shareH264Mem)
	{
		munmap(shareH264Mem, shareH264MemSize);
		shareH264Mem = NULL;
		shareH264MemSize = 0;
	}

	if(shareCaptureMem)
	{
		munmap(shareCaptureMem, shareCaptureMemSize);
		shareCaptureMem = NULL;
		shareCaptureMemSize = 0;
	}

	if(!access("/sdcard/.mainImg", F_OK))
		remove("/sdcard/.mainImg");

	if(!access("/sdcard/.mainH264", F_OK))
		remove("/sdcard/.mainH264");

	if(!access("/sdcard/.cMainImg", F_OK))
		remove("/sdcard/.cMainImg");

	isInit = false;

}

#ifdef __cplusplus
}
#endif

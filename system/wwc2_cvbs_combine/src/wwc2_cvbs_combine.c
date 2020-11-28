#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h> 
#include <stdlib.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include "wwc2_cvbs_combine.h"

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

bool getMirrorValue(void)
{
	int value = getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/cam_mirror");

 	if(value == 1)
		return true;
	else
		return false;
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

void doMirror(unsigned char *src, const unsigned int width, const unsigned int height)
{
	int h = 0;
	int dataWidth = width / 2;

	for(h = 0; h < height; h++)
		lineMirror((unsigned int *)src+h*dataWidth, dataWidth);
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

static int slopeChangeTimes(unsigned char *src, const unsigned int height)
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

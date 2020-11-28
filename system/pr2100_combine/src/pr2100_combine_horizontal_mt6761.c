#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include "pr2100_combine.h"
#include "wwc2_pr2100_capture.h"
#include "wwc2_pr2100_record.h"
#include "avm_cam.h"

static enum CAMERA_FORMAT cameraFormat = FOUR_HD;
struct FOUR_CHANNEL_FRAME_FLIP frameFlip = {.ch0FrameFlip = 0, .ch1FrameFlip = 0, .ch2FrameFlip = 0, .ch3FrameFlip = 0};
static struct FOUR_CHANNEL_FRAME_INFO *preFrameInfo = NULL;
static struct FOUR_CHANNEL_FRAME_INFO *curFrameInfo = NULL;
static unsigned char *preFrameBuffer = NULL;
static unsigned char *curFrameBuffer = NULL;
static bool pr2100Init = false;

bool dual_display_is_black = true;

void pr2100_flip_set(int channelId, int flip)
{
	switch(channelId)
	{
		case 0:
			frameFlip.ch0FrameFlip= flip;
			break;
		case 1:
			frameFlip.ch1FrameFlip= flip;
			break;
		case 2:
			frameFlip.ch2FrameFlip= flip;
			break;
		case 3:
			frameFlip.ch3FrameFlip= flip;
			break;
		default:
			break;
	}
}

static enum CAMERA_FORMAT get_camera_format(void)
{
	enum CAMERA_FORMAT format = FOUR_HD;

	char data = '\0';
	FILE *fd = NULL;
	size_t n = 0;
	int value = 0;

	fd = fopen("sys/class/gpiodrv/gpio_ctrl/360_camtype","r");
	if(fd)
	{
		n = fread(&data,sizeof(data),1,fd);
		fclose(fd);
		if(n == sizeof(data))
			value = (int)(data - '0');
	}

	switch(value)
	{
		case 1:
			format = FOUR_HD;
			break;
		case 2:
			format = CH0HD_CH1HD;
			break;
		case 8:
			format = CH0FHD_CH1FHD;
			break;
		case 9:
			format = CH0FHD_CH1HD;
			break;
		default:
			ALOGE("pr2100 combine had not support");
			break;
	}
	
	return format;
}


void get_camera_display_size(unsigned int *startX, unsigned int *startY, unsigned int *width, unsigned int *height)
{
	DISPLAY_MODE mode = get_camera_display_mode();

	if(cameraFormat == FOUR_HD)
	{
		switch(mode)
		{
			case DISABLE_DISPLAY:
				*startX = 16;
				*startY = 0;
				*width = FRAME_WIDTH;
				*height = FRAME_HEIGHT;
				break;
			case FRONT_DISPLAY:
				*startX = 16;
				*startY = 0;
				*width = FRAME_WIDTH;
				*height = FRAME_HEIGHT;
				break;
			case BACK_DISPLAY:
				*startX = 16+FRAME_WIDTH;
				*startY = 0;
				*width = FRAME_WIDTH;
				*height = FRAME_HEIGHT;
				break;
			case LEFT_DISPLAY:
				*startX = 16+FRAME_WIDTH*2;
				*startY = 0;
				*width = FRAME_WIDTH;
				*height = FRAME_HEIGHT;
				break;
			case RIGHT_DISPLAY:
				*startX = 16+FRAME_WIDTH*3;
				*startY = 0;
				*width = FRAME_WIDTH;
				*height = FRAME_HEIGHT;
				break;
			case QUART_DISPLAY:
				*startX = 16;
				*startY = 0;
				*width = FRAME_WIDTH;
				*height = FRAME_HEIGHT;
				break;
			case FOUR_DISPLAY:
				*startX = 0;
				*startY = 0;
				*width = PR2100_FRAME_WIDTH;
				*height = FRAME_HEIGHT;
				break;
			case UNKNOW_DISPLAY:
				*startX = 0;
				*startY = 0;
				*width = PR2100_FRAME_WIDTH;
				*height = PR2100_FRAME_HEIGHT;
			default:
				break;
		}
	}	
}

static unsigned char* pr2100_alloc_memory(unsigned int memSize)
{
	unsigned char *mem = NULL;

	if(memSize)
	{
		mem = (unsigned char *)malloc(memSize);
		if(mem == NULL)
		{
			ALOGE("pr2100 malloc error");
		}
		else
			memset(mem, 0, memSize);
	}
	return mem;
}

static void collect_channel_frame_info(unsigned char *headAddr, struct CHANNEL_FRAME_INFO *info)
{
	struct PR2100_HEAD_INFO *head = NULL;

	unsigned int curPartFrameNum = 0xff;
	unsigned int curLength = 0;
	unsigned int nextLength = 0;
	unsigned int h = 0;
	unsigned int curPartPreLineNum = 0xffff;
	unsigned int nextPartPreLineNum = 0xffff;

	for(h = 0; h < PR2100_FRAME_HEIGHT; h++)
	{
		head = (struct PR2100_HEAD_INFO *)(headAddr+h*PR2100_FRAME_WIDTH_BIT);
		if(head->LINE_VALID && head->CH_VACT)
		{
			curPartFrameNum = head->FRM_NUM;
			break;
		}
	}

	if(curPartFrameNum == 0xff)
		return;

	for(h = 0; h < PR2100_FRAME_HEIGHT; h++)
	{
		head = (struct PR2100_HEAD_INFO *)(headAddr+h*PR2100_FRAME_WIDTH_BIT);

		if(head->LINE_VALID && head->CH_VACT)
		{
			if(head->FRM_NUM == curPartFrameNum)
			{
				if(curPartPreLineNum != head->VALID_LINE_NUM)
					info->curPart.offset[curLength++] = h*PR2100_FRAME_WIDTH_BIT;

				curPartPreLineNum = head->VALID_LINE_NUM;
			}
			else
			{
				if(nextPartPreLineNum != head->VALID_LINE_NUM)
					info->nextPart.offset[nextLength++] = h*PR2100_FRAME_WIDTH_BIT;

				nextPartPreLineNum = head->VALID_LINE_NUM;
			}
		}

		if(curLength == FRAME_HEIGHT || nextLength == FRAME_HEIGHT)
			break;
	}

	info->curPart.length = curLength;
	info->nextPart.length = nextLength;
}


static void get_head_addr(struct PR2100_CHANNLE_ADDR *pChAddr, unsigned char *src, CH_ORDER order)
{
	switch(order)
	{
		case CH_0123:
			pChAddr->ch0Header = src;
			pChAddr->ch1Header = src+4;
			pChAddr->ch2Header = src+8;
			pChAddr->ch3Header = src+12;

			pChAddr->ch0Addr = src+16;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*3;
			break;
		case CH_0132:
			pChAddr->ch0Header = src;
			pChAddr->ch1Header = src+4;
			pChAddr->ch2Header = src+12;
			pChAddr->ch3Header = src+8;

			pChAddr->ch0Addr = src+16;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*2;
			break;
		case CH_0213:
			pChAddr->ch0Header = src;
			pChAddr->ch1Header = src+8;
			pChAddr->ch2Header = src+4;
			pChAddr->ch3Header = src+12;

			pChAddr->ch0Addr = src+16;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*3;
			break;
		case CH_0231:
			pChAddr->ch0Header = src;
			pChAddr->ch1Header = src+8;
			pChAddr->ch2Header = src+12;
			pChAddr->ch3Header = src+4;

			pChAddr->ch0Addr = src+16;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT;
			break;
		case CH_0312:
			pChAddr->ch0Header = src;
			pChAddr->ch1Header = src+12;
			pChAddr->ch2Header = src+4;
			pChAddr->ch3Header = src+8;

			pChAddr->ch0Addr = src+16;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*2;
			break;
		case CH_0321:
			pChAddr->ch0Header = src;
			pChAddr->ch1Header = src+12;
			pChAddr->ch2Header = src+8;
			pChAddr->ch3Header = src+4;

			pChAddr->ch0Addr = src+16;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT;
			break;

		case CH_1023:
			pChAddr->ch0Header = src+4;
			pChAddr->ch1Header = src;
			pChAddr->ch2Header = src+8;
			pChAddr->ch3Header = src+12;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch1Addr = src+16;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*3;
			break;
		case CH_1032:
			pChAddr->ch0Header = src+4;
			pChAddr->ch1Header = src;
			pChAddr->ch2Header = src+12;
			pChAddr->ch3Header = src+8;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch1Addr = src+16;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*2;
			break;
		case CH_1203:
			pChAddr->ch0Header = src+4;
			pChAddr->ch1Header = src+8;
			pChAddr->ch2Header = src;
			pChAddr->ch3Header = src+12;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch2Addr = src+16;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*3;
			break;
		case CH_1230:
			pChAddr->ch0Header = src+4;
			pChAddr->ch1Header = src+8;
			pChAddr->ch2Header = src+12;
			pChAddr->ch3Header = src;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch3Addr = src+16;
			break;
		case CH_1302:
			pChAddr->ch0Header = src+4;
			pChAddr->ch1Header = src+12;
			pChAddr->ch2Header = src;
			pChAddr->ch3Header = src+8;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch2Addr = src+16;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*2;
			break;
		case CH_1320:
			pChAddr->ch0Header = src+4;
			pChAddr->ch1Header = src+12;
			pChAddr->ch2Header = src+8;
			pChAddr->ch3Header = src;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch3Addr = src+16;
			break;

		case CH_2013:
			pChAddr->ch0Header = src+8;
			pChAddr->ch1Header = src;
			pChAddr->ch2Header = src+4;
			pChAddr->ch3Header = src+12;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch1Addr = src+16;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*3;
			break;
		case CH_2031:
			pChAddr->ch0Header = src+8;
			pChAddr->ch1Header = src;
			pChAddr->ch2Header = src+12;
			pChAddr->ch3Header = src+4;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch1Addr = src+16;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT;
			break;
		case CH_2103:
			pChAddr->ch0Header = src+8;
			pChAddr->ch1Header = src+4;
			pChAddr->ch2Header = src;
			pChAddr->ch3Header = src+12;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch2Addr = src+16;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*3;
			break;
		case CH_2130:
			pChAddr->ch0Header = src+8;
			pChAddr->ch1Header = src+4;
			pChAddr->ch2Header = src+12;
			pChAddr->ch3Header = src;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch3Addr = src+16;
			break;
		case CH_2301:
			pChAddr->ch0Header = src+8;
			pChAddr->ch1Header = src+12;
			pChAddr->ch2Header = src;
			pChAddr->ch3Header = src+4;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch2Addr = src+16;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT;
			break;
		case CH_2310:
			pChAddr->ch0Header = src+8;
			pChAddr->ch1Header = src+12;
			pChAddr->ch2Header = src+4;
			pChAddr->ch3Header = src;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch3Addr = src+16;
			break;

		case CH_3012:
			pChAddr->ch0Header = src+12;
			pChAddr->ch1Header = src;
			pChAddr->ch2Header = src+4;
			pChAddr->ch3Header = src+8;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch1Addr = src+16;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*2;
			break;
		case CH_3021:
			pChAddr->ch0Header = src+12;
			pChAddr->ch1Header = src;
			pChAddr->ch2Header = src+8;
			pChAddr->ch3Header = src+4;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch1Addr = src+16;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT;
			break;
		case CH_3102:
			pChAddr->ch0Header = src+12;
			pChAddr->ch1Header = src+4;
			pChAddr->ch2Header = src;
			pChAddr->ch3Header = src+8;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch2Addr = src+16;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT*2;
			break;
		case CH_3120:
			pChAddr->ch0Header = src+12;
			pChAddr->ch1Header = src+4;
			pChAddr->ch2Header = src+8;
			pChAddr->ch3Header = src;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch3Addr = src+16;
			break;
		case CH_3201:
			pChAddr->ch0Header = src+12;
			pChAddr->ch1Header = src+8;
			pChAddr->ch2Header = src;
			pChAddr->ch3Header = src+4;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch2Addr = src+16;
			pChAddr->ch3Addr = src+16+FRAME_WIDTH_BIT;
			break;
		case CH_3210:
			pChAddr->ch0Header = src+12;
			pChAddr->ch1Header = src+8;
			pChAddr->ch2Header = src+4;
			pChAddr->ch3Header = src;

			pChAddr->ch0Addr = src+16+FRAME_WIDTH_BIT*3;
			pChAddr->ch1Addr = src+16+FRAME_WIDTH_BIT*2;
			pChAddr->ch2Addr = src+16+FRAME_WIDTH_BIT;
			pChAddr->ch3Addr = src+16;
			break;
		default:
			break;
	}
}


static void four_channel_update_addr(unsigned char *frameAddr, struct FOUR_CHANNEL_FRAME_INFO *info)
{
	struct CHANNEL_FRAME_INFO *ch0Info = &(info->ch0FrameInfo);
	struct CHANNEL_FRAME_INFO *ch1Info = &(info->ch1FrameInfo);
	struct CHANNEL_FRAME_INFO *ch2Info = &(info->ch2FrameInfo);
	struct CHANNEL_FRAME_INFO *ch3Info = &(info->ch3FrameInfo);

	ch0Info->chAddr = frameAddr+16;
	ch1Info->chAddr = frameAddr+16+FRAME_WIDTH_BIT;
	ch2Info->chAddr = frameAddr+16+FRAME_WIDTH_BIT*2;
	ch3Info->chAddr = frameAddr+16+FRAME_WIDTH_BIT*3;
}

static void collect_four_channel_frame_info(unsigned char *startAddr, struct FOUR_CHANNEL_FRAME_INFO *info)
{
	struct PR2100_CHANNLE_ADDR pChAddr;
	struct CHANNEL_FRAME_INFO *ch0Info = &(info->ch0FrameInfo);
	struct CHANNEL_FRAME_INFO *ch1Info = &(info->ch1FrameInfo);
	struct CHANNEL_FRAME_INFO *ch2Info = &(info->ch2FrameInfo);
	struct CHANNEL_FRAME_INFO *ch3Info = &(info->ch3FrameInfo);

	get_head_addr(&pChAddr, startAddr, pr2100Obj->chOrder);
	collect_channel_frame_info(pChAddr.ch0Header, ch0Info);
	collect_channel_frame_info(pChAddr.ch1Header, ch1Info);
	collect_channel_frame_info(pChAddr.ch2Header, ch2Info);
	collect_channel_frame_info(pChAddr.ch3Header, ch3Info);

	ch0Info->chAddr = pChAddr.ch0Addr;
	ch1Info->chAddr = pChAddr.ch1Addr;
	ch2Info->chAddr = pChAddr.ch2Addr;
	ch3Info->chAddr = pChAddr.ch3Addr;

}

static void pr2100_display_one_channel_four_hd(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, DISPLAY_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *v = dst+FRAME_WIDTH*FRAME_HEIGHT;
	unsigned char *u = v+FRAME_WIDTH*FRAME_HEIGHT/4;	
	unsigned int w,h = 0;
	unsigned int indexY = 0;
	unsigned int indexU = 0;
	unsigned int indexV = 0;
	int flip = 0;

	if(frameInfo == NULL || pFrameInfo == NULL)
		return;

	switch(mode)
	{
		case FRONT_DISPLAY:
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_DISPLAY:
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_DISPLAY:
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_DISPLAY:
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}
			break;
		case 1:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}

					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
					
					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
				}
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				for(w = FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}

					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}

					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
				}
			}
			break;
		default:
			break;
	}
}

static void pr2100_record_one_of_four_channel_hd(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, RECORD_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *u = dst+FRAME_WIDTH*FRAME_HEIGHT;
	unsigned char *v = u+FRAME_WIDTH*FRAME_HEIGHT/4;	
	unsigned int w,h = 0;
	unsigned int indexY = 0;
	unsigned int indexU = 0;
	unsigned int indexV = 0;
	int flip = 0;

	if(frameInfo == NULL || pFrameInfo == NULL)
		return;

	switch(mode)
	{
		case FRONT_RECORD:
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_RECORD:
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_RECORD:
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_RECORD:
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}
			break;
		case 1:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}

					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
					
					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
				}
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				for(w = FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}

					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}

					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
				}
			}
			break;
		default:
			break;
	}	
}

static void pr2100_record_one_of_four_channel_qhd(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, RECORD_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *u = dst+960*540;
	unsigned char *v = u+960*540/4;	
	unsigned int w,h,dst_h = 0;
	unsigned int indexY = 0;
	unsigned int indexU = 0;
	unsigned int indexV = 0;
	int flip = 0;

	if(frameInfo == NULL || pFrameInfo == NULL)
		return;

	switch(mode)
	{
		case FRONT_RECORD:
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_RECORD:
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_RECORD:
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_RECORD:
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				if((h&0x3) == 0x3)
					continue;

				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
				{
					*(y+indexY++) = *(srcAddr+w+1);
					*(y+indexY++) = *(srcAddr+w+3);
					*(y+indexY++) = *(srcAddr+w+5);
					*(y+indexY++) = *(srcAddr+w+7);
					*(y+indexY++) = *(srcAddr+w+9);
					*(y+indexY++) = *(srcAddr+w+11);
				}

				if((dst_h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
						*(u+indexU++) = *(srcAddr+w+4);
						*(v+indexV++) = *(srcAddr+w+6);
						*(u+indexU++) = *(srcAddr+w+8);
						*(v+indexV++) = *(srcAddr+w+10);
					}
				}
				dst_h++;
			}

			for(h = 0; h < bottomLength; h++)
			{
				if(((topLength+h)&0x03) == 0x3)
					continue;

				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
				{
					*(y+indexY++) = *(srcAddr+w+1);
					*(y+indexY++) = *(srcAddr+w+3);
					*(y+indexY++) = *(srcAddr+w+5);
					*(y+indexY++) = *(srcAddr+w+7);
					*(y+indexY++) = *(srcAddr+w+9);
					*(y+indexY++) = *(srcAddr+w+11);
				}

				if((dst_h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
						*(u+indexU++) = *(srcAddr+w+4);
						*(v+indexV++) = *(srcAddr+w+6);
						*(u+indexU++) = *(srcAddr+w+8);
						*(v+indexV++) = *(srcAddr+w+10);
					}
				}
				dst_h++;
			}
			break;
		case 1:
			for(h = 0; h < topLength; h++)
			{
				if((h&0x3) == 0x3)
					continue;

				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-16; w != 0; w-=16)
				{
					*(y+indexY++) = *(srcAddr+w+1);
					*(y+indexY++) = *(srcAddr+w+3);
					*(y+indexY++) = *(srcAddr+w+5);
					*(y+indexY++) = *(srcAddr+w+7);
					*(y+indexY++) = *(srcAddr+w+9);
					*(y+indexY++) = *(srcAddr+w+11);
				}
				*(y+indexY++) = *(srcAddr+1);
				*(y+indexY++) = *(srcAddr+3);
				*(y+indexY++) = *(srcAddr+5);
				*(y+indexY++) = *(srcAddr+7);
				*(y+indexY++) = *(srcAddr+9);
				*(y+indexY++) = *(srcAddr+11);

				if((dst_h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-16; w != 0; w-=16)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
						*(u+indexU++) = *(srcAddr+w+4);
						*(v+indexV++) = *(srcAddr+w+6);
						*(u+indexU++) = *(srcAddr+w+8);
						*(v+indexV++) = *(srcAddr+w+10);
					}

					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
					*(u+indexU++) = *(srcAddr+4);
					*(v+indexV++) = *(srcAddr+6);
					*(u+indexU++) = *(srcAddr+8);
					*(v+indexV++) = *(srcAddr+10);
				}
				dst_h++;
			}

			for(h = 0; h < bottomLength; h++)
			{
				if(((topLength+h)&0x03) == 0x3)
					continue;

				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-16; w !=0; w-=16)
				{
					*(y+indexY++) = *(srcAddr+w+1);
					*(y+indexY++) = *(srcAddr+w+3);
					*(y+indexY++) = *(srcAddr+w+5);
					*(y+indexY++) = *(srcAddr+w+7);
					*(y+indexY++) = *(srcAddr+w+9);
					*(y+indexY++) = *(srcAddr+w+11);
				}
				*(y+indexY++) = *(srcAddr+1);
				*(y+indexY++) = *(srcAddr+3);
				*(y+indexY++) = *(srcAddr+5);
				*(y+indexY++) = *(srcAddr+7);
				*(y+indexY++) = *(srcAddr+9);
				*(y+indexY++) = *(srcAddr+11);

				if((dst_h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-16; w != 0; w-=16)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
						*(u+indexU++) = *(srcAddr+w+4);
						*(v+indexV++) = *(srcAddr+w+6);
						*(u+indexU++) = *(srcAddr+w+8);
						*(v+indexV++) = *(srcAddr+w+10);
					}
					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
					*(u+indexU++) = *(srcAddr+4);
					*(v+indexV++) = *(srcAddr+6);
					*(u+indexU++) = *(srcAddr+8);
					*(v+indexV++) = *(srcAddr+10);
				}
				dst_h++;
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h++)
			{
				if((h&0x3) == 0x3)
					continue;

				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
				{
					*(y+indexY++) = *(srcAddr+w+1);
					*(y+indexY++) = *(srcAddr+w+3);
					*(y+indexY++) = *(srcAddr+w+5);
					*(y+indexY++) = *(srcAddr+w+7);
					*(y+indexY++) = *(srcAddr+w+9);
					*(y+indexY++) = *(srcAddr+w+11);
				}

				if((dst_h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
						*(u+indexU++) = *(srcAddr+w+4);
						*(v+indexV++) = *(srcAddr+w+6);
						*(u+indexU++) = *(srcAddr+w+8);
						*(v+indexV++) = *(srcAddr+w+10);
					}
				}
				dst_h++;
			}

			for(h = 0; h < topLength; h++)
			{
				if(((bottomLength+h)&0x03) == 0x3)
					continue;

				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
				{
					*(y+indexY++) = *(srcAddr+w+1);
					*(y+indexY++) = *(srcAddr+w+3);
					*(y+indexY++) = *(srcAddr+w+5);
					*(y+indexY++) = *(srcAddr+w+7);
					*(y+indexY++) = *(srcAddr+w+9);
					*(y+indexY++) = *(srcAddr+w+11);
				}

				if((dst_h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
						*(u+indexU++) = *(srcAddr+w+4);
						*(v+indexV++) = *(srcAddr+w+6);
						*(u+indexU++) = *(srcAddr+w+8);
						*(v+indexV++) = *(srcAddr+w+10);
					}
				}
				dst_h++;
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h++)
			{
				if((h&0x3) == 0x3)
					continue;

				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				for(w = FRAME_WIDTH_BIT-16; w != 0; w-=16)
				{
					*(y+indexY++) = *(srcAddr+w+1);
					*(y+indexY++) = *(srcAddr+w+3);
					*(y+indexY++) = *(srcAddr+w+5);
					*(y+indexY++) = *(srcAddr+w+7);
					*(y+indexY++) = *(srcAddr+w+9);
					*(y+indexY++) = *(srcAddr+w+11);
				}
				*(y+indexY++) = *(srcAddr+1);
				*(y+indexY++) = *(srcAddr+3);
				*(y+indexY++) = *(srcAddr+5);
				*(y+indexY++) = *(srcAddr+7);
				*(y+indexY++) = *(srcAddr+9);
				*(y+indexY++) = *(srcAddr+11);

				if((dst_h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-16; w != 0; w-=16)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
						*(u+indexU++) = *(srcAddr+w+4);
						*(v+indexV++) = *(srcAddr+w+6);
						*(u+indexU++) = *(srcAddr+w+8);
						*(v+indexV++) = *(srcAddr+w+10);
					}
					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
					*(u+indexU++) = *(srcAddr+4);
					*(v+indexV++) = *(srcAddr+6);
					*(u+indexU++) = *(srcAddr+8);
					*(v+indexV++) = *(srcAddr+10);
				}
				dst_h++;
			}

			for(h = 0; h < topLength; h++)
			{
				if(((bottomLength+h)&0x03) == 0x3)
					continue;

				for(w = FRAME_WIDTH_BIT-16; w != 0; w-=16)
				{
					*(y+indexY++) = *(srcAddr+w+1);
					*(y+indexY++) = *(srcAddr+w+3);
					*(y+indexY++) = *(srcAddr+w+5);
					*(y+indexY++) = *(srcAddr+w+7);
					*(y+indexY++) = *(srcAddr+w+9);
					*(y+indexY++) = *(srcAddr+w+11);
				}
				*(y+indexY++) = *(srcAddr+1);
				*(y+indexY++) = *(srcAddr+3);
				*(y+indexY++) = *(srcAddr+5);
				*(y+indexY++) = *(srcAddr+7);
				*(y+indexY++) = *(srcAddr+9);
				*(y+indexY++) = *(srcAddr+11);

				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				if((dst_h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-16; w != 0; w-=16)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
						*(u+indexU++) = *(srcAddr+w+4);
						*(v+indexV++) = *(srcAddr+w+6);
						*(u+indexU++) = *(srcAddr+w+8);
						*(v+indexV++) = *(srcAddr+w+10);
					}
					*(u+indexU++) = *(srcAddr);
					*(v+indexV++) = *(srcAddr+2);
					*(u+indexU++) = *(srcAddr+4);
					*(v+indexV++) = *(srcAddr+6);
					*(u+indexU++) = *(srcAddr+8);
					*(v+indexV++) = *(srcAddr+10);
				}
				dst_h++;
			}
			break;
		default:
			break;
	}	
}

static void pr2100_capture_one_of_four_channel_hd(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, CAPTURE_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *uv = dst+FRAME_WIDTH*FRAME_HEIGHT;
	unsigned int w,h = 0;
	unsigned int indexY = 0;
	unsigned int indexUV = 0;
	int flip = 0;

	if(frameInfo == NULL || pFrameInfo == NULL)
		return;

	switch(mode)
	{
		case FRONT_CAPTURE:
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_CAPTURE:
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_CAPTURE:
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_CAPTURE:
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}
				}
			}
			break;
		case 1:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}

					*(uv+indexUV++) = *(srcAddr+2);
					*(uv+indexUV++) = *(srcAddr);
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}

					*(uv+indexUV++) = *(srcAddr+2);
					*(uv+indexUV++) = *(srcAddr);
				}
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}
				}
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}
					*(uv+indexUV++) = *(srcAddr+2);
					*(uv+indexUV++) = *(srcAddr);
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}

					*(uv+indexUV++) = *(srcAddr+2);
					*(uv+indexUV++) = *(srcAddr);
				}
			}
			break;
		default:
			break;
	}
}

static void pr2100_capture_one_channel_four_hd(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, CAPTURE_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = NULL;
	unsigned char *uv = NULL;
	unsigned int w,h = 0;
	unsigned int dstYOffset = 0;
	unsigned int dstUVOffset = 0;
	unsigned int indexY = 0;
	unsigned int indexUV = 0;
	int flip = 0;

	if(frameInfo == NULL || pFrameInfo == NULL)
		return;

	switch(mode)
	{
		case FRONT_CAPTURE:
			y = dst;
			uv = dst + FRAME_WIDTH*FRAME_HEIGHT*4;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_CAPTURE:
			y = dst + FRAME_WIDTH;
			uv = dst + FRAME_WIDTH*FRAME_HEIGHT*4 + FRAME_WIDTH;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_CAPTURE:
			y = dst + FRAME_WIDTH*FRAME_HEIGHT*2;
			uv = dst + FRAME_WIDTH*FRAME_HEIGHT*4 + FRAME_WIDTH*FRAME_HEIGHT;
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_CAPTURE:
			y = dst + FRAME_WIDTH*FRAME_HEIGHT*2 + FRAME_WIDTH;
			uv = dst + FRAME_WIDTH*FRAME_HEIGHT*4 + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH;
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				dstYOffset = h * FRAME_WIDTH*2;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					dstUVOffset = h*FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength + h) * FRAME_WIDTH*2;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				if(((h+topLength)&1) == 0)
				{
					dstUVOffset = (topLength+h)/2*FRAME_WIDTH*2;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}
				}	
			}
			break;
		case 1:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				dstYOffset = h * FRAME_WIDTH*2;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				*(y+dstYOffset+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					dstUVOffset = h*FRAME_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(srcAddr+2);
					*(uv+dstUVOffset+indexUV++) = *(srcAddr);
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength + h) * FRAME_WIDTH*2;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				*(y+dstYOffset+indexY++) = *(srcAddr+1);

				if(((h+topLength)&1) == 0)
				{
					dstUVOffset = (topLength+h)/2*FRAME_WIDTH*2;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(srcAddr+2);
					*(uv+dstUVOffset+indexUV++) = *(srcAddr);
				}	
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h * FRAME_WIDTH*2;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					dstUVOffset = h*FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength + h) * FRAME_WIDTH*2;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					dstUVOffset = (bottomLength+h)/2*FRAME_WIDTH*2;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}
				}	
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr+bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h * FRAME_WIDTH*2;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				*(y+dstYOffset+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					dstUVOffset = h*FRAME_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(srcAddr+2);
					*(uv+dstUVOffset+indexUV++) = *(srcAddr);
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength + h) * FRAME_WIDTH*2;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				*(y+dstYOffset+indexY++) = *(srcAddr+1);

				if(((h+bottomLength)&1) == 0)
				{
					dstUVOffset = (bottomLength+h)/2*FRAME_WIDTH*2;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(srcAddr+2);
					*(uv+dstUVOffset+indexUV++) = *(srcAddr);
				}	
			}
			break;
		default:
			break;
	}


}

static void pr2100_yuv422_to_quart_yv12(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst,DISPLAY_MODE mode)
{
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = NULL;
	unsigned char *u = NULL;
	unsigned char *v = NULL;
	unsigned char *src = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	unsigned int w,h = 0;
	unsigned int dstYOffset = 0;
	unsigned int dstUVOffset = 0;
	unsigned int indexY = 0;
	unsigned int indexU = 0;
	unsigned int indexV = 0;

	unsigned int bottomStartOfY = 0;
	unsigned int bottomStartOfUV = 0;
	unsigned int bottomUvFlag = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_DISPLAY:
			y = dst;
			v = dst + FRAME_WIDTH*FRAME_HEIGHT;
			u = dst + FRAME_WIDTH*FRAME_HEIGHT*5/4;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_DISPLAY:
			y = dst + FRAME_WIDTH/2;
			v = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH/4;
			u = dst + FRAME_WIDTH*FRAME_HEIGHT/4*5+FRAME_WIDTH/4;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_DISPLAY:
			y = dst + FRAME_WIDTH*FRAME_HEIGHT/2;
			v = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH*FRAME_HEIGHT/8;
			u = dst + FRAME_WIDTH*FRAME_HEIGHT/4*5+FRAME_WIDTH*FRAME_HEIGHT/8;
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_DISPLAY:
			y = dst + FRAME_WIDTH*FRAME_HEIGHT/2 +  FRAME_WIDTH/2;
			v = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH*FRAME_HEIGHT/8 + FRAME_WIDTH/4;
			u = dst + FRAME_WIDTH*FRAME_HEIGHT/4*5+FRAME_WIDTH*FRAME_HEIGHT/8 + FRAME_WIDTH/4;
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*FRAME_WIDTH;
					else
						dstUVOffset += FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;
			if(topLength > 0)
			{
				bottomUvFlag = 1 -(((topLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src =bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/8)*FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((topLength+h)/8)*FRAME_WIDTH;
						else
							dstUVOffset += FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 1:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*FRAME_WIDTH;
					else
						dstUVOffset += FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;
			if(topLength > 0)
			{
				bottomUvFlag = 1 -( ((topLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src =bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/8)*FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((topLength+h)/8)*FRAME_WIDTH;
						else
							dstUVOffset += FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}	
			break;
		case 2:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*FRAME_WIDTH;
					else
						dstUVOffset += FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;
			if(bottomLength > 0)
			{
				bottomUvFlag = 1 -(((bottomLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src =topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/8)*FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((bottomLength+h)/8)*FRAME_WIDTH;
						else
							dstUVOffset += FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 3:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*FRAME_WIDTH;
					else
						dstUVOffset += FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;
			if(bottomLength > 0)
			{
				bottomUvFlag = 1 -(((bottomLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src =topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/8)*FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((bottomLength+h)/8)*FRAME_WIDTH;
						else
							dstUVOffset += FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}	
			break;
		default:
			break;
	}
}

static void pr2100_yuv422_to_quart_yuv420(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst,RECORD_MODE mode)
{
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = NULL;
	unsigned char *u = NULL;
	unsigned char *v = NULL;
	unsigned char *src = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	unsigned int w,h = 0;
	unsigned int dstYOffset = 0;
	unsigned int dstUVOffset = 0;
	unsigned int indexY = 0;
	unsigned int indexU = 0;
	unsigned int indexV = 0;

	unsigned int bottomStartOfY = 0;
	unsigned int bottomStartOfUV = 0;
	unsigned int bottomUvFlag = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_RECORD:
			y = dst;
			u = y + FRAME_WIDTH*FRAME_HEIGHT;
			v = u + FRAME_WIDTH*FRAME_HEIGHT/4;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_RECORD:
			y = dst + FRAME_WIDTH/2;
			u = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH/4;
			v = dst + FRAME_WIDTH*FRAME_HEIGHT/4*5+FRAME_WIDTH/4;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_RECORD:
			y = dst + FRAME_WIDTH*FRAME_HEIGHT/2;
			u = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH*FRAME_HEIGHT/8;
			v = dst + FRAME_WIDTH*FRAME_HEIGHT/4*5+FRAME_WIDTH*FRAME_HEIGHT/8;
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_RECORD:
			y = dst + FRAME_WIDTH*FRAME_HEIGHT/2 +  FRAME_WIDTH/2;
			u = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH*FRAME_HEIGHT/8 + FRAME_WIDTH/4;
			v = dst + FRAME_WIDTH*FRAME_HEIGHT/4*5+FRAME_WIDTH*FRAME_HEIGHT/8 + FRAME_WIDTH/4;
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*FRAME_WIDTH;
					else
						dstUVOffset += FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;
			if(topLength > 0)
			{
				bottomUvFlag = 1 -(((topLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/8)*FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((topLength+h)/8)*FRAME_WIDTH;
						else
							dstUVOffset += FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 1:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*FRAME_WIDTH;
					else
						dstUVOffset += FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;
			if(topLength > 0)
			{
				bottomUvFlag = 1 -(((topLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/8)*FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((topLength+h)/8)*FRAME_WIDTH;
						else
							dstUVOffset += FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}	
			break;
		case 2:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*FRAME_WIDTH;
					else
						dstUVOffset += FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;
			if(bottomLength > 0)
			{
				bottomUvFlag = 1 -(((bottomLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/8)*FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((bottomLength+h)/8)*FRAME_WIDTH;
						else
							dstUVOffset += FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 3:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*FRAME_WIDTH;
					else
						dstUVOffset += FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;
			if(bottomLength > 0)
			{
				bottomUvFlag = 1 -(((bottomLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/8)*FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((bottomLength+h)/8)*FRAME_WIDTH;
						else
							dstUVOffset += FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}	
			break;
		default:
			break;
	}		
}

static void pr2100_yuv422_to_quart_nv21(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst,CAPTURE_MODE mode)
{
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = NULL;
	unsigned char *uv = NULL;
	unsigned char *src = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	unsigned int w,h = 0;
	unsigned int dstYOffset = 0;
	unsigned int dstUVOffset = 0;
	unsigned int indexY = 0;
	unsigned int indexUV = 0;

	unsigned int bottomStartOfY = 0;
	unsigned int bottomStartOfUV = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_CAPTURE:
			y = dst;
			uv = y + FRAME_WIDTH*FRAME_HEIGHT;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_CAPTURE:
			y = dst + FRAME_WIDTH/2;
			uv = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH/2;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_CAPTURE:
			y = dst + FRAME_WIDTH*FRAME_HEIGHT/2;
			uv = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH*FRAME_HEIGHT/4;
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_CAPTURE:
			y = dst + FRAME_WIDTH*FRAME_HEIGHT/2 +  FRAME_WIDTH/2;
			uv = dst + FRAME_WIDTH*FRAME_HEIGHT + FRAME_WIDTH*FRAME_HEIGHT/4 + FRAME_WIDTH/2;
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					dstUVOffset = h/4*FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src =bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					dstUVOffset = (topLength+h)/4*FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}
			break;
		case 1:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					dstUVOffset = h/4*FRAME_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src =bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					dstUVOffset = (topLength+h)/4*FRAME_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					dstUVOffset = h/4*FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					dstUVOffset = (bottomLength+h)/4*FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					dstUVOffset = h/4*FRAME_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/2*FRAME_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					dstUVOffset = (bottomLength+h)/4*FRAME_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}
			break;
		default:
			break;
	}

}

static void pr2100_360p_scale_nv21(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst,H264_MODE mode)
{
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *uv = y + FRAME_WIDTH*FRAME_HEIGHT/4;
	unsigned char *src = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	unsigned int w,h = 0;
	unsigned int indexY = 0;
	unsigned int indexUV = 0;

	unsigned int bottomStartOfY = 0;
	unsigned int bottomStartOfUV = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_H264:
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_H264:
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_H264:
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_H264:
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+indexUV++) = *(src+w+2);
						*(uv+indexUV++) = *(src+w);
					}
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+indexUV++) = *(src+w+2);
						*(uv+indexUV++) = *(src+w);
					}
				}
			}
			break;
		case 1:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+indexUV++) = *(src+w+2);
						*(uv+indexUV++) = *(src+w);
					}

					*(uv+indexUV++) = *(src+2);
					*(uv+indexUV++) = *(src);
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+indexUV++) = *(src+w+2);
						*(uv+indexUV++) = *(src+w);
					}

					*(uv+indexUV++) = *(src+2);
					*(uv+indexUV++) = *(src);
				}
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+indexUV++) = *(src+w+2);
						*(uv+indexUV++) = *(src+w);
					}
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+indexUV++) = *(src+w+2);
						*(uv+indexUV++) = *(src+w);
					}
				}
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+indexUV++) = *(src+w+2);
						*(uv+indexUV++) = *(src+w);
					}

					*(uv+indexUV++) = *(src+2);
					*(uv+indexUV++) = *(src);
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+indexUV++) = *(src+w+2);
						*(uv+indexUV++) = *(src+w);
					}

					*(uv+indexUV++) = *(src+2);
					*(uv+indexUV++) = *(src);
				}
			}
			break;
		default:
			break;
	}

}

static void pr2100_360p_scale_quart_nv21(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst,H264_MODE mode)
{
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = NULL;
	unsigned char *uv = NULL;
	unsigned char *src = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	unsigned int w,h = 0;
	unsigned int dstYOffset = 0;
	unsigned int dstUVOffset = 0;
	unsigned int indexY = 0;
	unsigned int indexUV = 0;

	unsigned int bottomStartOfY = 0;
	unsigned int bottomStartOfUV = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_H264:
			y = dst;
			uv = y + FRAME_360P_WIDTH*FRAME_360P_HEIGHT;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_H264:
			y = dst + FRAME_360P_WIDTH/2;
			uv = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT + FRAME_360P_WIDTH/2;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_H264:
			y = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/2;
			uv = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/4;
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_H264:
			y = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/2 +  FRAME_360P_WIDTH/2;
			uv = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/4 + FRAME_360P_WIDTH/2;
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=4)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&7) == 0)
				{
					dstUVOffset = h/8*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}

			bottomStartOfY = (4-(topLength&3))&3;
			bottomStartOfUV = (8 - (topLength&7))&7;

			for(h = bottomStartOfY; h < bottomLength; h+=4)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&7) == 0)
				{
					dstUVOffset = (topLength+h)/8*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}
			break;
		case 1:
			for(h = 0; h < topLength; h+=4)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&7) == 0)
				{
					dstUVOffset = h/8*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}

			bottomStartOfY = (4-(topLength&3))&3;
			bottomStartOfUV = (8 - (topLength&7))&7;

			for(h = bottomStartOfY; h < bottomLength; h+=4)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&7) == 0)
				{
					dstUVOffset = (topLength+h)/8*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h+=4)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&7) == 0)
				{
					dstUVOffset = h/8*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}

			bottomStartOfY = (4-(bottomLength&3))&3;
			bottomStartOfUV = (8 - (bottomLength&7))&7;

			for(h = bottomStartOfY; h < topLength; h+=4)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&7) == 0)
				{
					dstUVOffset = (bottomLength+h)/8*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h+=4)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&7) == 0)
				{
					dstUVOffset = h/8*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}

			bottomStartOfY = (4-(bottomLength&3))&3;
			bottomStartOfUV = (8 - (bottomLength&7))&7;

			for(h = bottomStartOfY; h < topLength; h+=4)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&7) == 0)
				{
					dstUVOffset = (bottomLength+h)/8*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}
			break;
		default:
			break;
	}
}

static void pr2100_360p_scale_yuv420(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, H264_MODE mode)
{
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *u = y + FRAME_WIDTH*FRAME_HEIGHT/4;
	unsigned char *v = u + FRAME_WIDTH*FRAME_HEIGHT/16;
	unsigned char *src = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	unsigned int w,h = 0;
	unsigned int indexY = 0;
	unsigned int indexU = 0;
	unsigned int indexV = 0;
	unsigned int bottomStartOfY = 0;
	unsigned int bottomStartOfUV = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_H264:
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_H264:
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_H264:
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_H264:
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+indexU++) = *(src+w);
						*(v+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+indexU++) = *(src+w);
						*(v+indexV++) = *(src+w+2);
					}
				}
			}
			break;
		case 1:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(u+indexU++) = *(src+w);
						*(v+indexV++) = *(src+w+2);
					}

					*(u+indexU++) = *(src);
					*(v+indexV++) = *(src+2);
				}
			}

			bottomStartOfY = topLength&1;
			bottomStartOfUV = (4 - (topLength&3))&3;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(u+indexU++) = *(src+w);
						*(v+indexV++) = *(src+w+2);
					}

					*(u+indexU++) = *(src);
					*(v+indexV++) = *(src+2);
				}
			}
			break;
		case 2:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+indexU++) = *(src+w);
						*(v+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					{
						*(u+indexU++) = *(src+w);
						*(v+indexV++) = *(src+w+2);
					}
				}
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h+=2)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(u+indexU++) = *(src+w);
						*(v+indexV++) = *(src+w+2);
					}

					*(u+indexU++) = *(src);
					*(v+indexV++) = *(src+2);
				}
			}

			bottomStartOfY = bottomLength&1;
			bottomStartOfUV = (4 - (bottomLength&3))&3;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(u+indexU++) = *(src+w);
						*(v+indexV++) = *(src+w+2);
					}

					*(u+indexU++) = *(src);
					*(v+indexV++) = *(src+2);
				}
			}
			break;
		default:
			break;
	}

}

static void pr2100_360p_scale_quart_yuv420(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst,H264_MODE mode)
{
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = NULL;
	unsigned char *u = NULL;
	unsigned char *v = NULL;
	unsigned char *src = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	unsigned int w,h = 0;
	unsigned int dstYOffset = 0;
	unsigned int dstUVOffset = 0;
	unsigned int indexY = 0;
	unsigned int indexU = 0;
	unsigned int indexV = 0;

	unsigned int bottomStartOfY = 0;
	unsigned int bottomStartOfUV = 0;
	unsigned int bottomUvFlag = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_H264:
			y = dst;
			u = y + FRAME_360P_WIDTH*FRAME_360P_HEIGHT;
			v = u + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/4;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_H264:
			y = dst + FRAME_360P_WIDTH/2;
			u = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT + FRAME_360P_WIDTH/4;
			v = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/4*5+FRAME_360P_WIDTH/4;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		case LEFT_H264:
			y = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/2;
			u = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/8;
			v = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/4*5+FRAME_360P_WIDTH*FRAME_360P_HEIGHT/8;
			topPartInfo = &(pFrameInfo->ch2FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch2FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch2FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch2FrameInfo.chAddr;
			flip = frameFlip.ch2FrameFlip;
			break;
		case RIGHT_H264:
			y = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/2 +  FRAME_360P_WIDTH/2;
			u = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/8 + FRAME_360P_WIDTH/4;
			v = dst + FRAME_360P_WIDTH*FRAME_360P_HEIGHT/4*5+FRAME_360P_WIDTH*FRAME_360P_HEIGHT/8 + FRAME_360P_WIDTH/4;
			topPartInfo = &(pFrameInfo->ch3FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch3FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch3FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch3FrameInfo.chAddr;
			flip = frameFlip.ch3FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > FRAME_HEIGHT)
		topLength = FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=4)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&7) == 0)
				{
					if((h&0xf) == 0)
						dstUVOffset = h/16*FRAME_360P_WIDTH;
					else
						dstUVOffset += FRAME_360P_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfY = (4 - topLength&3)&3;
			bottomStartOfUV = (8 - (topLength&7))&7;
			if(topLength > 0)
			{
				bottomUvFlag = 1 -( ((topLength-1)/8)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < bottomLength; h+=4)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&7) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&0xf) == 0)
							dstUVOffset += FRAME_360P_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/16)*FRAME_360P_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&0xf) == 0)
							dstUVOffset = ((topLength+h)/16)*FRAME_360P_WIDTH;
						else
							dstUVOffset += FRAME_360P_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 1:
			for(h = 0; h < topLength; h+=4)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&7) == 0)
				{
					if((h&0xf) == 0)
						dstUVOffset = h/16*FRAME_360P_WIDTH;
					else
						dstUVOffset += FRAME_360P_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfY = (4 - topLength&3)&3;
			bottomStartOfUV = (8 - (topLength&7))&7;
			if(topLength > 0)
			{
				bottomUvFlag = 1 -( ((topLength-1)/8)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < bottomLength; h+=4)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&7) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&0xf) == 0)
							dstUVOffset += FRAME_360P_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/16)*FRAME_360P_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&0xf) == 0)
							dstUVOffset = ((topLength+h)/16)*FRAME_360P_WIDTH;
						else
							dstUVOffset += FRAME_360P_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}	
			break;
		case 2:
			for(h = 0; h < bottomLength; h+=4)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&7) == 0)
				{
					if((h&0xf) == 0)
						dstUVOffset = h/16*FRAME_360P_WIDTH;
					else
						dstUVOffset += FRAME_360P_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfY = (4 - bottomLength&3)&3;
			bottomStartOfUV = (8 - (bottomLength&7))&7;
			if(bottomLength > 0)
			{
				bottomUvFlag = 1 -( ((bottomLength-1)/8)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < topLength; h+=4)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&7) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&0xf) == 0)
							dstUVOffset += FRAME_360P_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/16)*FRAME_360P_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&0xf) == 0)
							dstUVOffset = ((bottomLength+h)/16)*FRAME_360P_WIDTH;
						else
							dstUVOffset += FRAME_360P_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < FRAME_WIDTH_BIT; w+=16)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 3:
			for(h = 0; h < bottomLength; h+=4)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&7) == 0)
				{
					if((h&0xf) == 0)
						dstUVOffset = h/16*FRAME_360P_WIDTH;
					else
						dstUVOffset += FRAME_360P_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfY = (4 - bottomLength&3)&3;
			bottomStartOfUV = (8 - (bottomLength&7))&7;
			if(bottomLength > 0)
			{
				bottomUvFlag = 1 -( ((bottomLength-1)/8)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < topLength; h+=4)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/4*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&7) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&0xf) == 0)
							dstUVOffset += FRAME_360P_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/16)*FRAME_360P_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&0xf) == 0)
							dstUVOffset = ((bottomLength+h)/16)*FRAME_360P_WIDTH;
						else
							dstUVOffset += FRAME_360P_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}	
			break;
		default:
			break;
	}
}

static void pr2100_display_action(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, DISPLAY_MODE mode)
{
	if(dst == NULL)
		return;

	pthread_mutex_lock(&pr2100Obj->displayMutex);
	switch(mode)
	{
		case FRONT_DISPLAY:
		case BACK_DISPLAY:
		case LEFT_DISPLAY:
		case RIGHT_DISPLAY:
			pr2100_display_one_channel_four_hd(frameInfo, pFrameInfo, dst, mode);
			break;			
		case QUART_DISPLAY:
			pr2100_yuv422_to_quart_yv12(frameInfo, pFrameInfo, dst, FRONT_DISPLAY);
			pr2100_yuv422_to_quart_yv12(frameInfo, pFrameInfo, dst, BACK_DISPLAY);
			pr2100_yuv422_to_quart_yv12(frameInfo, pFrameInfo, dst, LEFT_DISPLAY);
			pr2100_yuv422_to_quart_yv12(frameInfo, pFrameInfo, dst, RIGHT_DISPLAY);
			break;
		default:
			break;
	}
	pthread_mutex_unlock(&pr2100Obj->displayMutex);
}

void display_data_copy(unsigned char* dst, unsigned int width, unsigned int height)
{
	//if(curFrameBuffer == NULL)
	//	return;

	if(pr2100Obj == NULL || pr2100Obj->display == NULL)
		return;

	if(pr2100Obj->displayMode >= DISABLE_DISPLAY && pr2100Obj->displayMode < FOUR_DISPLAY)
	{
		pthread_mutex_lock(&pr2100Obj->displayMutex);
		 if(width == 1280 && height == 720)
			memcpy(dst, pr2100Obj->display,FRAME_YUV420_SIZE);
		else if(pr2100Obj->displaySize == DUAL_FHD_FRAME_YUV420_SIZE)
			mdp_resize_yv12(pr2100Obj->display, 1920, 1080, dst, width, height);
		else
			mdp_resize_yv12(pr2100Obj->display, 1280, 720, dst, width, height);
		pthread_mutex_unlock(&pr2100Obj->displayMutex);
	}
}

extern struct WWC2_FOUR_RECORD_THREAD *frontThread;
extern struct WWC2_FOUR_RECORD_THREAD *backThread;
extern struct WWC2_FOUR_RECORD_THREAD *leftThread;
extern struct WWC2_FOUR_RECORD_THREAD *rightThread;

static void pr2100_record_action(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, RECORD_MODE mode)
{
	if(mode == FOUR_RECORD)
	{
		if(frontThread->data == NULL || backThread->data == NULL || leftThread->data == NULL || rightThread->data == NULL)
			return;

		if(sem_trywait(&frontThread->sem.recordWriteSem) == 0)
		{
			pr2100_record_one_of_four_channel_qhd(frameInfo, pFrameInfo, frontThread->data, FRONT_RECORD);
			sem_post(&frontThread->sem.recordReadSem);
		}
		if(sem_trywait(&backThread->sem.recordWriteSem) == 0)
		{
			pr2100_record_one_of_four_channel_qhd(frameInfo, pFrameInfo, backThread->data, BACK_RECORD);
			sem_post(&backThread->sem.recordReadSem);
		}
		if(sem_trywait(&leftThread->sem.recordWriteSem) == 0)
		{
			pr2100_record_one_of_four_channel_qhd(frameInfo, pFrameInfo, leftThread->data, LEFT_RECORD);
			sem_post(&leftThread->sem.recordReadSem);
		}
		if(sem_trywait(&rightThread->sem.recordWriteSem) == 0)
		{
			pr2100_record_one_of_four_channel_qhd(frameInfo, pFrameInfo, rightThread->data, RIGHT_RECORD);
			sem_post(&rightThread->sem.recordReadSem);
		}

		return;
	}

	if(dst == NULL)
		return;

	if(sem_trywait(&recordWriteSem) == 0)
	{
		switch(mode)
		{
			case FRONT_RECORD:
			case BACK_RECORD:
			case LEFT_RECORD:
			case RIGHT_RECORD:
				pr2100_record_one_of_four_channel_hd(frameInfo, pFrameInfo, dst, mode);
				break;
			case QUART_RECORD:
				pr2100_yuv422_to_quart_yuv420(frameInfo, pFrameInfo,dst,FRONT_RECORD);
				pr2100_yuv422_to_quart_yuv420(frameInfo, pFrameInfo,dst,BACK_RECORD);
				pr2100_yuv422_to_quart_yuv420(frameInfo, pFrameInfo,dst,LEFT_RECORD);
				pr2100_yuv422_to_quart_yuv420(frameInfo, pFrameInfo,dst,RIGHT_RECORD);
				break;
			default:
				break;
		}
		sem_post(&recordReadSem);
	}
}

static void pr2100_captrue_action(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, CAPTURE_MODE mode)
{
	if(dst == NULL)
		return;

	if(sem_trywait(&captureWriteSem) == 0)
	{
		switch(mode)
		{
			case FRONT_CAPTURE:
			case BACK_CAPTURE:
			case LEFT_CAPTURE:
			case RIGHT_CAPTURE:
				pr2100_capture_one_of_four_channel_hd(frameInfo, pFrameInfo, dst, mode);
				break;
			case QUART_CAPTURE:
				pr2100_yuv422_to_quart_nv21(frameInfo, pFrameInfo,dst,FRONT_CAPTURE);
				pr2100_yuv422_to_quart_nv21(frameInfo, pFrameInfo,dst,BACK_CAPTURE);
				pr2100_yuv422_to_quart_nv21(frameInfo, pFrameInfo,dst,LEFT_CAPTURE);
				pr2100_yuv422_to_quart_nv21(frameInfo, pFrameInfo,dst,RIGHT_CAPTURE);
				break;
			case FOUR_CAPTURE:
				pr2100_capture_one_channel_four_hd(frameInfo, pFrameInfo, dst, FRONT_CAPTURE);
				pr2100_capture_one_channel_four_hd(frameInfo, pFrameInfo, dst, BACK_CAPTURE);
				pr2100_capture_one_channel_four_hd(frameInfo, pFrameInfo, dst, LEFT_CAPTURE);
				pr2100_capture_one_channel_four_hd(frameInfo, pFrameInfo, dst, RIGHT_CAPTURE);
				break;
			default:
				break;
		}
		sem_post(&captureReadSem);
	}
}

static void pr2100_h264yuv_action(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, unsigned char *dst, H264_MODE mode)
{
	unsigned char *writeFlag = NULL;
	char data = 1;

	if(dst == NULL)
		return;

	writeFlag = dst + FRAME_H264_SIZE;

	if(*writeFlag == 0)
	{
		switch(mode)
		{
			case FRONT_H264:
			case BACK_H264:
			case LEFT_H264:
			case RIGHT_H264:
				pr2100_360p_scale_nv21(frameInfo, pFrameInfo,dst,mode);
				*writeFlag = 1;
				break;
			case QUART_H264:
				pr2100_360p_scale_quart_nv21(frameInfo, pFrameInfo,dst,FRONT_H264);
				pr2100_360p_scale_quart_nv21(frameInfo, pFrameInfo,dst,BACK_H264);
				pr2100_360p_scale_quart_nv21(frameInfo, pFrameInfo,dst,LEFT_H264);
				pr2100_360p_scale_quart_nv21(frameInfo, pFrameInfo,dst,RIGHT_H264);
				*writeFlag = 1;
				break;
			case FOUR_H264:
				break;
			default:
				break;
		}
	}

	if(pr2100Obj->h264YuvFd > 0)
		write(pr2100Obj->h264YuvFd, &data, 1);
}

extern struct WWC2_H264_STREAM_THREAD *frontH264Stream;
extern struct WWC2_H264_STREAM_THREAD *backH264Stream;
extern struct WWC2_H264_STREAM_THREAD *leftH264Stream;
extern struct WWC2_H264_STREAM_THREAD *rightH264Stream;
static void pr2100_h264stream_action(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo)
{
	if(frontH264Stream->yuvData != NULL && sem_trywait(&frontH264Stream->yuvWriteSem) == 0)
	{
		frontH264Stream->yuvWriteFlag = true;
		pr2100_360p_scale_yuv420(frameInfo, pFrameInfo, frontH264Stream->yuvData, FRONT_H264);
		frontH264Stream->yuvWriteFlag = false;
		sem_post(&frontH264Stream->yuvReadSem);		
	}
	if(backH264Stream->yuvData != NULL && sem_trywait(&backH264Stream->yuvWriteSem) == 0)
	{
		backH264Stream->yuvWriteFlag = true;
		pr2100_360p_scale_yuv420(frameInfo, pFrameInfo, backH264Stream->yuvData, BACK_H264);
		backH264Stream->yuvWriteFlag = false;
		sem_post(&backH264Stream->yuvReadSem);
	}
	if(leftH264Stream->yuvData != NULL && sem_trywait(&leftH264Stream->yuvWriteSem) == 0)
	{
		leftH264Stream->yuvWriteFlag = true;
		pr2100_360p_scale_yuv420(frameInfo, pFrameInfo, leftH264Stream->yuvData, LEFT_H264);
		leftH264Stream->yuvWriteFlag = false;
		sem_post(&leftH264Stream->yuvReadSem);
	}
	if(rightH264Stream->yuvData != NULL && sem_trywait(&rightH264Stream->yuvWriteSem) == 0)
	{
		rightH264Stream->yuvWriteFlag = true;
		pr2100_360p_scale_yuv420(frameInfo, pFrameInfo, rightH264Stream->yuvData, RIGHT_H264);
		rightH264Stream->yuvWriteFlag = false;
		sem_post(&rightH264Stream->yuvReadSem);
	}
}

extern struct WWC2_PR2100_AVM_DATA *avmData;
static void pr2100_avm_action(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo)
{
	unsigned char *ch0Flag = NULL;
	unsigned char *ch1Flag = NULL;
	unsigned char *ch2Flag = NULL;
	unsigned char *ch3Flag = NULL;
	char data = 1;

	if(avmData == NULL)
		return;

	ch0Flag = avmData->ch0_avm_buff + FRAME_YUV420_SIZE;
	ch1Flag = avmData->ch1_avm_buff + FRAME_YUV420_SIZE;
	ch2Flag = avmData->ch2_avm_buff + FRAME_YUV420_SIZE;
	ch3Flag = avmData->ch3_avm_buff + FRAME_YUV420_SIZE;

	if(*ch0Flag == 0)
	{
		pr2100_display_one_channel_four_hd(frameInfo, pFrameInfo, avmData->ch0_avm_buff, FRONT_DISPLAY);
		*ch0Flag = 1;
	}

	if(*ch1Flag == 0)
	{
		pr2100_display_one_channel_four_hd(frameInfo, pFrameInfo, avmData->ch1_avm_buff, BACK_DISPLAY);
		*ch1Flag = 1;
	}

	if(*ch2Flag == 0)
	{
		pr2100_display_one_channel_four_hd(frameInfo, pFrameInfo, avmData->ch2_avm_buff, LEFT_DISPLAY);
		*ch2Flag = 1;
	}

	if(*ch3Flag == 0)
	{
		pr2100_display_one_channel_four_hd(frameInfo, pFrameInfo, avmData->ch3_avm_buff, RIGHT_DISPLAY);
		*ch3Flag = 1;
	}

	write(signalFd, &data, 1);
}

static void pr2100_combine_four_hd(unsigned char *src, const unsigned int width, const unsigned int height)
{
	unsigned char* pTempBuffer = preFrameBuffer;
	struct FOUR_CHANNEL_FRAME_INFO* pTempInfo = preFrameInfo;

	if(height == 1 || width != PR2100_FRAME_WIDTH)
		return;

	if(pr2100Obj == NULL)
		return;
	//ALOGD("bbl--pr2100_combine start");
	preFrameBuffer = curFrameBuffer;
	curFrameBuffer = pTempBuffer;
	preFrameInfo = curFrameInfo;
	curFrameInfo = pTempInfo;
	curFrameBuffer = src;
	collect_four_channel_frame_info(curFrameBuffer, curFrameInfo);

	if(preFrameBuffer == NULL || curFrameBuffer == NULL)
		return;

	pr2100_water_mark_custom(curFrameInfo, preFrameInfo, pr2100Obj->cardData, pr2100Obj->gpsData);

	pr2100_display_action(curFrameInfo, preFrameInfo, pr2100Obj->display, get_camera_display_mode());
	pr2100_record_action(curFrameInfo, preFrameInfo, pr2100Obj->record, get_camera_record_mode());
	pr2100_captrue_action(curFrameInfo, preFrameInfo, pr2100Obj->capture, get_camera_capture_mode());
	pr2100_h264yuv_action(curFrameInfo, preFrameInfo, pr2100Obj->h264Yuv, get_camera_h264_mode());
	pr2100_h264stream_action(curFrameInfo, preFrameInfo);

	pr2100_avm_action(curFrameInfo, preFrameInfo);
	//ALOGD("bbl--pr2100_combine end");
}

void pr2100_combine(unsigned char *src, const unsigned int width, const unsigned int height)
{
	switch(cameraFormat)
	{
		case FOUR_HD:
			pr2100_combine_four_hd(src, width, height);
			break;
		case FOUR_FHD:
			break;
		case CH0HD_CH1HD:
			pr2100_combine_dual_hd(src, width, height);
			break;
		case CH0FHD_CH1FHD:
			pr2100_combine_dual_fhd(src, width, height);
			break;
		default:
			break;
	}
}

void pr2100_combine_init(void)
{
	cameraFormat = get_camera_format();
	unsigned int frameInfoSize = 0;

	if(pr2100Init == false)
	{
		switch(cameraFormat)
		{
			case FOUR_HD:
				frameInfoSize = sizeof(struct FOUR_CHANNEL_FRAME_INFO);

				preFrameInfo = (struct FOUR_CHANNEL_FRAME_INFO *)pr2100_alloc_memory(frameInfoSize);
				curFrameInfo = (struct FOUR_CHANNEL_FRAME_INFO *)pr2100_alloc_memory(frameInfoSize);
				pr2100_record_init();

				preFrameBuffer = NULL;
				curFrameBuffer = NULL;
				break;
			case FOUR_FHD:
				break;
			case CH0HD_CH1HD:
				frameInfoSize = sizeof(struct DUAL_HD_FRAME_INFO);

				dualHdPreFrameInfo = (struct DUAL_HD_FRAME_INFO *)pr2100_alloc_memory(frameInfoSize);
				dualHdCurFrameInfo = (struct DUAL_HD_FRAME_INFO *)pr2100_alloc_memory(frameInfoSize);
				pr2100_dual_hd_record_init();

				dualHdPreFrameBuffer = NULL;
				dualHdCurFrameBuffer = NULL;
				dual_display_is_black = true;
				break;
			case CH0FHD_CH1FHD:
				frameInfoSize = sizeof(struct DUAL_FHD_FRAME_INFO);

				dualFhdPreFrameInfo = (struct DUAL_FHD_FRAME_INFO *)pr2100_alloc_memory(frameInfoSize);
				dualFhdCurFrameInfo = (struct DUAL_FHD_FRAME_INFO *)pr2100_alloc_memory(frameInfoSize);
				pr2100_dual_fhd_record_init();

				dualFhdPreFrameBuffer = NULL;
				dualFhdCurFrameBuffer = NULL;
				dual_display_is_black = true;
				break;
			default:
				break;
		}

		pr2100Init = true;
	}
	ALOGD("bbl--pr2100 infoSize = %d, cameraFormat = %d", frameInfoSize,cameraFormat);
}

void pr2100_combine_uninit(void)
{
	switch(cameraFormat)
	{
		case FOUR_HD:
			if(preFrameInfo)
			{
				free(preFrameInfo);
				preFrameInfo = NULL;
			}

			if(curFrameInfo)
			{
				free(curFrameInfo);
				curFrameInfo = NULL;
			}

			pr2100_record_uninit();
			break;
		case FOUR_FHD:
			break;
		case CH0HD_CH1HD:
			if(dualHdPreFrameInfo)
			{
				free(dualHdPreFrameInfo);
				dualHdPreFrameInfo = NULL;
			}

			if(dualHdCurFrameInfo)
			{
				free(dualHdCurFrameInfo);
				dualHdCurFrameInfo = NULL;
			}

			pr2100_dual_hd_record_uninit();
			break;
		case CH0FHD_CH1FHD:
			if(dualFhdPreFrameBuffer)
			{
				free(dualFhdPreFrameBuffer);
				dualFhdPreFrameBuffer = NULL;
			}

			if(dualFhdCurFrameBuffer)
			{
				free(dualFhdCurFrameBuffer);
				dualFhdCurFrameBuffer = NULL;
			}
			pr2100_dual_fhd_record_uninit();
			break;
		default:
			break;
	}
	pr2100Init = false;
}

#ifdef __cplusplus
}
#endif

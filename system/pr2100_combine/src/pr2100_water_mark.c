#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include "waterMaskData.h"
#include "card_data.h"
#include "pr2100_combine.h"

static const unsigned char waterMark_20x32[][20*32] = {ZERO_20X32, ONE_20X32, TWO_20X32, THREE_20X32, FOUR_20X32, FIVE_20X32,\
									SIX_20X32, SEVEN_20X32, EIGHT_20X32, NINE_20X32, SLASH_20X32, COLON_20X32};

static const unsigned char waterMarkFBLR_40x40[][40*40] = {FRONT_40X40, BACK_40X40, LEFT_40X40, RIGHT_40X40};

static const unsigned char card_data_20x32[][20*32] = {\
	CP_0, CP_1, CP_2, CP_3, CP_4, CP_5, CP_6, CP_7, CP_8, CP_9,\
	CP_A, CP_B, CP_C, CP_D, CP_E, CP_F, CP_G, CP_H, CP_I, CP_J,\
	CP_K, CP_L, CP_M, CP_N, CP_O, CP_P, CP_Q, CP_R, CP_S, CP_T,\
	CP_U, CP_V, CP_W, CP_X, CP_Y, CP_Z, CP_DIAN,\
	CP_JING, CP_JIN, CP_YU, CP_HU, CP_YI, CP_JIN2, CP_LIAO, CP_JI, CP_HEI, CP_SU,\
	CP_ZHE, CP_WAN, CP_MIN, CP_GAN, CP_LU, CP_YU2, CP_E2, CP_XIANG, CP_YUE, CP_QIONG,\
	CP_CHUAN, CP_GUI, CP_YUN, CP_SHAN, CP_GAN2, CP_QING, CP_MENG, CP_GUI2, CP_NING, CP_XIN,\
	CP_ZANG, CP_SHI, CP_LING, CP_JING2, CP_XUE, CP_GANG, CP_AO,\
};

static const unsigned char gps_data_20x32[][20*32] = {CP_0, CP_1, CP_2, CP_3, CP_4, CP_5, CP_6, CP_7, CP_8, CP_9, GPS_DIAN,\
												CP_N, CP_S, CP_W, CP_E};

static void getTimeString(char* mTime)
{
	struct tm *p = NULL;
	time_t timer = time(NULL);
	p = localtime(&timer);
	strftime(mTime,20,"%F  %T",p);
}

static void pr2100_water_mark(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, DISPLAY_MODE mode, struct WATER_MARK water)
{
	struct PART_FRAME_INFO *topPartInfo = NULL;
	struct PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *dstAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	unsigned int w,h = 0;
	int flip = 0;

	const struct WATER_MARK_POSITION position = water.position;
	const unsigned char *source = water.data;
	const unsigned int sourceWidth = water.width;
	const unsigned int sourceHeight = water.height;

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
			if(topLength > (position.y+sourceHeight))
			{
				for(h = 0; h < sourceHeight; h++)
				{
					dstAddr = topStartAddr + topPartInfo->offset[position.y+h];
					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  position.x*2 + 1 + w*2) = 0xff;
					}
				}
			}
			else if(topLength <= position.y)
			{
				for(h = 0; h < sourceHeight; h++)
				{
					dstAddr =  bottomStartAddr + bottomPartInfo->offset[position.y+h-topLength];
					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  position.x*2 + 1 + w*2) = 0xff;
					}
				}
			}
			else
			{
				for(h = 0; h < sourceHeight; h++)
				{
					if(topLength > (position.y+h))
						dstAddr = topStartAddr + topPartInfo->offset[position.y+h];
					else
						dstAddr =  bottomStartAddr + bottomPartInfo->offset[position.y+h-topLength];

					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  position.x*2 + 1 + w*2) = 0xff;
					}
				}
			}
			break;
		case 1:
			if(topLength > (position.y+sourceHeight))
			{
				for(h = 0; h < sourceHeight; h++)
				{
					dstAddr = topStartAddr + topPartInfo->offset[position.y+h];
					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  (FRAME_WIDTH-position.x)*2 + 1 - w*2) = 0xff;
					}
				}
			}
			else if(topLength <= position.y)
			{
				for(h = 0; h < sourceHeight; h++)
				{
					dstAddr =  bottomStartAddr + bottomPartInfo->offset[position.y+h-topLength];
					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  (FRAME_WIDTH-position.x)*2 + 1 - w*2) = 0xff;
					}
				}
			}
			else
			{
				for(h = 0; h < sourceHeight; h++)
				{
					if(topLength > (position.y+h))
						dstAddr = topStartAddr + topPartInfo->offset[position.y+h];
					else
						dstAddr =  bottomStartAddr + bottomPartInfo->offset[position.y+h-topLength];

					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  (FRAME_WIDTH-position.x)*2 + 1 - w*2) = 0xff;
					}
				}
			}
			break;
		case 2:
			if(bottomLength > (position.y+sourceHeight))
			{
				for(h = 0; h < sourceHeight; h++)
				{
					dstAddr = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-(position.y+h)];
					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  position.x*2 + 1 + w*2) = 0xff;
					}
				}
			}
			else if(bottomLength <= position.y)
			{
				for(h = 0; h < sourceHeight; h++)
				{
					dstAddr =  topStartAddr + topPartInfo->offset[topLength-1-(position.y+h-bottomLength)];
					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  position.x*2 + 1 + w*2) = 0xff;
					}
				}
			}
			else
			{
				for(h = 0; h < sourceHeight; h++)
				{
					if(bottomLength > (position.y+h))
						dstAddr = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-(position.y+h)];
					else
						dstAddr =  topStartAddr + topPartInfo->offset[topLength-1-(position.y+h-bottomLength)];

					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  position.x*2 + 1 + w*2) = 0xff;
					}
				}
			}
			break;
		case 3:
			if(bottomLength > (position.y+sourceHeight))
			{
				for(h = 0; h < sourceHeight; h++)
				{
					dstAddr = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-(position.y+h)];
					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  (FRAME_WIDTH-position.x)*2 + 1 - w*2) = 0xff;
					}
				}
			}
			else if(bottomLength <= position.y)
			{
				for(h = 0; h < sourceHeight; h++)
				{
					dstAddr =  topStartAddr + topPartInfo->offset[topLength-1-(position.y+h-bottomLength)];
					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  (FRAME_WIDTH-position.x)*2 + 1 - w*2) = 0xff;
					}
				}
			}
			else
			{
				for(h = 0; h < sourceHeight; h++)
				{
					if(bottomLength > (position.y+h))
						dstAddr = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-(position.y+h)];
					else
						dstAddr =  topStartAddr + topPartInfo->offset[topLength-1-(position.y+h-bottomLength)];

					for(w = 0; w < sourceWidth; w++)
					{
						if(*(source + sourceWidth*h + w) < 128)
							*(dstAddr +  (FRAME_WIDTH-position.x)*2 + 1 - w*2) = 0xff;
					}
				}
			}
			break;
		default:
			break;
	}
}


static void pr2100_time_water_mark(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, DISPLAY_MODE mode, char *timeData)
{
	unsigned int i = 0;
	struct WATER_MARK water = {.width = 20, .height = 32, .position = {.x = 40, .y = 640}};

	for(i = 0; i < 20; i++)
	{
		switch(timeData[i])
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
				water.data = waterMark_20x32[timeData[i] - '0'];
				break;
			case '-':
				water.data = waterMark_20x32[10];
				break;
			case ':':
				water.data = waterMark_20x32[11];
				break;
			case ' ':
				water.data = NULL;
				break;
			default:
				break;
		}
		water.position.x += water.width;
		if(water.data)
		{
			pr2100_water_mark(frameInfo,pFrameInfo, mode, water);
		}
	}
}

static void pr2100_card_water_mark(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, DISPLAY_MODE mode, int cardData[10])
{
	unsigned int i = 0;
	struct WATER_MARK water = {.width = 20, .height = 32, .position = {.x = 40, .y = 40}};

	for(i = 0; i < 10; i++)
	{
		if(cardData[i] <= 0 || cardData[i] > 73)
			return;

		water.data = card_data_20x32[cardData[i]];
		water.position.x += water.width;
		if(water.data)
		{
			pr2100_water_mark(frameInfo,pFrameInfo, mode, water);
		}
	}
}

static void pr2100_gps_water_mark(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, DISPLAY_MODE mode, char gps[32])
{
	unsigned int i = 0;
	struct WATER_MARK water = {.width = 20, .height = 32, .position = {.x = 800, .y = 40}};
	unsigned int len = (unsigned int)strlen(gps);

	for(i = 0; i < len; i++)
	{
		switch(gps[i])
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
				water.data = gps_data_20x32[gps[i] - '0'];
				break;
			case '.':
				water.data = gps_data_20x32[10];
				break;
			case 'N':
				water.data = gps_data_20x32[11];
				break;
			case 'S':
				water.data = gps_data_20x32[12];
				break;
			case 'W':
				water.data = gps_data_20x32[13];
				break;
			case 'E':
				water.data = gps_data_20x32[14];
				break;
			case ',':
				water.data = NULL;
				break;
			default:
				break;
		}
		water.position.x += water.width;
		if(water.data)
		{
			pr2100_water_mark(frameInfo,pFrameInfo, mode, water);
		}

	}

}

static void pr2100_water_mark_channel(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo)
{
	struct WATER_MARK ch0Water = {.data = waterMarkFBLR_40x40[0], .width = 40, .height = 40, .position = {.x = 1200, .y = 640}};
	struct WATER_MARK ch1Water = {.data = waterMarkFBLR_40x40[1], .width = 40, .height = 40, .position = {.x = 1200, .y = 640}};
	struct WATER_MARK ch2Water = {.data = waterMarkFBLR_40x40[2], .width = 40, .height = 40, .position = {.x = 1200, .y = 640}};
	struct WATER_MARK ch3Water = {.data = waterMarkFBLR_40x40[3], .width = 40, .height = 40, .position = {.x = 1200, .y = 640}};

	if( pr2100Obj->channelWaterMark)
	{
		pr2100_water_mark(frameInfo, pFrameInfo, FRONT_DISPLAY, ch0Water);
		pr2100_water_mark(frameInfo, pFrameInfo, BACK_DISPLAY, ch1Water);
		pr2100_water_mark(frameInfo, pFrameInfo, LEFT_DISPLAY, ch2Water);
		pr2100_water_mark(frameInfo, pFrameInfo, RIGHT_DISPLAY, ch3Water);
	}
}

static void pr2100_water_mark_time(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo)
{
	char timeData[21] = {0};

	if( pr2100Obj->timeWaterMark)
	{
		getTimeString(timeData);
		pr2100_time_water_mark(frameInfo, pFrameInfo, FRONT_DISPLAY, timeData);
		pr2100_time_water_mark(frameInfo, pFrameInfo, BACK_DISPLAY, timeData);
		pr2100_time_water_mark(frameInfo, pFrameInfo, LEFT_DISPLAY, timeData);
		pr2100_time_water_mark(frameInfo, pFrameInfo, RIGHT_DISPLAY, timeData);
	}
}

static void pr2100_water_mark_card(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, int cardData[10])
{
	if(pr2100Obj->cardWaterMark)
	{
		pr2100_card_water_mark(frameInfo, pFrameInfo, FRONT_DISPLAY, cardData);
		pr2100_card_water_mark(frameInfo, pFrameInfo, BACK_DISPLAY, cardData);
		pr2100_card_water_mark(frameInfo, pFrameInfo, LEFT_DISPLAY, cardData);
		pr2100_card_water_mark(frameInfo, pFrameInfo, RIGHT_DISPLAY, cardData);
	}
}

static void pr2100_water_mark_gps(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, char gps[32])
{
	if(pr2100Obj->gpsWaterMark)
	{
		pr2100_gps_water_mark(frameInfo, pFrameInfo, FRONT_DISPLAY, gps);
		pr2100_gps_water_mark(frameInfo, pFrameInfo, BACK_DISPLAY, gps);
		pr2100_gps_water_mark(frameInfo, pFrameInfo, LEFT_DISPLAY, gps);
		pr2100_gps_water_mark(frameInfo, pFrameInfo, RIGHT_DISPLAY, gps);
	}
}

void pr2100_water_mark_custom(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, int cardData[10], char gps[32])
{
	pr2100_water_mark_channel(frameInfo, pFrameInfo);
	pr2100_water_mark_time(frameInfo, pFrameInfo);
	pr2100_water_mark_card(frameInfo, pFrameInfo,cardData);
	pr2100_water_mark_gps(frameInfo, pFrameInfo,gps);
}

#ifdef __cplusplus
}
#endif

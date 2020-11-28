#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include "pr2100_combine.h"
#include "wwc2_pr2100_capture.h"
#include "wwc2_pr2100_record.h"

struct DUAL_HD_FRAME_INFO *dualHdPreFrameInfo = NULL;
struct DUAL_HD_FRAME_INFO *dualHdCurFrameInfo = NULL;

unsigned char *dualHdPreFrameBuffer = NULL;
unsigned char *dualHdCurFrameBuffer = NULL;

static void get_head_addr_dual_hd(struct PR2100_DUAL_HD_ADDR *pChAddr, unsigned char *src, DUAL_HD_CH_ORDER order)
{
	switch(order)
	{
		case CH_HD_01:
			pChAddr->ch0Header = src;
			pChAddr->ch1Header = src+4;

			pChAddr->ch0Addr = src+8;
			pChAddr->ch1Addr = src+8+DUAL_HD_FRAME_WIDTH_BIT;
			break;
		case CH_HD_10:
			pChAddr->ch0Header = src+4;
			pChAddr->ch1Header = src;

			pChAddr->ch0Addr = src+8+DUAL_HD_FRAME_WIDTH_BIT;
			pChAddr->ch1Addr = src+8;
			break;
		default:
			pChAddr->ch0Header = src;
			pChAddr->ch1Header = src+4;

			pChAddr->ch0Addr = src+8;
			pChAddr->ch1Addr = src+8+DUAL_HD_FRAME_WIDTH_BIT;
			break;
	}
}

static void collect_hd_frame_info(unsigned char *headAddr, struct HD_FRAME_INFO *info)
{
	struct PR2100_HEAD_INFO *head = NULL;

	unsigned int curPartFrameNum = 0xff;
	unsigned int curLength = 0;
	unsigned int nextLength = 0;
	unsigned int h = 0;
	unsigned int curPartPreLineNum = 0xffff;
	unsigned int nextPartPreLineNum = 0xffff;

	for(h = 0; h < PR2100_DUAL_HD_FRAME_HEIGHT; h++)
	{
		head = (struct PR2100_HEAD_INFO *)(headAddr+h*PR2100_DUAL_HD_FRAME_WIDTH_BIT);
		if(head->LINE_VALID && head->CH_VACT)
		{
			curPartFrameNum = head->FRM_NUM;
			break;
		}
	}

	if(curPartFrameNum == 0xff)
		return;

	for(h = 0; h < PR2100_DUAL_HD_FRAME_HEIGHT; h++)
	{
		head = (struct PR2100_HEAD_INFO *)(headAddr+h*PR2100_DUAL_HD_FRAME_WIDTH_BIT);

		if(head->LINE_VALID && head->CH_VACT)
		{
			if(head->FRM_NUM == curPartFrameNum)
			{
				if(curPartPreLineNum != head->VALID_LINE_NUM)
					info->curPart.offset[curLength++] = h*PR2100_DUAL_HD_FRAME_WIDTH_BIT;

				curPartPreLineNum = head->VALID_LINE_NUM;
			}
			else
			{
				if(nextPartPreLineNum != head->VALID_LINE_NUM)
					info->nextPart.offset[nextLength++] = h*PR2100_DUAL_HD_FRAME_WIDTH_BIT;

				nextPartPreLineNum = head->VALID_LINE_NUM;
			}
		}

		if(curLength == DUAL_HD_FRAME_HEIGHT || nextLength == DUAL_HD_FRAME_HEIGHT)
			break;
	}

	info->curPart.length = curLength;
	info->nextPart.length = nextLength;
}

static void collect_dual_hd_frame_info(unsigned char *startAddr, struct DUAL_HD_FRAME_INFO *info)
{
	struct PR2100_DUAL_HD_ADDR pChAddr;
	struct HD_FRAME_INFO *ch0Info = &(info->ch0FrameInfo);
	struct HD_FRAME_INFO *ch1Info = &(info->ch1FrameInfo);

	get_head_addr_dual_hd(&pChAddr, startAddr, (DUAL_HD_CH_ORDER)pr2100Obj->chOrder);
	collect_hd_frame_info(pChAddr.ch0Header, ch0Info);
	collect_hd_frame_info(pChAddr.ch1Header, ch1Info);

	ch0Info->chAddr = pChAddr.ch0Addr;
	ch1Info->chAddr = pChAddr.ch1Addr;
}


static void pr2100_dual_hd_display_one(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst, DISPLAY_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *v = dst+DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT;
	unsigned char *u = v+DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/4;	
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
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
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
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
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

static void pr2100_dual_hd_yuv422_to_quart_yv12(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst,DISPLAY_MODE mode)
{
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
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
			y = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/4;
			v = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/16;
			u = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT*5/4 + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/16;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_DISPLAY:
			y = dst + DUAL_HD_FRAME_WIDTH/2 + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/4;
			v = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT + DUAL_HD_FRAME_WIDTH/4 + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/16;
			u = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/4*5+DUAL_HD_FRAME_WIDTH/4 + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/16;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/2*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
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
				bottomUvFlag = 1 -( ((topLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < bottomLength; h+=2)
			{
				src =bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/8)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((topLength+h)/8)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
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
				dstYOffset = h/2*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
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
				dstYOffset = (topLength+h)/2*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/8)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((topLength+h)/8)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
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
				dstYOffset = h/2*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
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
				bottomUvFlag = 1 -( ((bottomLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src =topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/2*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/8)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((bottomLength+h)/8)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
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
				dstYOffset = h/2*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					if((h&7) == 0)
						dstUVOffset = h/8*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
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
				bottomUvFlag = 1 -( ((bottomLength-1)/4)&1);
			}
			else
				bottomUvFlag = 0;

			for(h = bottomStartOfY; h < topLength; h+=2)
			{
				src =topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)/2*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/8)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&7) == 0)
							dstUVOffset = ((bottomLength+h)/8)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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

static void pr2100_dual_hd_yuv422_to_half_yv12(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst,DISPLAY_MODE mode)
{
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
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

	unsigned int bottomStartOfUV = 0;
	unsigned int bottomUvFlag = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_DISPLAY:
			y = dst;
			v = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT;
			u = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT*5/4;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_DISPLAY:
			y = dst + DUAL_HD_FRAME_WIDTH/2;
			v = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT + DUAL_HD_FRAME_WIDTH/4;
			u = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/4*5+DUAL_HD_FRAME_WIDTH/4;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&1) == 0)
				{
					if((h&3) == 0)
						dstUVOffset = h/4*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfUV = topLength&1;
			bottomUvFlag = bottomStartOfUV;

			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&1) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/4)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset = ((topLength+h)/4)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 1:
			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&1) == 0)
				{
					if((h&3) == 0)
						dstUVOffset = h/4*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfUV = topLength&1;
			bottomUvFlag = bottomStartOfUV;

			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&1) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/4)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset = ((topLength+h)/4)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
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
			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&1) == 0)
				{
					if((h&3) == 0)
						dstUVOffset = h/4*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfUV = bottomLength&1;
			bottomUvFlag = bottomStartOfUV;

			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&1) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/4)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset = ((bottomLength+h)/4)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 3:
			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&1) == 0)
				{
					if((h&3) == 0)
						dstUVOffset = h/4*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfUV = bottomLength&1;
			bottomUvFlag = bottomStartOfUV;

			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&1) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/4)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset = ((bottomLength+h)/4)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;	
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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

static void pr2100_dual_hd_capture_one(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst, CAPTURE_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *uv = dst+DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT;
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
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
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
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+indexUV++) = *(srcAddr+w+2);
						*(uv+indexUV++) = *(srcAddr+w);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
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

static void pr2100_dual_hd_yuv422_to_half_nv21(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst,CAPTURE_MODE mode)
{
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
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
	int flip = 0;

	switch(mode)
	{
		case FRONT_CAPTURE:
			y = dst;
			uv = y + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_CAPTURE:
			y = dst + DUAL_HD_FRAME_WIDTH/2;
			uv = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT + DUAL_HD_FRAME_WIDTH/2;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&1) == 0)
				{
					dstUVOffset = h/2*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(((h+topLength)&1) == 0)
				{
					dstUVOffset = (topLength+h)/2*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}
			break;
		case 1:
			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&1) == 0)
				{
					dstUVOffset = h/2*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(((h+topLength)&1) == 0)
				{
					dstUVOffset = (topLength+h)/2*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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
			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h/DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&1) == 0)
				{
					dstUVOffset = h/2*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					dstUVOffset = (bottomLength+h)/2*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}
				}
			}
			break;
		case 3:
			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&1) == 0)
				{
					dstUVOffset = h/2*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					{
						*(uv+dstUVOffset+indexUV++) = *(src+w+2);
						*(uv+dstUVOffset+indexUV++) = *(src+w);
					}

					*(uv+dstUVOffset+indexUV++) = *(src+2);
					*(uv+dstUVOffset+indexUV++) = *(src);
				}
			}

			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(((h+bottomLength)&1) == 0)
				{
					dstUVOffset = (bottomLength+h)/2*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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

static void pr2100_dual_hd_capture_two(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst, CAPTURE_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
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
			uv = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT*2;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_CAPTURE:
			y = dst + DUAL_HD_FRAME_WIDTH;
			uv = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT*2 + DUAL_HD_FRAME_WIDTH;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				dstYOffset = h * DUAL_HD_FRAME_WIDTH*2;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					dstUVOffset = h*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength + h) * DUAL_HD_FRAME_WIDTH*2;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				if(((h+topLength)&1) == 0)
				{
					dstUVOffset = (topLength+h)/2*DUAL_HD_FRAME_WIDTH*2;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
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
				dstYOffset = h * DUAL_HD_FRAME_WIDTH*2;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				*(y+dstYOffset+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					dstUVOffset = h*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
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
				dstYOffset = (topLength + h) * DUAL_HD_FRAME_WIDTH*2;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				*(y+dstYOffset+indexY++) = *(srcAddr+1);

				if(((h+topLength)&1) == 0)
				{
					dstUVOffset = (topLength+h)/2*DUAL_HD_FRAME_WIDTH*2;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
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
				dstYOffset = h * DUAL_HD_FRAME_WIDTH*2;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					dstUVOffset = h*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					{
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w+2);
						*(uv+dstUVOffset+indexUV++) = *(srcAddr+w);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength + h) * DUAL_HD_FRAME_WIDTH*2;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					dstUVOffset = (bottomLength+h)/2*DUAL_HD_FRAME_WIDTH*2;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
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
				dstYOffset = h * DUAL_HD_FRAME_WIDTH*2;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				*(y+dstYOffset+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					dstUVOffset = h*DUAL_HD_FRAME_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
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
				dstYOffset = (bottomLength + h) * DUAL_HD_FRAME_WIDTH*2;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+dstYOffset+indexY++) = *(srcAddr+w+1);

				*(y+dstYOffset+indexY++) = *(srcAddr+1);

				if(((h+bottomLength)&1) == 0)
				{
					dstUVOffset = (bottomLength+h)/2*DUAL_HD_FRAME_WIDTH*2;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
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

static void pr2100_dual_hd_record_one(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst, RECORD_MODE mode)
{
	unsigned char *srcAddr = NULL;
	unsigned int topLength = 0;
	unsigned int bottomLength = 0;
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *u = dst+DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT;
	unsigned char *v = u+DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/4;	
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
		default:
			break;
	}

	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;
	
	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr+topPartInfo->offset[h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}

			for(h = 0; h < bottomLength; h++)
			{
				srcAddr = bottomStartAddr + bottomPartInfo->offset[h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+topLength)&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
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
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if((h&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					{
						*(u+indexU++) = *(srcAddr+w);
						*(v+indexV++) = *(srcAddr+w+2);
					}
				}
			}

			for(h = 0; h < topLength; h++)
			{
				srcAddr = topStartAddr + topPartInfo->offset[topLength-1-h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=2)
					*(y+indexY++) = *(srcAddr+w+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if((h&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-2; w != 0 ; w-=2)
					*(y+indexY++) = *(srcAddr+w+1);

				*(y+indexY++) = *(srcAddr+1);

				if(((h+bottomLength)&1) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
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

static void pr2100_dual_hd_yuv422_to_half_yuv420(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst,RECORD_MODE mode)
{
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
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

	unsigned int bottomStartOfUV = 0;
	unsigned int bottomUvFlag = 0;
	int flip = 0;

	switch(mode)
	{
		case FRONT_RECORD:
			y = dst;
			u = y + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT;
			v = u + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/4;
			topPartInfo = &(pFrameInfo->ch0FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch0FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch0FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch0FrameInfo.chAddr;
			flip = frameFlip.ch0FrameFlip;
			break;
		case BACK_RECORD:
			y = dst + DUAL_HD_FRAME_WIDTH/2;
			u = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT + DUAL_HD_FRAME_WIDTH/4;
			v = dst + DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT/4*5+DUAL_HD_FRAME_WIDTH/4;
			topPartInfo = &(pFrameInfo->ch1FrameInfo.nextPart);
			bottomPartInfo = &(frameInfo->ch1FrameInfo.curPart);
			topStartAddr = pFrameInfo->ch1FrameInfo.chAddr;
			bottomStartAddr = frameInfo->ch1FrameInfo.chAddr;
			flip = frameFlip.ch1FrameFlip;
			break;
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&1) == 0)
				{
					if((h&3) == 0)
						dstUVOffset = h/4*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;	
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfUV = topLength&1;
			bottomUvFlag = bottomStartOfUV;

			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&1) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/4)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset = ((topLength+h)/4)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 1:
			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&1) == 0)
				{
					if((h&3) == 0)
						dstUVOffset = h/4*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfUV = topLength&1;
			bottomUvFlag = bottomStartOfUV;

			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&1) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((topLength+h)/4)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset = ((topLength+h)/4)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
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
			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&1) == 0)
				{
					if((h&3) == 0)
						dstUVOffset = h/4*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}

			bottomStartOfUV = bottomLength&1;
			bottomUvFlag = bottomStartOfUV;

			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&1) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/4)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset = ((bottomLength+h)/4)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}
				}
			}	
			break;
		case 3:
			for(h = 0; h < bottomLength; h++)
			{
				src = bottomStartAddr + bottomPartInfo->offset[bottomLength-1-h];
				dstYOffset = h*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&1) == 0)
				{
					if((h&3) == 0)
						dstUVOffset = h/4*DUAL_HD_FRAME_WIDTH;
					else
						dstUVOffset += DUAL_HD_FRAME_WIDTH/2;

					indexU = 0;
					indexV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0; w-=8)
					{
						*(u+dstUVOffset+indexU++) = *(src+w);
						*(v+dstUVOffset+indexV++) = *(src+w+2);
					}

					*(u+dstUVOffset+indexU++) = *(src);
					*(v+dstUVOffset+indexV++) = *(src+2);
				}
			}

			bottomStartOfUV = bottomLength&1;
			bottomUvFlag = bottomStartOfUV;

			for(h = 0; h < topLength; h++)
			{
				src = topStartAddr + topPartInfo->offset[topLength-1-h];
				dstYOffset = (bottomLength+h)*DUAL_HD_FRAME_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&1) == 0)
				{
					if(bottomUvFlag)
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
						else
							dstUVOffset = ((bottomLength+h)/4)*DUAL_HD_FRAME_WIDTH;
					}
					else
					{
						if(((h-bottomStartOfUV)&3) == 0)
							dstUVOffset = ((bottomLength+h)/4)*DUAL_HD_FRAME_WIDTH;
						else
							dstUVOffset += DUAL_HD_FRAME_WIDTH/2;
					}

					indexU = 0;
					indexV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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

static void pr2100_dual_hd_360p_scale_nv21(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst,H264_MODE mode)
{
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
	unsigned char *topStartAddr = NULL;
	unsigned char *bottomStartAddr = NULL;
	unsigned char *y = dst;
	unsigned char *uv = y + FRAME_360P_WIDTH*FRAME_360P_HEIGHT;
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
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
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
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
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
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=4)
					*(y+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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
				for(w = DUAL_HD_FRAME_WIDTH_BIT-4; w != 0 ; w-=4)
					*(y+indexY++) = *(src+w+1);

				*(y+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
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

static void pr2100_dual_hd_360p_scale_half_nv21(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst,H264_MODE mode)
{
	struct HD_PART_FRAME_INFO *topPartInfo = NULL;
	struct HD_PART_FRAME_INFO *bottomPartInfo = NULL;
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
		default:
			break;
	}
	topLength = topPartInfo->length;
	bottomLength = bottomPartInfo->length;
	if((topLength+bottomLength) > DUAL_HD_FRAME_HEIGHT)
		topLength = DUAL_HD_FRAME_HEIGHT - bottomLength;

	switch(flip)
	{
		case 0:
			for(h = 0; h < topLength; h+=2)
			{
				src = topStartAddr + topPartInfo->offset[h];
				dstYOffset = h/2*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					dstUVOffset = h/4*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=16)
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
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					dstUVOffset = (topLength+h)/4*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=16)
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
				dstYOffset = h/2*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					dstUVOffset = h/4*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
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
				src = bottomStartAddr + bottomPartInfo->offset[h];
				dstYOffset = (topLength+h)/2*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					dstUVOffset = (topLength+h)/4*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
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
				dstYOffset = h/2*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if((h&3) == 0)
				{
					dstUVOffset = h/4*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=16)
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
				dstYOffset = (bottomLength+h)/2*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					dstUVOffset = (bottomLength+h)/4*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = 0; w < DUAL_HD_FRAME_WIDTH_BIT; w+=16)
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
				dstYOffset = h/2*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if((h&3) == 0)
				{
					dstUVOffset = h/4*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
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
				dstYOffset = (bottomLength+h)/2*FRAME_360P_WIDTH;
				indexY = 0;
				for(w = DUAL_HD_FRAME_WIDTH_BIT-8; w != 0 ; w-=8)
					*(y+dstYOffset+indexY++) = *(src+w+1);

				*(y+dstYOffset+indexY++) = *(src+1);

				if(h >= bottomStartOfUV && ((h-bottomStartOfUV)&3) == 0)
				{
					dstUVOffset = (bottomLength+h)/4*FRAME_360P_WIDTH;
					indexUV = 0;
					for(w = DUAL_HD_FRAME_WIDTH_BIT-16; w != 0 ; w-=16)
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

static void pr2100_dual_hd_set_black_display(unsigned char *dst)
{
	unsigned int i = 0;
	unsigned int ySize = DUAL_HD_FRAME_WIDTH * DUAL_HD_FRAME_HEIGHT;
	unsigned int uvSize = DUAL_HD_FRAME_WIDTH * DUAL_HD_FRAME_HEIGHT/2;
	unsigned char *yAddr = dst;
	unsigned char *uvAddr = yAddr + ySize;

	for(i = 0; i < ySize; i++)
		*(yAddr + i) = 0x10;

	for(i = 0; i < uvSize; i++)
		*(uvAddr + i) = 0x80;
}

static void pr2100_dual_hd_display_action(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst, DISPLAY_MODE mode)
{
	if(dst == NULL)
		return;

	pthread_mutex_lock(&pr2100Obj->displayMutex);
	switch(mode)
	{
		case FRONT_DISPLAY:
		case BACK_DISPLAY:
			pr2100_dual_hd_display_one(frameInfo, pFrameInfo, dst, mode);
			dual_display_is_black = true;
			break;			
		case DUAL_DISPLAY:
			if(pr2100Obj->displayType == 1)
			{
				if(dual_display_is_black)
					pr2100_dual_hd_set_black_display(dst);
				pr2100_dual_hd_yuv422_to_quart_yv12(frameInfo, pFrameInfo, dst, FRONT_DISPLAY);
				pr2100_dual_hd_yuv422_to_quart_yv12(frameInfo, pFrameInfo, dst, BACK_DISPLAY);
				dual_display_is_black = false;			
			}
			else
			{
				pr2100_dual_hd_yuv422_to_half_yv12(frameInfo, pFrameInfo, dst, FRONT_DISPLAY);
				pr2100_dual_hd_yuv422_to_half_yv12(frameInfo, pFrameInfo, dst, BACK_DISPLAY);
			}
			break;
		default:
			break;
	}
	pthread_mutex_unlock(&pr2100Obj->displayMutex);
}

static void pr2100_dual_hd_captrue_action(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst, CAPTURE_MODE mode)
{
	if(dst == NULL)
		return;

	if(sem_trywait(&captureWriteSem) == 0)
	{
		switch(mode)
		{
			case FRONT_CAPTURE:
			case BACK_CAPTURE:
				pr2100_dual_hd_capture_one(frameInfo, pFrameInfo, dst, mode);
				break;
			case DUAL_CAPTURE:
				pr2100_dual_hd_yuv422_to_half_nv21(frameInfo, pFrameInfo,dst,FRONT_CAPTURE);
				pr2100_dual_hd_yuv422_to_half_nv21(frameInfo, pFrameInfo,dst,BACK_CAPTURE);
				break;
			case TWO_CAPTURE:
				pr2100_dual_hd_capture_two(frameInfo, pFrameInfo, dst, FRONT_CAPTURE);
				pr2100_dual_hd_capture_two(frameInfo, pFrameInfo, dst, BACK_CAPTURE);
				break;
			default:
				break;
		}
		sem_post(&captureReadSem);
	}
}

extern struct WWC2_FOUR_RECORD_THREAD *frontThread;
extern struct WWC2_FOUR_RECORD_THREAD *backThread;
static void pr2100_dual_hd_record_action(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst, RECORD_MODE mode)
{
	if(mode == TWO_RECORD)
	{
		if(frontThread->data == NULL || backThread->data == NULL)
			return;

		if(sem_trywait(&frontThread->sem.recordWriteSem) == 0)
		{
			pr2100_dual_hd_record_one(frameInfo, pFrameInfo, frontThread->data, FRONT_RECORD);
			sem_post(&frontThread->sem.recordReadSem);
		}
		if(sem_trywait(&backThread->sem.recordWriteSem) == 0)
		{
			pr2100_dual_hd_record_one(frameInfo, pFrameInfo, backThread->data, BACK_RECORD);
			sem_post(&backThread->sem.recordReadSem);
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
				pr2100_dual_hd_record_one(frameInfo, pFrameInfo, dst, mode);
				break;
			case DUAL_RECORD:
				pr2100_dual_hd_yuv422_to_half_yuv420(frameInfo, pFrameInfo,dst,FRONT_RECORD);
				pr2100_dual_hd_yuv422_to_half_yuv420(frameInfo, pFrameInfo,dst,BACK_RECORD);
				break;
			default:
				break;
		}
		sem_post(&recordReadSem);
	}
}


static void pr2100_dual_hd_h264yuv_action(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, unsigned char *dst, H264_MODE mode)
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
				pr2100_dual_hd_360p_scale_nv21(frameInfo, pFrameInfo,dst,mode);
				*writeFlag = 1;
				break;
			case DUAL_H264:
				pr2100_dual_hd_360p_scale_half_nv21(frameInfo, pFrameInfo,dst,FRONT_H264);
				pr2100_dual_hd_360p_scale_half_nv21(frameInfo, pFrameInfo,dst,BACK_H264);
				*writeFlag = 1;
				break;
			default:
				break;
		}
	}

	if(pr2100Obj->h264YuvFd > 0)
		write(pr2100Obj->h264YuvFd, &data, 1);
}

void pr2100_combine_dual_hd(unsigned char *src, const unsigned int width, const unsigned int height)
{
	unsigned char* pTempBuffer = dualHdPreFrameBuffer;
	struct DUAL_HD_FRAME_INFO* pTempInfo = dualHdPreFrameInfo;

	if(height == 1 || width != PR2100_DUAL_HD_FRAME_WIDTH)
		return;

	if(pr2100Obj == NULL)
		return;
	//ALOGD("bbl--pr2100_combine_dual_hd start");
	dualHdPreFrameBuffer = dualHdCurFrameBuffer;
	dualHdCurFrameBuffer = pTempBuffer;
	dualHdPreFrameInfo = dualHdCurFrameInfo;
	dualHdCurFrameInfo = pTempInfo;
	dualHdCurFrameBuffer = src;
	collect_dual_hd_frame_info(dualHdCurFrameBuffer, dualHdCurFrameInfo);

	if(dualHdPreFrameBuffer == NULL || dualHdCurFrameBuffer == NULL)
		return;
	pr2100_dual_hd_water_mark_custom(dualHdCurFrameInfo, dualHdPreFrameInfo, pr2100Obj->cardData, pr2100Obj->gpsData);
	pr2100_dual_hd_display_action(dualHdCurFrameInfo, dualHdPreFrameInfo, pr2100Obj->display, get_camera_display_mode());
	pr2100_dual_hd_captrue_action(dualHdCurFrameInfo, dualHdPreFrameInfo, pr2100Obj->capture, get_camera_capture_mode());
	pr2100_dual_hd_record_action(dualHdCurFrameInfo, dualHdPreFrameInfo, pr2100Obj->record, get_camera_record_mode());
	pr2100_dual_hd_h264yuv_action(dualHdCurFrameInfo, dualHdPreFrameInfo, pr2100Obj->h264Yuv, get_camera_h264_mode());
	//ALOGD("bbl--pr2100_combine_dual_hd end");
}

#ifdef __cplusplus
}
#endif


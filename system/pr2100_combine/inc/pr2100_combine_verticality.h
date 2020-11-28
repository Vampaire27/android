#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h> 

#define PR2100_FRAME_WIDTH			(1284)	//1280 + 4
#define PR2100_FRAME_WIDTH_BIT		(2568)	//(PR2100_FRAME_WIDTH*2)
#define PR2100_FRAME_HEIGHT			(2992)	//(748*4)
#define PR2100_FRAME_SIZE			(7683456)	//(PR2100_FRAME_WIDTH*PR2100_FRAME_HEIGHT*2)


#define FRAME_WIDTH					(1280)
#define FRAME_WIDTH_BIT				(2560)	//(FRAME_WIDTH*2)
#define FRAME_HEIGHT				(720)
#define FRAME_SIZE					(1843200)	//(FRAME_WIDTH*FRAME_HEIGHT*2)
#define FRAME_YUV420_SIZE			(1382400)

struct PART_FRAME_INFO {
	unsigned int offset[FRAME_HEIGHT];
	unsigned int length;
};

struct CHANNEL_FRAME_INFO {
	struct PART_FRAME_INFO curPart;
	struct PART_FRAME_INFO nextPart;
};

struct FOUR_CHANNEL_FRAME_INFO {
	unsigned char *srcAddr;
	struct CHANNEL_FRAME_INFO ch0FrameInfo;
	struct CHANNEL_FRAME_INFO ch1FrameInfo;
	struct CHANNEL_FRAME_INFO ch2FrameInfo;
	struct CHANNEL_FRAME_INFO ch3FrameInfo;
};

struct TWO_CHANNEL_FRAME_INFO {
	unsigned char *srcAddr;
	struct CHANNEL_FRAME_INFO ch0FrameInfo;
	struct CHANNEL_FRAME_INFO ch1FrameInfo;
};


enum CAMERA_FORMAT{
	CH0HD_CH1HD,
	CH0HD_CH1FHD,
	CH0FHD_CH1HD,
	CH0FHD_CH1FHD,
	FOUR_HD,
	FOUR_FHD,
};

typedef enum WWC2_DISPLAY_MODE{
	DISABLE_DISPLAY = 0,
	FRONT_DISPLAY,
	BACK_DISPLAY,
	LEFT_DISPLAY,
	RIGHT_DISPLAY,
	QUART_DISPLAY,
	FOUR_DISPLAY,
	UNKNOW_DISPLAY
} DISPLAY_MODE;


struct PR2100_HEAD_INFO {
	unsigned VALID_LINE_NUM:11;//LSB
	unsigned LINE_VALID:1;
	unsigned FRM_NUM:3;
	unsigned FRM_VALID:1;
	unsigned RAW_LINE_NUM:11;
	unsigned CH_VACT:1;
	unsigned CH_FMT:2;
	unsigned CH_NUM:2;//MSB
};

struct PR2100_RECORD {
	unsigned char *record;
	unsigned char *capture;
	unsigned char *h264;

	unsigned int recordSize;
	unsigned int captureSize;
	unsigned int h264Size;

	int waterMask;
};

struct WATER_MASK_POSITION {
	unsigned int x;
	unsigned int y;
};

struct WATER_MASK {
	const unsigned char *data;
	unsigned int width;
	unsigned int height;
	struct WATER_MASK_POSITION position;
};

extern void pr2100_combine_init(void);
extern void pr2100_combine_uninit(void);
extern void pr2100_combine(unsigned char *src, const int width, const int height);
extern void get_camera_display_size(int *startX, int *startY, int *width, int *height);

extern void pr2100_record_init(void);
extern void pr2100_record_uninit(void);
extern void pr2100_water_mark_four_channel(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo);
extern void pr2100_time_water_four_channel(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo);
extern struct PR2100_RECORD *pr2100Obj;

#ifdef __cplusplus
}
#endif

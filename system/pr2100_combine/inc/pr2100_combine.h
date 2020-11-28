#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h> 
#include <semaphore.h>
#include <pthread.h>
#include <sys/ioctl.h>

#define PR2100_FRAME_WIDTH		(5128) //1280*4+8
#define PR2100_FRAME_WIDTH_BIT	(10256)//PR2100_FRAME_WDITH*2
#define PR2100_FRAME_HEIGHT		(748)
#define PR2100_FRAME_SIZE			(7671488)//PR2100_FRAME_WDITH*PR2100_FRAME_HEIGHT*2


#define FRAME_WIDTH				(1280)
#define FRAME_WIDTH_BIT			(2560)//(FRAME_WIDTH*2)
#define FRAME_HEIGHT				(720)
#define FRAME_SIZE					(1843200)//(FRAME_WIDTH*FRAME_HEIGHT*2)
#define FRAME_YUV420_SIZE			(1382400)//(FRAME_WIDTH*FRAME_HEIGHT*3/2)
#define FRAME_QHD_YUV420_SIZE		(777600)//(960*540*3/2)

#define FRAME_H264_SIZE			(345600)//FRAME_YUV420_SIZE/4
#define H264_STREAM_SIZE			(102400)// 25*4096

#define FRAME_360P_WIDTH	(640)
#define FRAME_360P_HEIGHT	(360)

#define PR2100_DUAL_HD_FRAME_WIDTH			(2564)//1280*2+4
#define PR2100_DUAL_HD_FRAME_WIDTH_BIT		(5128)//PR2100_DUAL_HD_FRAME_WIDTH*2
#define PR2100_DUAL_HD_FRAME_HEIGHT			(748)
#define PR2100_DUAL_HD_FRAME_SIZE				(3835744)//PR2100_DUAL_HD_FRAME_WIDTH*PR2100_DUAL_HD_FRAME_HEIGHT*2
#define DUAL_HD_FRAME_WIDTH					(1280)
#define DUAL_HD_FRAME_WIDTH_BIT				(2560)//(DUAL_HD_FRAME_WIDTH*2)
#define DUAL_HD_FRAME_HEIGHT					(720)
#define DUAL_HD_FRAME_SIZE						(1843200)//(DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT*2)
#define DUAL_HD_FRAME_YUV420_SIZE			(1382400)//(DUAL_HD_FRAME_WIDTH*DUAL_HD_FRAME_HEIGHT*3/2)

#define PR2100_DUAL_FHD_FRAME_WIDTH			(3844)//1920*2+4
#define PR2100_DUAL_FHD_FRAME_WIDTH_BIT		(7688)//PR2100_DUAL_FHD_FRAME_WIDTH*2
#define PR2100_DUAL_FHD_FRAME_HEIGHT			(1124)
#define PR2100_DUAL_FHD_FRAME_SIZE			(8641312)//PR2100_DUAL_FHD_FRAME_WIDTH*PR2100_DUAL_FHD_FRAME_HEIGHT*2
#define DUAL_FHD_FRAME_WIDTH					(1920)
#define DUAL_FHD_FRAME_WIDTH_BIT				(3840)//(DUAL_FHD_FRAME_WIDTH*2)
#define DUAL_FHD_FRAME_HEIGHT					(1080)
#define DUAL_FHD_FRAME_SIZE					(4147200)//(DUAL_FHD_FRAME_WIDTH*DUAL_FHD_FRAME_HEIGHT*2)
#define DUAL_FHD_FRAME_YUV420_SIZE			(3110400)//(DUAL_FHD_FRAME_WIDTH*DUAL_FHD_FRAME_WIDTH*3/2)


#define WWC2_MAGIC		'W'
#define WWC2_SIGIO_ACK		_IO(WWC2_MAGIC, 1)

struct PART_FRAME_INFO {
   unsigned int offset[FRAME_HEIGHT];
   unsigned int length;
};

struct CHANNEL_FRAME_INFO {
   unsigned char *chAddr;
   struct PART_FRAME_INFO curPart;
   struct PART_FRAME_INFO nextPart;
};

struct FOUR_CHANNEL_FRAME_INFO {
   struct CHANNEL_FRAME_INFO ch0FrameInfo;
   struct CHANNEL_FRAME_INFO ch1FrameInfo;
   struct CHANNEL_FRAME_INFO ch2FrameInfo;
   struct CHANNEL_FRAME_INFO ch3FrameInfo;
};

//dual HD
struct HD_PART_FRAME_INFO {
   unsigned int offset[DUAL_HD_FRAME_HEIGHT];
   unsigned int length;
};

struct HD_FRAME_INFO {
   unsigned char *chAddr;
   struct HD_PART_FRAME_INFO curPart;
   struct HD_PART_FRAME_INFO nextPart;
};

struct DUAL_HD_FRAME_INFO {
   struct HD_FRAME_INFO ch0FrameInfo;
   struct HD_FRAME_INFO ch1FrameInfo;
};


//dual DHD
struct FHD_PART_FRAME_INFO {
   unsigned int offset[DUAL_FHD_FRAME_HEIGHT];
   unsigned int length;
};

struct FHD_FRAME_INFO {
   unsigned char *chAddr;
   struct FHD_PART_FRAME_INFO curPart;
   struct FHD_PART_FRAME_INFO nextPart;
};

struct DUAL_FHD_FRAME_INFO {
   struct FHD_FRAME_INFO ch0FrameInfo;
   struct FHD_FRAME_INFO ch1FrameInfo;
};



struct FOUR_CHANNEL_FRAME_FLIP {
   int ch0FrameFlip;
   int ch1FrameFlip;
   int ch2FrameFlip;
   int ch3FrameFlip;
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
	DUAL_DISPLAY,
	FOUR_DISPLAY,
	UNKNOW_DISPLAY
} DISPLAY_MODE;

typedef enum WWC2_RECORD_MODE{
	DISABLE_RECORD = 0,
	FRONT_RECORD,
	BACK_RECORD,
	LEFT_RECORD,
	RIGHT_RECORD,
	QUART_RECORD,
	FOUR_RECORD,
	DUAL_RECORD,
	TWO_RECORD,
	UNKNOW_RECORD,
	START_RECORD = 10,
	STOP_RECORD
}RECORD_MODE;

typedef enum WWC2_RECORD_STATUS{
	CAMERA_OPEN_STATUS = 0,
	START_RECORD_STATUS,
	RUNNING_RECORD_STATUS,
	STOP_SUCCESS_RECORD_STATUS,
	STOP_FAIL_RECORD_STATUS,
	CAMERA_CLOSE_STATUS,
	UNKNOW_RECORD_STATUS
}RECORD_STATUS;

typedef enum WWC2_CAPTURE_MODE{
	DISABLE_CAPTURE = 0,
	FRONT_CAPTURE,
	BACK_CAPTURE,
	LEFT_CAPTURE,
	RIGHT_CAPTURE,
	QUART_CAPTURE,
	FOUR_CAPTURE,
	DUAL_CAPTURE,
	TWO_CAPTURE,
	UNKNOW_CAPTURE
}CAPTURE_MODE;

typedef enum WWC2_CAPTURE_STATUS{
	START_CAPTURE_STATUS = 1,
	RUNNING_CAPTURE_STATUS,
	STOP_SUCCESS_CAPTUR_STATUS,
	STOP_FAIL_CAPTURE_STATUS,
	UNKNOW_CAPTURE_STATUS
}CAPTURE_STATUS;

typedef enum WWC2_H264_MODE{
	DISABLE_H264 = 0,
	FRONT_H264,
	BACK_H264,
	LEFT_H264,
	RIGHT_H264,
	QUART_H264,
	FOUR_H264,
	DUAL_H264,
	UNKNOW_H264,
	START_H264 = 10,
	STOP_H264,
	FRONT_H264STREAM = 50,
	BACK_H264STREAM,
	LEFT_H264STREAM,
	RIGHT_H264STREAM,
	STOP_FRONT_H264STREAM = 100,
	STOP_BACK_H264STREAM,
	STOP_LEFT_H264STREAM,
	STOP_RIGHT_H264STREAM,
}H264_MODE;

typedef enum WWC2_H264_STATUS{
	START_H264_STATUS = 1,
	RUNNING_H264_STATUS,
	STOP_SUCCESS_H264_STATUS,
	STOP_FAIL_H264_STATUS,
	UNKNOW_H264_STATUS
}H264_STATUS;

typedef enum WWC2_SAVE_FILE_DIR {
	DIR_UNKOWN = 0,
	DIR_LOCAL,
	DIR_TFCARD,
	DIR_GPSCARD,
	DIR_USB0,
	DIR_USB1,
	DIR_USB2,
	DIR_USB3,
	DIR_LOCAL_USB0 = 14,
	DIR_LOCAL_USB1,
	DIR_LOCAL_USB2,
	DIR_LOCAL_USB3,
	DIR_LOCAL_TFCARD
}SAVE_FILE_DIR;


typedef enum WWC2_CHANNEL_ORDER_ENUM {
	CH_0123 = 0,
	CH_0132,
	CH_0213,
	CH_0231,
	CH_0312,
	CH_0321,

	CH_1023,//6
	CH_1032,
	CH_1203,
	CH_1230,
	CH_1302,
	CH_1320,

	CH_2013,//12
	CH_2031,
	CH_2103,
	CH_2130,
	CH_2301,
	CH_2310,

	CH_3012,//18
	CH_3021,
	CH_3102,
	CH_3120,
	CH_3201,
	CH_3210
}CH_ORDER;

typedef enum WWC2_DUAL_HD_ORDER_ENUM {
	CH_HD_01 = 0,
	CH_HD_10,
}DUAL_HD_CH_ORDER;

typedef enum WWC2_DUAL_FHD_ORDER_ENUM {
	CH_FHD_01 = 0,
	CH_FHD_10,
}DUAL_FHD_CH_ORDER;

enum WWC2_CAMERA_MODE {
	WWC2_DISPLAY = 0,
	WWC2_RECORD,
	WWC2_CAPTURE,
	WWC2_H264,
	WWC2_UNKNOW,
	WWC2_CHANNELWATERMARK = 10,
	WWC2_TIMEWATERMARK,
	WWC2_GPSWATERMARK,
	WWC2_CARDWATERMARK,
	WWC2_AUDIOENABLE,
	WWC2_RECORD_TIMEOUT,
	WWC2_CH0_FLIP,
	WWC2_CH1_FLIP,
	WWC2_CH2_FLIP,
	WWC2_CH3_FLIP,
	WWC2_RECORD_BPS, //20
	WWC2_RECORD_DIR,
	WWC2_CAPTURE_DIR,
	WWC2_CHANNEL_ORDER,
};

struct WWC2_CAMERA_ACTION{
	int mode;
	int act;
};

struct PR2100_HEAD_INFO {
	unsigned int VALID_LINE_NUM:11;//LSB 0..10
	unsigned int LINE_VALID:1;// 11
	unsigned int FRM_NUM:3; // 12..14
	unsigned int FRM_VALID:1;// 15
	unsigned int RAW_LINE_NUM:11;// 16..26
	unsigned int CH_VACT:1;// 27
	unsigned int CH_FMT:2;// 28..29
	unsigned int CH_NUM:2;//MSB 30..31
};

struct PR2100_THREAD {
	bool threadLoopFlag;
	pthread_t thread;
	sem_t sem;
};

struct WWC2_H264_STREAM_THREAD {
	bool threadLoopFlag;
	unsigned char *streamData;
	unsigned char *yuvData;
	const char *threadName;
	const char *syncDevName;
	const char *shareStreamFileName;
	H264_MODE	mode;
	int syncFd;
	bool yuvWriteFlag;
	pthread_t thread;
	sem_t threadSem;
	sem_t yuvWriteSem;
	sem_t yuvReadSem;
	sem_t stopSem;
};

struct PR2100_RECORD {
	unsigned char *display;
	unsigned char *record;
	unsigned char *capture;
	unsigned char *h264Yuv;

	unsigned int displaySize;
	unsigned int recordSize;
	unsigned int captureSize;
	unsigned int h264YuvSize;

	DISPLAY_MODE displayMode;
	RECORD_MODE recordMode;
	CAPTURE_MODE captureMode;
	H264_MODE h264Mode;
	int channelWaterMark;
	int timeWaterMark;
	int gpsWaterMark;
	int cardWaterMark;
	int audioEnable;
	int ch0Filp;
	int ch1Filp;
	int ch2Filp;
	int ch3Filp;
	int record_bps;
	SAVE_FILE_DIR record_dir;
	SAVE_FILE_DIR capture_dir;
	CH_ORDER chOrder;
	int cardData[10];
	char gpsData[32];

	pthread_mutex_t displayMutex;
	struct PR2100_THREAD captureThread;
	struct PR2100_THREAD recordThread;
	struct PR2100_THREAD h264YuvThread;

	int recordStopFlag;
	int statusReportFlag;
	int h264YuvFd;
	enum CAMERA_FORMAT cameraFormat;
	int displayType;
};

struct WATER_MARK_POSITION {
	unsigned int x;
	unsigned int y;
};

struct WATER_MARK {
	const unsigned char *data;
	unsigned int width;
	unsigned int height;
	struct WATER_MARK_POSITION position;
};

struct WWC2_RECORD_SEM{
	sem_t recordWriteSem;
	sem_t recordReadSem;
	sem_t recordStopSem;
};

struct WWC2_FOUR_RECORD_THREAD {
	unsigned char *data;
	const char *threadName;
	RECORD_MODE mode;
	int recordStopFlag;
	int statusReportFlag;
	struct WWC2_RECORD_SEM sem;
	struct PR2100_THREAD pThread;
};

struct WWC2_PR2100_AVM_DATA {
	unsigned char *ch0_avm_buff;
	unsigned char *ch1_avm_buff;
	unsigned char *ch2_avm_buff;
	unsigned char *ch3_avm_buff;
};

struct PR2100_CHANNLE_ADDR {
	unsigned char *ch0Header;
	unsigned char *ch1Header;
	unsigned char *ch2Header;
	unsigned char *ch3Header;

	unsigned char *ch0Addr;
	unsigned char *ch1Addr;
	unsigned char *ch2Addr;
	unsigned char *ch3Addr;
};

struct PR2100_DUAL_HD_ADDR {
	unsigned char *ch0Header;
	unsigned char *ch1Header;

	unsigned char *ch0Addr;
	unsigned char *ch1Addr;
};

struct PR2100_DUAL_FHD_ADDR {
	unsigned char *ch0Header;
	unsigned char *ch1Header;

	unsigned char *ch0Addr;
	unsigned char *ch1Addr;
};

extern void pr2100_combine_init(void);
extern void pr2100_combine_uninit(void);
extern void pr2100_combine(unsigned char *src, const unsigned int width, const unsigned int height);
extern void get_camera_display_size(unsigned int *startX, unsigned int *startY, unsigned int *width, unsigned int *height);
extern void pr2100_water_mark_custom(struct FOUR_CHANNEL_FRAME_INFO *frameInfo, struct FOUR_CHANNEL_FRAME_INFO *pFrameInfo, int cardData[10], char gps[32]);
extern void pr2100_record_init(void);
extern void pr2100_record_uninit(void);
extern DISPLAY_MODE get_camera_display_mode(void);
extern CAPTURE_MODE get_camera_capture_mode(void);
extern RECORD_MODE get_camera_record_mode(void);
extern H264_MODE get_camera_h264_mode(void);
extern void display_data_copy(unsigned char* dst, unsigned int width, unsigned int height);
extern void pr2100_flip_set(int channelId, int flip);
extern struct PR2100_RECORD *pr2100Obj;
extern struct FOUR_CHANNEL_FRAME_FLIP frameFlip;
extern int signalFd;

extern struct DUAL_HD_FRAME_INFO *dualHdPreFrameInfo;
extern struct DUAL_HD_FRAME_INFO *dualHdCurFrameInfo;
extern unsigned char *dualHdPreFrameBuffer;
extern unsigned char *dualHdCurFrameBuffer;
extern void pr2100_dual_hd_record_init(void);
extern void pr2100_dual_hd_record_uninit(void);
extern void pr2100_combine_dual_hd(unsigned char *src, const unsigned int width, const unsigned int height);
extern void pr2100_dual_hd_water_mark_custom(struct DUAL_HD_FRAME_INFO *frameInfo, struct DUAL_HD_FRAME_INFO *pFrameInfo, int cardData[10], char gps[32]);


extern struct DUAL_FHD_FRAME_INFO *dualFhdPreFrameInfo;
extern struct DUAL_FHD_FRAME_INFO *dualFhdCurFrameInfo;
extern unsigned char *dualFhdPreFrameBuffer;
extern unsigned char *dualFhdCurFrameBuffer;
extern void pr2100_dual_fhd_record_init(void);
extern void pr2100_dual_fhd_record_uninit(void);
extern void pr2100_combine_dual_fhd(unsigned char *src, const unsigned int width, const unsigned int height);
extern void pr2100_dual_fhd_water_mark_custom(struct DUAL_FHD_FRAME_INFO *frameInfo, struct DUAL_FHD_FRAME_INFO *pFrameInfo, int cardData[10], char gps[32]);

extern bool dual_display_is_black;

#ifdef __cplusplus
}
#endif

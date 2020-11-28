#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/input.h>

#define WWC2_CAMERA_COMBINE_NAME	"wwc2_camera_combine"
#define WWC2_MAGIC		'W'
#define WWC2_SIGIO_ACK		_IO(WWC2_MAGIC, 1)

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
	DUAL_ERCORD,
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
	UNKOW_CAPTURE,
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
	WWC2_RECORD_BPS,//20
	WWC2_RECORD_DIR,
	WWC2_CAPTURE_DIR,
	WWC2_CHANNEL_ORDER
};

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

struct WWC2_CAMERA_ACTION{
	int mode;
	int act;
};

struct WWC2_FILE_NOTIFY {
	wait_queue_head_t queue;
	int state;
	char file_name[128];
};

struct WWC2_AVM_NOTIFY {
	wait_queue_head_t queue;
	int state;
	int data;
};

struct WWC2_SIGIO_NOTIFY {
	wait_queue_head_t queue;
	int state;
};

struct WWC2_GPS_DATA {
	char longitude[16];
	char latitude[16];
};

struct WWC2_DEV_COMBINE{
	bool channel_water_mark;
	bool time_water_mark;
	bool gps_water_mark;
	bool card_water_mark;
	bool audio_enable;
	DISPLAY_MODE display_mode;
	RECORD_MODE record_mode;
	CAPTURE_MODE capture_mode;
	H264_MODE h264_mode;
	RECORD_STATUS record_status;
	RECORD_STATUS record_four_status;
	CAPTURE_STATUS capture_status;
	H264_STATUS h264_status;
	struct input_dev* input_record_status;
	int latency;
	int ch0_filp;
	int ch1_filp;
	int ch2_filp;
	int ch3_filp;
	int record_bps;
	SAVE_FILE_DIR record_dir;
	SAVE_FILE_DIR capture_dir;
	CH_ORDER ch_order;
	int card_data[10];
	struct WWC2_GPS_DATA gps_data;
	struct mutex action_lock;
	struct mutex input_lock;
	struct delayed_work record_work;
	struct workqueue_struct *record_work_queue;
	long record_start_time_ms;
	struct WWC2_FILE_NOTIFY capture_file;
	struct WWC2_AVM_NOTIFY avm_notify;
	struct WWC2_SIGIO_NOTIFY sigio_notify;
};

struct wwc2_point_info {
	int status;
	int x;
	int y;
	int id;
};

enum WWC2_POINT_STATUS {
	BTN_TOUCH_UP = 0,
	ONE_POINT_DOWN = 1,
	TWO_POINT_DOWN = 2
};

enum WWC2_TOUCH_TYPE {
	TOUCH_TYPE_TOUCHSCREEN = 0x00,
	TOUCH_TYPE_TOUCHBOARD = 0x01
};

extern void wwc2_tpd_report(struct wwc2_point_info *info);

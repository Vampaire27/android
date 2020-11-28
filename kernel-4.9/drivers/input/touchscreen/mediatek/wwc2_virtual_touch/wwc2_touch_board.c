#include <linux/input.h>

#define CENTER_POINT_X			512
#define CENTER_POINT_Y			512
#define TOUCH_LIMIT_RADIUS		400


static int down_clear = 1;
static long down_ms = 0L;

static int distance_x = 0;
static int distance_y = 0;

static int touch_borad_data_check(int x, int y, int id)
{
	int xd = abs(x-CENTER_POINT_X);
	int yd = abs(y-CENTER_POINT_Y);

	if(id != 0)
		return -1;

	if(xd*xd+yd*yd > TOUCH_LIMIT_RADIUS*TOUCH_LIMIT_RADIUS)
		return -2;

	return 0;
}

void touch_board_init(struct input_dev *dev)
{
	dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
	dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) | BIT_MASK(BTN_EXTRA);
	dev->relbit[0] |= BIT_MASK(REL_WHEEL);
}

void touch_board_down(struct input_dev *dev, int x, int y, int id)
{
	static int pre_down_x = 0;
	static int pre_down_y = 0;
	static int first_down_x = 0;
	static int first_down_y = 0;
	int tmp_x = x;
	int tmp_y = y;
	struct timeval now;
	int ret = touch_borad_data_check(x, y, id);

	if(ret)
		return;

	input_report_key(dev, BTN_EXTRA, 0);
	if(down_clear == 1)
	{
		input_report_rel(dev, REL_X, 0);
		input_report_rel(dev, REL_Y, 0);
		do_gettimeofday(&now);
		down_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
		first_down_x = x;
		first_down_y = y;
		distance_x = 0;
		distance_y = 0;
		down_clear = 0;
	}
	else
	{
		input_report_rel(dev, REL_X, (tmp_x-pre_down_x)/2);
		input_report_rel(dev, REL_Y, (tmp_y-pre_down_y)/3);
		distance_x = abs(x - first_down_x);
		distance_y = abs(y - first_down_y);

	}
	input_sync(dev);

	pre_down_x = tmp_x;
	pre_down_y = tmp_y;
}

void touch_board_up(struct input_dev *dev)
{
	struct timeval now;
	long up_ms = 0L;

	do_gettimeofday(&now);
	up_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
	if(up_ms - down_ms < 250 && distance_x < 20 && distance_y < 20)
	{
		input_report_key(dev, BTN_LEFT, 1);
		input_sync(dev);

		input_report_key(dev, BTN_LEFT, 0);
		input_sync(dev);
	}
	
	down_clear = 1;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cutils/log.h>
#include <cutils/properties.h>

static void shell_command(char *cmd1, char *cmd2, char *rData)
{
	FILE *fd = NULL;

	ALOGD("wwc2_shell %s",cmd1);
	fd = popen(cmd1, cmd2);
	if(fd == NULL)
		return;

	while(fgets(rData, 64, fd) != NULL)
	{
		ALOGD("wwc2_shell %s", rData);
	}

	pclose(fd);
}

static int get_user_command(void)
{
	int val = 0;
	char value[PROPERTY_VALUE_MAX] = {0};

	property_get("wwc2.camera.reset", value, "0");
	val = atoi(value);

	return val;
}

static int get_camera_work_status(void)
{
	char flag[8] = {'\0'};
	FILE *fd = NULL;
	int value = -1;

	fd = fopen("/sys/class/gpiodrv/gpio_ctrl/camera_work_status","r");
	if(fd)
	{
		fread(flag,1,8,fd);
		fclose(fd);
		value = atoi(flag);
	}
	return value;
}

int main(void)
{
	char buff[64];
	char cmd0[64] = "pgrep cameraserver";
	char cmd1[64] = {0};
	char cmd2[2] = "r";
	int reset = 0;
	int status = -1;

	while(1)
	{
		reset = get_user_command();
		if(reset == 1)
			break;

		sleep(1);
	}

	memset(buff, 0, sizeof(buff));

	shell_command(cmd0, cmd2, buff);
	sprintf(cmd1, "kill %s", buff);
	shell_command(cmd1, cmd2, buff);

	while(1)
	{
		if(get_camera_work_status() == 0)
			break;
		usleep(100*1000);
	}

	property_set("wwc2.camera.reset", "0");

	return 0;
}

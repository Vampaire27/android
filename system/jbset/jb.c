/* JB.C
 *
 *
 * Copyright 2018 by wwc2 Incorporated.
 *
 * HISTORY
 *  when        who     what, where, why
 *  --------    ---     ----------------------------------------------------------
 *  2018/03/30    HuangZeming  Initial Revision
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <errno.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#undef LOG_TAG
#define LOG_TAG "JBSET"

#define CUSTOM_DEV_NODE "/dev/block/platform/bootdevice/by-name/custom"
#define W_CUSTOM_OFFSET 254

#define SCREEN_SIZE 0
#define SCREEN_SIZE_OFFSET 253
 
#define VCOM 1
#define VCOM_OFFSET 254
 
#define LK_LOGO 2
#define LK_LOGO_OFFSET 250

#define AVDD 3
#define AVDD_OFFSET 252
 
int main(int argc, char** argv)
{
    int raw_type, raw_val;
    int fid = -1;
    ssize_t count = 0;
    char a;


    if(argc > 1){
        raw_type = atoi(argv[1]);
    }

    if(argc > 2){
        raw_val = atoi(argv[2]);
    }

    a = raw_val;

    ALOGD("jbset mark3 --- raw_type=%d raw_val=%d a=%d", raw_type, raw_val, a);

    /// write into custom.img offset 254
    fid = open(CUSTOM_DEV_NODE, O_RDWR|O_SYNC);
    if(fid < 0){
        ALOGD(" jbset  open Custom file fail! ");
	    printf("jbset can not open file %s\n", CUSTOM_DEV_NODE);
		goto bail;
     }

    /// screen size
    if(raw_type == SCREEN_SIZE) {
        lseek(fid, SCREEN_SIZE_OFFSET, SEEK_SET);
        count = write(fid, &a, 1);
    } 

    /// vcom
    if(raw_type == VCOM){
        lseek(fid, VCOM_OFFSET, SEEK_SET);
        count = write(fid, &a, 1);
    }

    /// lk logo index 
    if(raw_type == LK_LOGO){
        lseek(fid, LK_LOGO_OFFSET, SEEK_SET);
        count = write(fid, &a, 1);
    }

    /// avdd 
    if(raw_type == AVDD){
        lseek(fid, AVDD_OFFSET, SEEK_SET);
        count = write(fid, &a, 1);
    }
	
    printf("jbset write_index --- count=%d\n",count);
	if(count < 1){
	    printf("jbset write index fails\n");
		goto bail;
	} else {
        ALOGD(" jbset set success!  ");

    }

bail:
	if(fid > 0){
		close(fid);
	}
	     ALOGD("jbset  persist.jbset.running =false");
        property_set("persist.jbset.running", "false");
	return 0;
}

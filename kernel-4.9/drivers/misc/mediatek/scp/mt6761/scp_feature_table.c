/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/module.h>       /* needed by all modules */
#include "scp_feature_define.h"
#include "scp_ipi.h"
#define xstr(s) (# s)

/*scp feature list*/
struct scp_feature_tb feature_table[NUM_FEATURE_ID] = {
	{
		.feature     = VOW_FEATURE_ID,
		.freq        = 75,
		.enable      = 0,
		.sub_feature = 0,
	},
	{
		.feature     = OPEN_DSP_FEATURE_ID,
		.freq        = 356,
		.enable      = 0,
		.sub_feature = 0,
	},
	{
		.feature     = SENS_FEATURE_ID,
		.freq        = 0,
		.enable      = 0,
		.sub_feature = 1,
	},
	{
		.feature     = MP3_FEATURE_ID,
		.freq        = 47,
		.enable      = 0,
		.sub_feature = 0,
	},
	{
		.feature     = FLP_FEATURE_ID,
		.freq        = 26,
		.enable      = 0,
		.sub_feature = 0,
	},
	{
		.feature     = RTOS_FEATURE_ID,
		.freq        = 0,
		.enable      = 0,
		.sub_feature = 0,
	},
	{
		.feature     = SPEAKER_PROTECT_FEATURE_ID,
		.freq        = 200,
		.enable      = 0,
		.sub_feature = 0,
	},
	{
		.feature     = VCORE_TEST_FEATURE_ID,
		.freq        = 0,
		.enable      = 0,
		.sub_feature = 0,
	},
};


/*scp sensor type list*/
struct scp_sub_feature_tb sensor_type_table[NUM_SENSOR_TYPE] = {
	{
		.feature = ACCELEROMETER_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = MAGNETIC_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = ORIENTATION_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = GYROSCOPE_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = LIGHT_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = PROXIMITY_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = PRESSURE_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = STEP_COUNTER_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = SIGNIFICANT_MOTION_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = STEP_DETECTOR_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = GLANCE_GESTURE_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = ANSWER_CALL_FEATURE_ID,
		.freq    = 3,
		.enable  = 0,
	},
	{
		.feature = SHAKE_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = STATIONARY_DETECT_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = MOTION_DETECT_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = IN_POCKET_FEATURE_ID,
		.freq    = 3,
		.enable  = 0,
	},
	{
		.feature = SHAKE_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = DEVICE_ORIENTATION_FEATURE_ID,
		.freq    = 2,
		.enable  = 0,
	},
	{
		.feature = ACTIVITY_FEATURE_ID,
		.freq    = 3,
		.enable  = 0,
	},
	};

const char *ipi_id_names[] = {
	xstr(IPI_WDT),
	xstr(IPI_TEST1),
	xstr(IPI_LOGGER_ENABLE),
	xstr(IPI_LOGGER_WAKEUP),
	xstr(IPI_LOGGER_INIT_A),
	xstr(IPI_VOW),
	xstr(IPI_AUDIO),
	xstr(IPI_DVT_TEST),
	xstr(IPI_SENSOR),
	xstr(IPI_TIME_SYNC),
	xstr(IPI_SHF),
	xstr(IPI_CONSYS),
	xstr(IPI_SCP_A_READY),
	xstr(IPI_APCCCI),
	xstr(IPI_SCP_A_RAM_DUMP),
	xstr(IPI_DVFS_DEBUG),
	xstr(IPI_DVFS_FIX_OPP_SET),
	xstr(IPI_DVFS_FIX_OPP_EN),
	xstr(IPI_DVFS_LIMIT_OPP_SET),
	xstr(IPI_DVFS_LIMIT_OPP_EN),
	xstr(IPI_DVFS_DISABLE),
	xstr(IPI_DVFS_SLEEP),
	xstr(IPI_PMICW_MODE_DEBUG),
	xstr(IPI_DVFS_SET_FREQ),
	xstr(IPI_CHRE),
	xstr(IPI_CHREX),
	xstr(IPI_SCP_PLL_CTRL),
	xstr(IPI_DO_AP_MSG),
	xstr(IPI_DO_SCP_MSG),
	xstr(IPI_MET_SCP),
	xstr(IPI_SCP_TIMER),
	xstr(SCP_NR_IPI)
};

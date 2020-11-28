/*
* Copyright (C) 2018 MediaTek Inc.
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
#ifndef __MEMORY_SSMR_H__
#define __MEMORY_SSMR_H__

enum ssmr_feature_type {
	SSMR_FEAT_SVP,
#ifdef CONFIG_MTK_IRIS_SUPPORT
	SSMR_FEAT_IRIS,
#endif
#ifdef CONFIG_MTK_CAM_SECURITY_SUPPORT
	SSMR_FEAT_2D_FR,
#endif
#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	SSMR_FEAT_TUI,
#endif
#ifdef CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT
	SSMR_FEAT_WFD,
#endif
#ifdef CONFIG_MTK_PROT_MEM_SUPPORT
	SSMR_FEAT_PROT_SHAREDMEM,
#endif
	__MAX_NR_SSMR_FEATURES,
};

extern int ssmr_offline(phys_addr_t *pa, unsigned long *size, bool is_64bit,
		unsigned int feat);
extern int ssmr_online(unsigned int feat);


#endif
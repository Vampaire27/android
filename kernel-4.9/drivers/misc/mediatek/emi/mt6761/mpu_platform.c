/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <mt-plat/sync_write.h>
#include <mt-plat/mtk_io.h>
#ifdef CONFIG_MTK_AEE_FEATURE
#include <mt-plat/aee.h>
#endif

#include "mt_emi.h"

enum {
	MASTER_APM0 = 0,
	MASTER_APM1 = 1,
	MASTER_MM0 = 2,
	MASTER_MDMCU = 3,
	MASTER_MD = 4,
	MASTER_MM1 = 5,
	MASTER_GPU0_PERI = 6,
	MASTER_GPU1_LPDMA = 7,
	MASTER_ALL = 8
};

static void __iomem *infra_ao_base;
static void __iomem *cen_emi_base;

int is_md_master(unsigned int master_id)
{
	if ((master_id & 0x7) == MASTER_MDMCU)
		return 1;

	if ((master_id & 0x7) == MASTER_MD)
		return 1;

	return 0;
}

void set_ap_region_permission(unsigned int apc[EMI_MPU_DGROUP_NUM])
{
	SET_ACCESS_PERMISSION(apc, LOCK,
		FORBIDDEN, FORBIDDEN, FORBIDDEN, FORBIDDEN,
		FORBIDDEN, FORBIDDEN, NO_PROTECTION, NO_PROTECTION,
		FORBIDDEN, SEC_R_NSEC_RW, FORBIDDEN, NO_PROTECTION,
		FORBIDDEN, FORBIDDEN, FORBIDDEN, NO_PROTECTION);
}

void bypass_init(unsigned int *init_flag)
{
	infra_ao_base = ioremap(0x10001000, 0x1000);
	cen_emi_base = mt_cen_emi_base_get();

	*init_flag = 1;
}

int bypass_violation(unsigned int mpus, unsigned int *init_flag)
{
	unsigned int mput, mput_2nd;
	unsigned long long vio_addr;

	if (*init_flag == 0)
		return 0;

	if ((mpus & 0x0000FF1F) == 0x7) {
		mput = readl(IOMEM(cen_emi_base + 0x1F8));
		mput_2nd = readl(IOMEM(cen_emi_base + 0x1FC));
		vio_addr = ((((unsigned long long)(mput_2nd & 0xF)) << 32)
			+ mput + 0x40000000);

		aee_kernel_exception("EMI MPU",
			"%s%s=0x%x,%s=0x%x,%s=0x%x,%s=0x%llx\n"
			"%s=0x%x,%s=0x%x\n"
			"%s%s\n",
			"EMI MPU violation.\n",
			"S", mpus,
			"T", mput,
			"T2", mput_2nd,
			"addr", vio_addr,
			"0x10001380", readl(IOMEM(infra_ao_base + 0x380)),
			"0x10001388", readl(IOMEM(infra_ao_base + 0x388)),
			"CRDISPATCH_KEY:EMI MPU Violation Issue/",
			"MT6761_M7_AXI_MST_CONNSYS");

		return 1;
	}

	return 0;
}

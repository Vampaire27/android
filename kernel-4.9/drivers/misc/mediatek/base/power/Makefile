#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

obj-$(CONFIG_MACH_MT8173) += $(subst ",,$(CONFIG_MTK_PLATFORM))/
obj-$(CONFIG_MACH_MT6758) += spm/
obj-$(CONFIG_MACH_MT6765) += spm/
obj-$(CONFIG_MACH_MT6761) += spm/
obj-$(CONFIG_MACH_MT6758) += leakage_table_v2/
obj-$(CONFIG_MACH_MT6765) += leakage_table_v2/
obj-$(CONFIG_MACH_MT6761) += leakage_table_v2/
obj-$(CONFIG_MACH_MT6758) += cpufreq_v1/
obj-$(CONFIG_MACH_MT6765) += cpufreq_v1/
obj-$(CONFIG_MACH_MT6761) += cpufreq_v1/
ifneq ($(CONFIG_FPGA_EARLY_PORTING),y)
obj-$(CONFIG_MACH_MT6758) += eem_v2/
obj-$(CONFIG_MACH_MT6765) += eem_v2/
obj-$(CONFIG_MACH_MT6761) += eem_v2/
endif
obj-$(CONFIG_MACH_MT6761) += upower_v2/
ifeq ($(CONFIG_MTK_UNIFY_POWER), y)
obj-$(CONFIG_MACH_MT6758) += upower_v2/
obj-$(CONFIG_MACH_MT6765) += upower_v2/
obj-$(CONFIG_MACH_MT6761) += upower_v2/
endif
obj-$(CONFIG_MACH_MT6761) += ppm_v3/
obj-$(CONFIG_MACH_MT6758) += ppm_v3/
obj-$(CONFIG_MACH_MT6765) += ppm_v3/
obj-$(CONFIG_MACH_MT6758) += dcm_v1/
obj-$(CONFIG_MACH_MT6761) += dcm_v1/
obj-$(CONFIG_MACH_MT6765) += dcm_v1/
ifneq ($(CONFIG_FPGA_EARLY_PORTING),y)
obj-$(CONFIG_MACH_MT6758) += clkbuf_v1/
obj-$(CONFIG_MACH_MT6765) += clkbuf_v1/
obj-$(CONFIG_MACH_MT6761) += clkbuf_v1/
obj-$(CONFIG_MACH_MT6765) += $(subst ",,$(CONFIG_MTK_PLATFORM))/
obj-$(CONFIG_MACH_MT6761) += $(subst ",,$(CONFIG_MTK_PLATFORM))/
endif
obj-$(CONFIG_MACH_MT6758) += pbm_v2/
obj-$(CONFIG_MACH_MT6765) += pbm_v4/
obj-$(CONFIG_MACH_MT6761) += pbm_v4/
obj-$(CONFIG_MACH_MT6765) += mdpm_v1/
obj-$(CONFIG_MACH_MT6761) += mdpm_v1/
obj-$(CONFIG_MACH_MT6758) += hps_v3/
obj-$(CONFIG_MACH_MT6765) += hps_v3/
obj-$(CONFIG_MACH_MT6761) += hps_v3/
obj-$(CONFIG_MACH_MT6758) += cpuidle_v3/
obj-$(CONFIG_MACH_MT6765) += cpuidle_v3/
obj-$(CONFIG_MACH_MT6761) += cpuidle_v3/
obj-$(CONFIG_MACH_MT6758) += power_gs_v1/
obj-$(CONFIG_MACH_MT6765) += power_gs_v1/
obj-$(CONFIG_MACH_MT6761) += power_gs_v1/
obj-$(CONFIG_MACH_MT6758) += udi_v1/
obj-$(CONFIG_MACH_MT6765) += udi_v1/
obj-$(CONFIG_MACH_MT6761) += udi_v1/
obj-$(CONFIG_MACH_MT6758) += mcdi/
obj-$(CONFIG_MACH_MT6765) += mcdi/
obj-$(CONFIG_MACH_MT6761) += mcdi/
ifeq ($(CONFIG_MTK_CM_MGR), y)
obj-$(CONFIG_MACH_MT6765) += cm_mgr_v1/
obj-$(CONFIG_MACH_MT6761) += cm_mgr_v1/
endif

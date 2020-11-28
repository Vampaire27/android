/*
 * Copyright (C) 2016 MediaTek Inc.
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

#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "mtk_mfg_counter.h"
#include "sysinfo.h"

#define __IMG_EXPLICIT_INCLUDE_HWDEFS
#if defined(__KERNEL__)
#include "rgx_cr_defs_km.h"
#else
#include RGX_BVNC_CORE_HEADER
#include RGX_BNC_CONFIG_HEADER
#include "rgx_cr_defs.h"
#endif
#undef __IMG_EXPLICIT_INCLUDE_HWDEFS

static DEFINE_SPINLOCK(counter_info_lock);
static DEFINE_SPINLOCK(met_callback_lock);

/* FIX ME: volatile can not pass check patch
 * static volatile void *g_MFG_base;
 */

static void *g_MFG_base;
static int mfg_is_power_on = -1;

/*The callback function to notify MET to query register*/
gpu_pmu_change_notify_fp gpu_pmu_change_notify = NULL;

/* FIX ME: volatile can not pass check patch
 * #define base_write32(addr, value) \
 *	do { *(volatile uint32_t *)(addr) = (uint32_t)(value) } while (0)
 * #define base_read32(addr)             (*(volatile uint32_t *)(addr))
 */
#define base_write32(addr, value) \
		do { *(uint32_t *)(addr) = (uint32_t)(value); } while (0)
#define base_read32(addr)             (*(uint32_t *)(addr))
#define MFG_write32(addr, value)      base_write32(g_MFG_base+addr, value)
#define MFG_read32(addr)              base_read32(g_MFG_base+addr)

#define base_write64(addr, value) \
        do { *(uint64_t *)(addr) = (uint64_t)(value); } while (0)
#define base_read64(addr)             (*(uint64_t *)(addr))
#define MFG_write64(addr, value)      base_write64(g_MFG_base+addr, value)
#define MFG_read64(addr)              base_read64(g_MFG_base+addr)

#ifndef MTK_UNREFERENCED_PARAMETER
#define MTK_UNREFERENCED_PARAMETER(param) ((void)(param))
#endif

typedef uint32_t (*mfg_read_pfn)(uint32_t offset);

static uint32_t RGX_read(uint32_t offset)
{
	return (g_MFG_base) ? MFG_read32(offset) : 0u;
}


static uint32_t mem_read_val = 0;
static uint32_t mem_write_val = 0;
static uint32_t mem_bw_val = 0;
static uint32_t g_time_s = 0;
static uint32_t g_time_us = 0;
static JOB_TYPE g_jobType = JOB_NONE;
static int g_frameCount = 0;

static uint32_t time_h_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);

    return g_time_s;
}

static uint32_t time_l_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);

    return g_time_us;
}

static uint32_t job_type_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);

    return g_jobType;
}

static uint32_t frames_count_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);

    return g_frameCount;
}

static uint32_t mem_read_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);
    uint32_t res = 0;

    if (g_MFG_base) {
        res += MFG_read32(RGX_CR_PERF_SLC0_READS);
        res += MFG_read32(RGX_CR_PERF_SLC1_READS);
        res += MFG_read32(RGX_CR_PERF_SLC2_READS);
        res += MFG_read32(RGX_CR_PERF_SLC3_READS);
    }
    mem_read_val = res;
    return res;
}

static uint32_t mem_write_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);
    uint32_t res = 0;

    if (g_MFG_base) {
        res += MFG_read32(RGX_CR_PERF_SLC0_WRITES);
        res += MFG_read32(RGX_CR_PERF_SLC1_WRITES);
        res += MFG_read32(RGX_CR_PERF_SLC2_WRITES);
        res += MFG_read32(RGX_CR_PERF_SLC3_WRITES);
    }
    mem_write_val =res;
    return res;
}

static uint32_t mem_bw_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);
    uint32_t res = 0;

    // 256 bits = 32 bytes
    res = (mem_read_val + mem_write_val) * 32;

    mem_bw_val = res;
    return res;
}

static uint32_t mem_predict_bw_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);
    uint32_t res = 0;

    res = mem_bw_val;
    return res;
}

#define _RGX_CR_USC_PERF_SELECTED_BITS                     (0x8148U)
#define _RGX_CR_USC_PERF_SELECTED_BITS_MASKFULL            (IMG_UINT64_C(0xFFFFFFFFFFFFFFFF))

#define _RGX_CR_USC_PERF_SELECT1                           (0x8110U)
#define _RGX_CR_USC_PERF_SELECT2                           (0x8118U)
#define _RGX_CR_USC_PERF_SELECT3                           (0x8120U)
#define _RGX_CR_USC_PERF_COUNTER_1                         (0x8158U)
#define _RGX_CR_USC_PERF_COUNTER_2                         (0x8160U)
#define _RGX_CR_USC_PERF_COUNTER_3                         (0x8168U)

uint32_t alu_running = 0;
uint32_t non_idle = 0;
uint32_t vtx_process = 0;
uint32_t pxl_process = 0;

static void settingCounter(void)
{
    uint64_t val = 0;
    // select USC instance 0
    MFG_write64(RGX_CR_USC_PERF_INDIRECT, 0);

    MFG_write64(_RGX_CR_USC_PERF_SELECTED_BITS, _RGX_CR_USC_PERF_SELECTED_BITS_MASKFULL);

    // select USC Group and mode
    // 21 MODE 0 = increment by count !¢FFD1!|, 1= increment by unsigned addition
    // 20:16 GROUP_SELECT Used to multiplex in one of the 16 groups to counter0
    // 15:0 BIT_SELECT Counter 0 bit select mask for enabled signals within a group.

    // PERF_USCPD0_ALU_RUNNING - USC_12, bit 0
    uint64_t mode = 0;
    uint64_t group = 12;
    uint64_t bit = 1; // 'b1
    val = ((mode << 21) + (group << 16) + bit);
    MFG_write64(RGX_CR_USC_PERF_SELECT0, val);

    // PERF_USC_NON_IDLE - USC_25, bit 0
    mode = 0;
    group = 25;
    bit = 1;
    val = ((mode << 21) + (group << 16) + bit);
    MFG_write64(_RGX_CR_USC_PERF_SELECT1, val);

    // PERF_USC_VTX_PROCESSING - USC_22, bit 0
    group = 22;
    bit = 1;
    val = ((mode << 21) + (group << 16) + bit);
    MFG_write64(_RGX_CR_USC_PERF_SELECT2, val);

    // PERF_USC_PXL_PROCESSING - USC_23, bit 0
    group = 23;
    bit = 1;
    val = ((mode << 21) + (group << 16) + bit);
    MFG_write64(_RGX_CR_USC_PERF_SELECT3, val);

    MFG_write64(RGX_CR_USC_PERF, 0x1E); // clear all counter

    MFG_write64(RGX_CR_USC_PERF, 0x01); // start counter

    alu_running = (uint32_t)(MFG_read64(RGX_CR_USC_PERF_COUNTER_0));
    non_idle = (uint32_t)(MFG_read64(_RGX_CR_USC_PERF_COUNTER_1));
    vtx_process = (uint32_t)(MFG_read64(_RGX_CR_USC_PERF_COUNTER_2));
    pxl_process = (uint32_t)(MFG_read64(_RGX_CR_USC_PERF_COUNTER_3));
}

static uint32_t alu_running_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);
    alu_running = 0;

    if (g_MFG_base) {
        uint64_t val = 0;

        // Check value
        val = MFG_read64(_RGX_CR_USC_PERF_SELECTED_BITS);
        if (val != _RGX_CR_USC_PERF_SELECTED_BITS_MASKFULL) {
            pr_debug("alu_running_func check, _RGX_CR_USC_PERF_SELECTED_BITS = 0x%" PRId64 "\n", val);
            settingCounter();
        }

        // read the counter back
        alu_running = (uint32_t)(MFG_read64(RGX_CR_USC_PERF_COUNTER_0));
    }
    return alu_running;
}

static uint32_t non_idle_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);
    non_idle = 0;

    if (g_MFG_base) {
        // read the counter back
        non_idle = (uint32_t)(MFG_read64(_RGX_CR_USC_PERF_COUNTER_1));
    }
    return non_idle;
}

static uint32_t vtx_process_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);
    vtx_process = 0;

    if (g_MFG_base) {
        vtx_process = (uint32_t)(MFG_read64(_RGX_CR_USC_PERF_COUNTER_2));
    }
    return vtx_process;
}

static uint32_t pxl_process_func(uint32_t offset)
{
    MTK_UNREFERENCED_PARAMETER(offset);
    pxl_process = 0;

    if (g_MFG_base) {
        pxl_process = (uint32_t)(MFG_read64(_RGX_CR_USC_PERF_COUNTER_3));
    }
    return pxl_process;
}

#define _RGX_CR_TPU_PWR_NUMBER_OF_TEXELS                   (0x17B0U)
static struct {
	const char *name;
	uint32_t offset;
	mfg_read_pfn read;
	unsigned int sum;
	unsigned int val_pre;
	int overflow;
} mfg_counters[] = {
{"time_h",  0,  time_h_func, 0u, 0u, 0},
{"time_l",  0,  time_l_func, 0u, 0u, 0},
{"job_type", 0, job_type_func, 0u, 0u, 0},
{"frames_count", 0, frames_count_func, 0u, 0u, 0},
{"pre_bw", RGX_CR_PERF_SLC0_WRITES, mem_predict_bw_func, 0u, 0u, 0},
{"mem_reads",  RGX_CR_PERF_SLC0_READS,  mem_read_func, 0u, 0u, 0},
{"mem_write", RGX_CR_PERF_SLC0_WRITES, mem_write_func, 0u, 0u, 0},
{"3d_cycles", RGX_CR_PERF_3D_CYCLES, RGX_read, 0u, 0u, 0},
{"ta_cycles", RGX_CR_PERF_TA_CYCLES, RGX_read, 0u, 0u, 0},
{"compute_cycle", RGX_CR_PERF_COMPUTE_CYCLES, RGX_read, 0u, 0u, 0},
{"tpu_texels_requested", _RGX_CR_TPU_PWR_NUMBER_OF_TEXELS, RGX_read, 0u, 0u, 0},
{"alu_running", RGX_CR_PERF_SLC0_READS, alu_running_func, 0u, 0u, 0},
{"non_idle", RGX_CR_PERF_SLC0_READS, non_idle_func, 0u, 0u, 0},
{"vtx_process", RGX_CR_PERF_SLC0_READS, vtx_process_func, 0u, 0u, 0},
{"pxl_process", RGX_CR_PERF_SLC0_READS, pxl_process_func, 0u, 0u, 0},
{"mem_bw", RGX_CR_PERF_SLC0_WRITES, mem_bw_func, 0u, 0u, 0},
};

#define MFG_COUNTER_SIZE (ARRAY_SIZE(mfg_counters))

static void update_time(void)
{
    uint64_t t, ui64Timestamp;
    ktime_t sTime = ktime_get();

    ui64Timestamp = sTime.tv64;

    t = ui64Timestamp + (NSEC_PER_USEC / 2);
    do_div(t, NSEC_PER_SEC);
    g_time_s = t;

    t = ui64Timestamp + (NSEC_PER_USEC / 2);
    do_div(t, NSEC_PER_USEC);
    g_time_us = do_div(t, USEC_PER_SEC);
}

/*
 * require: power must be on
 * require: get counters_lock
 */
static void mtk_mfg_counter_update(void)
{
    int i;

    update_time();

    for (i = 0; i < MFG_COUNTER_SIZE; ++i) {
        uint32_t val, diff;

        val = mfg_counters[i].read(mfg_counters[i].offset);

        if (strcmp(mfg_counters[i].name, "time_h") == 0 ||
            strcmp(mfg_counters[i].name, "time_l") == 0 ||
            strcmp(mfg_counters[i].name, "job_type") == 0) {
            mfg_counters[i].sum = val;
        } else if (strcmp(mfg_counters[i].name, "frames_count") == 0) {
            if (val == mfg_counters[i].val_pre) {
                mfg_counters[i].sum = 0;
            } else {
                mfg_counters[i].sum = 1;
            }
        } else if (g_jobType != JOB_TA && g_jobType != JOB_TA_END) {
            diff = val - mfg_counters[i].val_pre;

            /* TODO: counter is reset by fw, how to be notify? */
            if (diff > 0xf7654321)
                diff = 0u - diff;
            if (mfg_counters[i].sum + diff < mfg_counters[i].sum) {
                mfg_counters[i].overflow = 1;
                mfg_counters[i].sum = (uint32_t)-1;
            } else if (mfg_counters[i].overflow == 0)
                mfg_counters[i].sum += diff;
        }
        mfg_counters[i].val_pre = val;
    }
}

/*
 * require: get counters_lock
 */
static void mtk_mfg_counter_reset_record(void)
{
	int i;

	for (i = 0; i < MFG_COUNTER_SIZE; ++i) {
		mfg_counters[i].sum = 0u;
		mfg_counters[i].overflow = 0;
	}
}

/*
 * require: get counters_lock
 */
static void mtk_mfg_counter_reset_register(void)
{
	int i;

	for (i = 0; i < MFG_COUNTER_SIZE; ++i)
		mfg_counters[i].val_pre = 0u;
}

static int img_get_gpu_pmu_init(GPU_PMU *pmus, int pmu_size, int *ret_size)
{
	if (pmus) {
		int size = (pmu_size > MFG_COUNTER_SIZE)
					? MFG_COUNTER_SIZE : pmu_size;
		int i;

		for (i = 0; i < size; ++i) {
			pmus[i].id = i;
			pmus[i].name = mfg_counters[i].name;
			pmus[i].value = mfg_counters[i].sum;
			pmus[i].overflow = mfg_counters[i].overflow;
		}
	}

	if (ret_size)
		*ret_size = MFG_COUNTER_SIZE;

	return 0;
}

static int img_get_gpu_pmu_swapnreset(GPU_PMU *pmus, int pmu_size)
{
	if (pmus) {

		int size = (pmu_size > MFG_COUNTER_SIZE)
					? MFG_COUNTER_SIZE : pmu_size;
		int i;

		spin_lock(&counter_info_lock);

		/* update if gpu power on */
		if (mfg_is_power_on)
			mtk_mfg_counter_update();

		/* swap */
		for (i = 0; i < size; ++i) {
			pmus[i].value = mfg_counters[i].sum;
			pmus[i].overflow = mfg_counters[i].overflow;
		}

		/* reset */
		mtk_mfg_counter_reset_record();

		spin_unlock(&counter_info_lock);
	}

	return 0;
}

static int img_register_gpu_pmu_change(gpu_pmu_change_notify_fp callback)
{
    spin_lock(&met_callback_lock);
    gpu_pmu_change_notify = callback;
    spin_unlock(&met_callback_lock);
    return 0; // means no error
}

static void *_mtk_of_ioremap(const char *node_name)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, node_name);
	if (node)
		return of_iomap(node, 0);

	pr_debug("cannot find [%s] of_node, please fix me\n", node_name);
	return NULL;
}

static void gpu_power_change_notify_mfg_counter(int power_on)
{
    if (mfg_is_power_on == power_on) {
        return;
    }
    spin_lock(&counter_info_lock);

    if (!power_on) {
        g_jobType = JOB_NONE;
        /* update before power off */
        mtk_mfg_counter_update();
        mtk_mfg_counter_reset_register();
        MFG_write64(RGX_CR_USC_PERF, 0x00); // stop counter
    } else {
        settingCounter();
    }

    mfg_is_power_on = power_on;

    spin_unlock(&counter_info_lock);
}

/* ****************************************** */

void mtk_mfg_counter_init(void)
{
    g_MFG_base = _mtk_of_ioremap(SYS_RGX_OF_COMPATIBLE);

    mtk_get_gpu_pmu_init_fp = img_get_gpu_pmu_init;
    mtk_get_gpu_pmu_swapnreset_fp = img_get_gpu_pmu_swapnreset;
    mtk_register_gpu_pmu_change_fp = img_register_gpu_pmu_change;

    mtk_register_gpu_power_change("mfg_counter",
        gpu_power_change_notify_mfg_counter);
}

void mtk_mfg_counter_destroy(void)
{
	mtk_unregister_gpu_power_change("mfg_counter");
}

int mtk_inner_get_gpu_pmu_swapnreset(int time_s, int time_us,
    JOB_TYPE jobType, int frameCount)
{

    g_time_s = time_s;
    g_time_us = time_us;
    g_jobType = jobType;
    g_frameCount = frameCount;

    spin_lock(&met_callback_lock);
    if (gpu_pmu_change_notify) {
        gpu_pmu_change_notify();
    }
    spin_unlock(&met_callback_lock);
    return 0;
}


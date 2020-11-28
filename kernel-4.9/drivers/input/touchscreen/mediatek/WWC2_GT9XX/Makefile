# Linux driver folder
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/WWC2_GT9XX/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/hwmon/include
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/WWC2_GT9XX/include/$(CONFIG_GT9XX_CONFIG)/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/

obj-y	+=  gt9xx_driver.o
obj-y	+=  gt9xx_extents.o
obj-y	+=  gt9xx_update.o
obj-y	+=  goodix_tool.o

ifeq ($(TARGET_PRODUCT),full_ch006_04)
        ccflags-y += -DGOODIX_TP_LINGFEI
endif

#ifndef MTK_LCM_DEVICE_TREE_SUPPORT
#ifndef BUILD_LK
    #include <linux/string.h>
    #include <linux/kernel.h>
#endif
#include "lcm_drv.h"

#ifdef BUILD_LK
#define LCD_DEBUG(fmt, args...) printf(fmt, ##args)		
#else
//#define LCD_DEBUG(fmt, args...) printk(fmt, ##args)
#define LCD_DEBUG(fmt, args...)
#endif

#ifndef BUILD_LK
#include <linux/kernel.h>
#include <linux/module.h>  
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
//#include <linux/jiffies.h>
#include <linux/uaccess.h>
//#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>


/***************************************************************************** 
 * Define
 *****************************************************************************/
#ifndef MACH_FPGA
//#define ICN6202_I2C_BUSNUM  3  //
#define ICN6202_ID_NAME "icn6202"
//#define ICN6202_ADDR 0x2C
/***************************************************************************** 
 * GLobal Variable
 *****************************************************************************/
#ifdef CONFIG_MTK_LEGACY
static struct i2c_board_info icn6202_board_info __initdata = { I2C_BOARD_INFO(ICN6202_ID_NAME, ICN6202_ADDR) };
#else
static const struct of_device_id lcm_of_match[] = {
		{.compatible = "mediatek,i2c_icn6202"},
		{},
};
#endif
/*static struct i2c_client *icn6202_i2c_client;*/
struct i2c_client *icn6202_i2c_client;


/***************************************************************************** 
 * Function Prototype
 *****************************************************************************/ 
static int icn6202_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int icn6202_remove(struct i2c_client *client);
/***************************************************************************** 
 * Data Structure
 *****************************************************************************/


static const struct i2c_device_id icn6202_id[] = {
	{ ICN6202_ID_NAME, 0 },
	{ }
};

//#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
//static struct i2c_client_address_data addr_data = { .forces = forces,};
//#endif

static struct i2c_driver icn6202_iic_driver = {
	.id_table	= icn6202_id,
	.probe		= icn6202_probe,
	.remove		= icn6202_remove,
	//.detect		= mt6605_detect,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "icn6202",
#ifndef CONFIG_MTK_LEGACY
		.of_match_table = lcm_of_match,
#endif
	},
 
};
/***************************************************************************** 
 * Extern Area
 *****************************************************************************/ 
 
 

/***************************************************************************** 
 * Function
 *****************************************************************************/ 
static int icn6202_probe(struct i2c_client *client, const struct i2c_device_id *id)
{  
	LCD_DEBUG( "********* icn6202_iic_probe\n");
	LCD_DEBUG("********* icn6202: info==>name=%s addr=0x%x\n",client->name,client->addr);
	icn6202_i2c_client  = client;		
	return 0;      
}


static int icn6202_remove(struct i2c_client *client)
{  	
	LCD_DEBUG( "********* icn6202_remove\n");
	icn6202_i2c_client = NULL;
	i2c_unregister_device(client);
	return 0;
}

int icn6202_i2c_write_bytes_kernel(unsigned char reg, unsigned char data)
//int icn6202_write_bytes(unsigned char addr, unsigned char value)
{	
	int ret = 0;
	struct i2c_client *client = icn6202_i2c_client;
	char write_data[2]={0};	
	//client->addr = ICN6202_ADDR; //(0x5A>>1);
	//client->ext_flag = (client->ext_flag & I2C_MASK_FLAG) & (~ I2C_DMA_FLAG);

	LCD_DEBUG("icn6202_i2c_write_bytes_kernel !!\n");
	write_data[0]= reg;
	write_data[1] = data;
    	ret=i2c_master_send(client, write_data, 2);
	if(ret<0)
	{
		printk("********* icn6202 write data fail !!\n");	
	}
		
	return ret ;
}

int icn6202_i2c_read_bytes_kernel(unsigned char reg)
{
	unsigned int ret_code = 0;
	struct i2c_client *client = icn6202_i2c_client;
    //client->addr = ICN6202_ADDR; //(0x5B>>1);
	//client->ext_flag = (client->ext_flag & I2C_MASK_FLAG) & (~ I2C_DMA_FLAG);	

	LCD_DEBUG("icn6202_i2c_read_bytes_kernel !!\n");
	ret_code = i2c_smbus_read_byte_data(client, reg);
	//printk("ICN6202 kernel: addr= %x, value = %x ",addr,ret_code);
	return ret_code;
}


/*
 * module load/unload record keeping
 */

static int __init icn6202_iic_init(void)
{

   LCD_DEBUG( "********* icn6202_iic_init\n");
#ifdef CONFIG_MTK_LEGACY
   i2c_register_board_info(ICN6202_I2C_BUSNUM, &icn6202_board_info, 1);
   LCD_DEBUG( "********* icn6202_iic_init2\n");
#endif
   i2c_add_driver(&icn6202_iic_driver);
   LCD_DEBUG( "********* icn6202_iic_init success\n");	
   return 0;
}

static void __exit icn6202_iic_exit(void)
{
  LCD_DEBUG( "********* icn6202_iic_exit\n");
  i2c_del_driver(&icn6202_iic_driver);  
}

module_init(icn6202_iic_init);
module_exit(icn6202_iic_exit);

MODULE_AUTHOR("jie.fang");
MODULE_DESCRIPTION("WTWD icn6202 I2C Driver");
MODULE_LICENSE("GPL"); 
#endif //#ifndef BUILD_LK
#endif //#ifndef MACH_FPGA
#endif //#ifndef MTK_LCM_DEVICE_TREE_SUPPORT


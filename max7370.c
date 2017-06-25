#include <linux/module.h>  
#include <linux/kernel.h>   
#include <linux/init.h> 
#include <linux/fs.h>
#include <linux/device.h>        
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include<linux/slab.h>            
#include <linux/gpio.h>
#include <linux/interrupt.h>


static u16 INT_GPIO =    117;
static u8 KB_I2C  =      1;
static u8 KB_ADDR =      0x38;

/*
 * MAX7370 registers
 */
#define MAX7370_REG_KEYFIFO	         0x00
#define MAX7370_REG_CONFIG	         0x01
#define MAX7370_REG_DEBOUNCE         0x02
#define MAX7370_REG_INTERRUPT        0x03
#define MAX7370_REG_PORTS            0x04
#define MAX7370_REG_KEYREP	         0x05
#define MAX7370_REG_SLEEP	         0x06
#define MAX7370_REG_ARR_SIZE         0x30


/*
 * Configuration register bits
 */
#define MAX7370_CFG_SLEEP       (1 << 7)
#define MAX7370_CFG_INTERRUPT   (1 << 5)
#define MAX7370_CFG_KEY_RELEASE (1 << 3)
#define MAX7370_CFG_WAKEUP      (1 << 1)
#define MAX7370_CFG_TIMEOUT     (1 << 0)

/*
 * Autosleep register values (ms)
 */
#define MAX7370_AUTOSLEEP_8192	        0x01
#define MAX7370_AUTOSLEEP_4096	        0x02
#define MAX7370_AUTOSLEEP_2048	        0x03
#define MAX7370_AUTOSLEEP_1024	        0x04
#define MAX7370_AUTOSLEEP_512	        0x05
#define MAX7370_AUTOSLEEP_256	        0x06
#define MAX7370_AUTOSLEEP_DISABLE       0x00

static unsigned int irqNumber;

static int minor = 0;
module_param( minor, int, S_IRUGO );

static struct input_dev *input;
struct i2c_client *max7370_client;
struct device_node *node;

static int max7370_write_reg(struct i2c_client *client, u8 reg, u8 val) {
	int ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n", __func__,
				reg, val, ret);
	return ret;
}

static int max7370_read_reg(struct i2c_client *client, int reg) {
	int ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, err %d\n", __func__, reg, ret);
	return ret;
}

static void max7370_initialize(struct i2c_client *client) {
  
    /* disable sleep, enable key release, clear int on first read */
	max7370_write_reg(client, MAX7370_REG_CONFIG, MAX7370_CFG_KEY_RELEASE | MAX7370_CFG_INTERRUPT | MAX7370_CFG_WAKEUP); 
	
	/* debounce time 16ms */
	max7370_write_reg(client, MAX7370_REG_DEBOUNCE, 0x77);
	         	
	/* nINT asserts every debounce cycles */
	max7370_write_reg(client, MAX7370_REG_INTERRUPT, 0x01);
	
	/* disable Autosleep  */
	max7370_write_reg(client, MAX7370_REG_SLEEP, MAX7370_AUTOSLEEP_DISABLE);
	
	/* kb arr size 4x3*/
	max7370_write_reg(client, MAX7370_REG_ARR_SIZE, 0xFF); 
}

static irqreturn_t kb_irq_handler(int irq, void *data) {
	
	unsigned int keycode;
	unsigned char max7370_data = 0;
	unsigned char code_valid;
	
	while(max7370_data != 0x3f) {
		max7370_data = max7370_read_reg(max7370_client, MAX7370_REG_KEYFIFO);
		code_valid = 1;
		switch(max7370_data)
		{
			case 0:  keycode = KEY_T; break;			
			case 8:  keycode = KEY_0; break;			
			case 4:  keycode = KEY_1; break;			
			case 12: keycode = KEY_2; break;
			case 17: keycode = KEY_3; break;
			case 2:  keycode = KEY_4; break;
			case 10: keycode = KEY_5; break;			
			case 18: keycode = KEY_6; break;			
			case 3:  keycode = KEY_7; break;			
			case 11: keycode = KEY_8; break;
			case 20: keycode = KEY_9; break;		
			case 16: keycode = KEY_X; break;
			
			case 22: keycode = KEY_A; break;			
			case 14: keycode = KEY_B; break;			
			case 6:  keycode = KEY_C; break;
			case 5:  keycode = KEY_D; break;		
			case 13: keycode = KEY_E; break;
			case 21: keycode = KEY_F; break;
			
			default: code_valid = 0;
		}
		if(code_valid) {
			input_report_key(input, keycode, 1);	
			input_report_key(input, keycode, 0);
			input_sync(input);
		}
	}
	
	return IRQ_HANDLED;
}

static int read_dts_pins(void) {
	u32 dts_data;

	node = of_find_node_by_name(NULL, "max7370"); 
	
	if (node != NULL) {
	    if (!of_property_read_u32(node, "max_addr", &dts_data)) {	      
	      KB_ADDR = dts_data;
	    }
	    else {
	      printk (KERN_INFO "reg not found, use default value: %d\n", KB_ADDR);
	    }
	    if (!of_property_read_u32(node, "max_bus_n", &dts_data)) {	      
	      KB_I2C = dts_data;
	      
	    }
	     else {
	      printk (KERN_INFO "bus number not found, use default value: %d\n", KB_I2C);
	    }
	    
	    if (!of_property_read_u32(node, "max_irq_pin", &dts_data)) {
	      INT_GPIO = dts_data;	      
	    }
	     else {
	      printk (KERN_INFO "irq_pin not found, use default value: %d\n", INT_GPIO);
	    }
	} else {
		return 1;
	}
	return 0;
}



static int __init test_init( void )
{
	int ret = 0;
	int result;
	struct i2c_adapter * my_adap;
	
	
 	if(read_dts_pins() != 0) {
		printk(KERN_INFO "max7370: no dts params\n");
 		return -ENODEV;
 	}
	
	my_adap = i2c_get_adapter(KB_I2C);
	
	printk( KERN_ALERT "max7370 starting...\n" );
		
	max7370_client = kmalloc(sizeof(*max7370_client), GFP_KERNEL);

	if (!max7370_client) {
		dev_err(&max7370_client->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
       
	max7370_client = i2c_new_dummy (my_adap, KB_ADDR);

	if (ret < 0) {
		dev_err(&max7370_client->dev, "failed to detect device\n");
		return -ENODEV;
	}
	while(ret != 0x3f) {
		ret = max7370_read_reg(max7370_client, MAX7370_REG_KEYFIFO);
	}

	gpio_request(INT_GPIO, "sysfs");
	gpio_direction_input(INT_GPIO);
	gpio_export(INT_GPIO,false);

	irqNumber = gpio_to_irq(INT_GPIO);
	printk(KERN_INFO "The button is mapped to IRQ: %d\n", irqNumber);

	max7370_client->irq = irqNumber;
	result = devm_request_threaded_irq(&max7370_client->dev, 
			max7370_client->irq, 
			NULL,                            // The interrupt number requested 
			kb_irq_handler,                  // The pointer to the handler function (above)
			IRQF_TRIGGER_LOW | IRQF_ONESHOT, 
			"kb_irq_handler",                // Used in /proc/interrupts to identify the owner
			NULL);                           // The *dev_id for shared interrupt lines, NULL here
	
	if(result) {
		printk(KERN_INFO "short: can't get assigned irq");
	}

	max7370_initialize(max7370_client);
	   
	input = input_allocate_device();

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;
	
	set_bit(EV_KEY, input->evbit);

	set_bit(KEY_0, input->keybit);
	set_bit(KEY_1, input->keybit);
	set_bit(KEY_2, input->keybit);
	set_bit(KEY_3, input->keybit);
	set_bit(KEY_4, input->keybit);
	set_bit(KEY_5, input->keybit);
	set_bit(KEY_6, input->keybit);
	set_bit(KEY_7, input->keybit);
	set_bit(KEY_8, input->keybit);
	set_bit(KEY_9, input->keybit);
	set_bit(KEY_T, input->keybit);
	set_bit(KEY_X, input->keybit);
	set_bit(KEY_A, input->keybit);
	set_bit(KEY_B, input->keybit);
	set_bit(KEY_C, input->keybit);
	set_bit(KEY_D, input->keybit);
	set_bit(KEY_E, input->keybit);
	set_bit(KEY_F, input->keybit);
	
	result = input_register_device(input);
	
	return 0;
}


static void __exit test_exit( void )
{
	printk( KERN_ALERT "max7370 is unloaded!\n" );
	i2c_unregister_device(max7370_client);
	
	input_unregister_device(input);
	input_free_device(input);
	
	gpio_unexport(INT_GPIO);
	gpio_free(INT_GPIO); 
}


module_init( test_init);
module_exit( test_exit);

MODULE_SUPPORTED_DEVICE( "test" );
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("driver");


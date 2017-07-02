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

/*  dts
	max7370: max7370@38{
		 compatible = "mx,max7370";
		 max7370_addr = <0x38>;
		 max7370_irq_pin = <117>;
		 max7370_bus_n = <1>;
		 max7370_keycodemax = <0xff>;
		 max7370_dbg_en = <0>;
		 max7370_keycodes = <48 10 7 4 11 9 6 3 30 8 5 2 >;
		 max7370_scancodes = <0 1 2 3 8 9 10 11 16 17 18 19>;		 
    };
*/


u16 INT_GPIO   =    117;
u8 KB_I2C      =    1;
u8 dbg         =    0;
u8 KB_ADDR     =    0x38;
u8 MAX_KEYCODE =    60;

u8 kb_array[255];

/*
 * MAX7370 registers
 */
#define MAX7370_REG_KEYFIFO          0x00
#define MAX7370_REG_CONFIG           0x01
#define MAX7370_REG_DEBOUNCE         0x02
#define MAX7370_REG_INTERRUPT        0x03
#define MAX7370_REG_PORTS            0x04
#define MAX7370_REG_KEYREP           0x05
#define MAX7370_REG_SLEEP            0x06
#define MAX7370_REG_ARR_SIZE         0x30


/*
 * Configuration register bits
 */
#define MAX7370_CFG_SLEEP           (1 << 7)
#define MAX7370_CFG_INTERRUPT       (1 << 5)
#define MAX7370_CFG_KEY_RELEASE     (1 << 3)
#define MAX7370_CFG_WAKEUP          (1 << 1)
#define MAX7370_CFG_TIMEOUT         (1 << 0)
#define MAX7370_CFG_KEY_NORELEASE   (0 << 3)

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
	max7370_write_reg(client, 
	        MAX7370_REG_CONFIG, MAX7370_CFG_KEY_NORELEASE | MAX7370_CFG_INTERRUPT | MAX7370_CFG_WAKEUP); 
	
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
	int max7370_data = 0;

	while(max7370_data != 0x3f) {
		max7370_data = max7370_read_reg(max7370_client, MAX7370_REG_KEYFIFO);
		if(max7370_data < 0) {
			return IRQ_HANDLED;
		}
		else {
		   if((max7370_data != 0x3f) && (dbg == 1)) {
		      printk (KERN_INFO "max7370 scancode = %d\n", max7370_data);
           }
	    }
		if(kb_array[max7370_data] != 0xff) {
			keycode = kb_array[max7370_data];
			if(dbg == 1) {
			    printk (KERN_INFO "linux keycode = %d\n", keycode);
			}
			input_report_key(input, keycode, 1);	
			input_sync(input);
		}
	}
	
	return IRQ_HANDLED;
}

static int read_dts_pins(void) {
	u32 dts_data;
	u32 dts_data2;
	u8 i;

	node = of_find_node_by_name(NULL, "max7370"); 
	
	if (node != NULL) {
	    if (!of_property_read_u32(node, "max7370_addr", &dts_data)) {	      
	      KB_ADDR = dts_data;
	    }
	    else {
	      printk (KERN_INFO "reg not found, use default value: %d\n", KB_ADDR);
	    }
	    
	    if (!of_property_read_u32(node, "max7370_bus_n", &dts_data)) {	      
	      KB_I2C = dts_data;	       
	    }
	    else {
	      printk (KERN_INFO "bus number not found, use default value: %d\n", KB_I2C);
	    }
	    
	    if (!of_property_read_u32(node, "max7370_irq_pin", &dts_data)) {
	      INT_GPIO = dts_data;	      
	    }
	    else {
	      printk (KERN_INFO "irq_pin not found, use default value: %d\n", INT_GPIO);
	    }
	    
	    if (!of_property_read_u32(node, "max7370_keycodemax", &dts_data)) {
	      MAX_KEYCODE = dts_data;	      
	    }
	    else {
	      printk (KERN_INFO "max_keycodemax not found, use default value: %d\n", MAX_KEYCODE);
	    }
	    
	    if (!of_property_read_u32(node, "max7370_dbg_en", &dts_data)) {
	      dbg = dts_data; 
	      dbg = 1;    
	    }
	    else {
	      printk (KERN_INFO "debug is not enabled: %d\n", MAX_KEYCODE);
	    }
	    
	    memset(kb_array, 0xff, sizeof(kb_array));
	    
	    for(i = 0; i < 254; i++) {
			if(!of_property_read_u32_index(node, "max7370_keycodes", i, &dts_data)) {
				if(!of_property_read_u32_index(node, "max7370_scancodes", i, &dts_data2)) {
					kb_array[dts_data2] = dts_data;
				}
				else {
					break;
					
				}
			}
			else {
				break;
			}
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
	int i;
	unsigned int irqNumber;
	struct i2c_adapter *max_i2c_adap;
	
 	if(read_dts_pins() != 0) {
		printk(KERN_INFO "max7370: no dts params\n");
 		return -ENODEV;
 	}
    //get adapter address
	max_i2c_adap = i2c_get_adapter(KB_I2C);
	if (max_i2c_adap <= 0) {
		printk(KERN_INFO "failed to find I2C bus\n");
		return -ENODEV;
    }

    //allocate memory
	max7370_client = kmalloc(sizeof(*max7370_client), GFP_KERNEL); 
	if (!max7370_client) {
		dev_err(&max7370_client->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	
    //set client address and bus_n   
	max7370_client = i2c_new_dummy (max_i2c_adap, KB_ADDR);
	
	//probe device 
	ret = max7370_read_reg(max7370_client, MAX7370_REG_KEYFIFO);
	if(ret < 0) {
		dev_err(&max7370_client->dev, "failed to detect device\n");
		return -ENODEV;
	}

	gpio_request(INT_GPIO, "sysfs");
	gpio_direction_input(INT_GPIO);
	gpio_export(INT_GPIO,false);

	irqNumber = gpio_to_irq(INT_GPIO);
	printk(KERN_INFO "The button is mapped to IRQ: %d\n", irqNumber);

	max7370_client->irq = irqNumber;
	result = devm_request_threaded_irq(&max7370_client->dev, 
			max7370_client->irq, 
			NULL, // The interrupt number requested 
			kb_irq_handler, // The pointer to the handler function (above)
			IRQF_TRIGGER_LOW | IRQF_ONESHOT, // Interrupt is on rising edge (button press in Fig.1)
			"kb_irq_handler", // Used in /proc/interrupts to identify the owner
			NULL); // The *dev_id for shared interrupt lines, NULL here
	
	if(result) {
		printk(KERN_INFO "short: can't get assigned irq");
	}

	max7370_initialize(max7370_client);
	   
	input = input_allocate_device();
	input->name = "matrix_kb";
	input->phys = "/max7370-kb/input0";
	
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;
	
	set_bit(EV_KEY, input->evbit);

    for(i = 0; i < MAX_KEYCODE; i++) { 
		__set_bit(i, input->keybit);
	}	
	result = input_register_device(input);
	
	return 0;
}


static void __exit test_exit( void )
{
	printk( KERN_ALERT "max7370 is unloaded!\n" );
	i2c_unregister_device(max7370_client);
	
	input_unregister_device(input);
	input_free_device(input);
	
	gpio_unexport(INT_GPIO); // Unexport the Button GPIO
	gpio_free(INT_GPIO); // Free the LED GPIO
}


module_init( test_init);
module_exit( test_exit);

MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("matrix kb driver");


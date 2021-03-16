#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#define ACPI_TZ_METHOD_GTMM "\\_TZ.GTMM" /* GetThermalStatus */
#define ACPI_TZ_METHOD_GFVE "\\_TZ.GFVE" /* GetFanValueExtended? */
#define ACPI_TZ_METHOD_GTFV "\\_TZ.GTFV" /* GetTargetFanValue */
#define ACPI_TZ_METHOD_GFRM "\\_TZ.GFRM" /* GetFanRPM */
#define ACPI_TZ_METHOD_GTRM "\\_TZ.GTRM" /* GetTargetFanRPM */
#define ACPI_TZ_METHOD_GFSD "\\_TZ.GFSD" /* GetFanSpeedDecimal? */

#define ACPI_EC_METHOD_GFSD "\\_SB.PCI0.LPCB.EC0.GFSD" /* GetFanSpeedDecimal */
#define ACPI_EC_METHOD_KGFS "\\_SB.PCI0.LPCB.EC0.KGFS" /* GetFanSpeed */
#define ACPI_EC_METHOD_KRFS "\\_SB.PCI0.LPCB.EC0.KRFS" /* GetRightFanSpeed (GPU) */
#define ACPI_EC_METHOD_SFSD "\\_SB.PCI0.LPCB.EC0.SFSD" /* SetFanSpeedDecimal */

#define ACPI_TZ_VALUE_MRPM "\\_TZ.MRPM" /* Max RPM */
#define ACPI_TZ_VALUE_FRDC "\\_TZ.FRDC" /* Fan 0 value */
#define ACPI_TZ_VALUE_FR2C "\\_TZ.FR2C" /* Fan 1 value */
#define ACPI_TZ_VALUE_FTDC "\\_TZ.FTDC" /* Fan 0 target value */
#define ACPI_TZ_VALUE_FT2C "\\_TZ.FT2C" /* Fan 1 target value */

#define ACPI_EC_VALUE_MFAC "\\_SB.PCI0.LPCB.EC0.MFAC" /* Fan always-on on AC */
#define ACPI_EC_MUTEX      "\\_SB.PCI0.LPCB.EC0.ECMX" /* EC Mutex */
#define ACPI_EC_REGION     "\\_SB.PCI0.LPCB.EC0.ECRG" /* EC Region control? */

enum {
	HP_FANREAD_NONE = 0x01, /* No fan speed information */
	HP_FANREAD_GTMM = 0x03, /* Use WMID/WMIV GetThermalStatus */
	HP_FANREAD_GFVE = 0x04, /* Use EC0 GFVE */
	HP_FANREAD_GFRM = 0x05, /* Use EC0 GFRM */
	HP_FANREAD_KGFS = 0x06, /* Use EC0 KGFS/KRFS */
	HP_FANREAD_GFSD = 0x07, /* Use EC0 GFSD */
	HP_FANREAD_I2CC = 0x08, /* Use fan control chip through I2C */
};

enum {
	HP_FANCTRL_AUTO = 0x22, /* BIOS controls fanspeed */
	HP_FANCTRL_STMM = 0x23, /* Use WMID/WMIV SetThermalStatus */
	HP_FANCTRL_KSFS = 0x24, /* Use EC0 KSFS */
	HP_FANCTRL_KFCL = 0x25, /* Use EC0 KFCL */
	HP_FANCTRL_SFSD = 0x26, /* Use EC0 SFSD */
	HP_FANCTRL_I2CC = 0x27, /* Use fan control chip through I2C */
}; 

static int debug = 0;
module_param(debug, int, 0660);

static int readtype = 0;

static int readtype_op_write_handler(const char *val, const struct kernel_param *kp) {
	char valcp[16];
	char *s;
 
	strncpy(valcp, val, 16);
	valcp[15] = '\0';
 
	s = strstrip(valcp);
 
	if (strcmp(s, "none") == 0)
		readtype = HP_FANREAD_NONE;
	else if (strcmp(s, "gtmm") == 0)
		readtype = HP_FANREAD_GTMM;
	else if (strcmp(s, "gfve") == 0)
		readtype = HP_FANREAD_GFVE;
	else if (strcmp(s, "gfrm") == 0)
		readtype = HP_FANREAD_GFRM;
	else if (strcmp(s, "kgfs") == 0)
		readtype = HP_FANREAD_KGFS;
	else if (strcmp(s, "gfsd") == 0)
		readtype = HP_FANREAD_GFSD;
	else if (strcmp(s, "i2cc") == 0)
		readtype = HP_FANREAD_I2CC;
	else
		return -EINVAL;
	return 0;
}

static int readtype_op_read_handler(char *buffer, const struct kernel_param *kp) {
	switch (readtype) {
	case HP_FANREAD_NONE:
		strcpy(buffer, "none");
		break;
	case HP_FANREAD_GTMM:
		strcpy(buffer, "gtmm");
		break;
	case HP_FANREAD_GFVE:
		strcpy(buffer, "gfve");
		break;
	case HP_FANREAD_GFRM:
		strcpy(buffer, "gfrm");
		break;
	case HP_FANREAD_KGFS:
		strcpy(buffer, "kgfs");
		break;
	case HP_FANREAD_GFSD:
		strcpy(buffer, "gfsd");
		break;
	case HP_FANREAD_I2CC:
		strcpy(buffer, "i2cc");
		break;
	}
 
	return strlen(buffer);
}

static const struct kernel_param_ops readtype_op_ops = {
	.set = readtype_op_write_handler,
	.get = readtype_op_read_handler
};

module_param_cb(readtype, &readtype_op_ops, NULL, 0660);


static int ctrltype = 0;

static int ctrltype_op_write_handler(const char *val, const struct kernel_param *kp) {
	char valcp[16];
	char *s;
 
	strncpy(valcp, val, 16);
	valcp[15] = '\0';
 
	s = strstrip(valcp);
 
	if (strcmp(s, "auto") == 0)
		ctrltype = HP_FANCTRL_AUTO;
	else if (strcmp(s, "stmm") == 0)
		ctrltype = HP_FANCTRL_STMM;
	else if (strcmp(s, "ksfs") == 0)
		ctrltype = HP_FANCTRL_KSFS;
	else if (strcmp(s, "kfcl") == 0)
		ctrltype = HP_FANCTRL_KFCL;
	else if (strcmp(s, "sfsd") == 0)
		ctrltype = HP_FANCTRL_SFSD;
	else if (strcmp(s, "i2cc") == 0)
		ctrltype = HP_FANCTRL_I2CC;
	else
		return -EINVAL;
	return 0;
}

static int ctrltype_op_read_handler(char *buffer, const struct kernel_param *kp) {
	switch (ctrltype) {
	case HP_FANCTRL_AUTO:
		strcpy(buffer, "auto");
		break;
	case HP_FANCTRL_STMM:
		strcpy(buffer, "stmm");
		break;
	case HP_FANCTRL_KSFS:
		strcpy(buffer, "ksfs");
		break;
	case HP_FANCTRL_KFCL:
		strcpy(buffer, "kfcl");
		break;
	case HP_FANCTRL_SFSD:
		strcpy(buffer, "sfsd");
		break;
	case HP_FANCTRL_I2CC:
		strcpy(buffer, "i2cc");
		break;
	}
 
	return strlen(buffer);
}

static const struct kernel_param_ops ctrltype_op_ops = {
	.set = ctrltype_op_write_handler,
	.get = ctrltype_op_read_handler
};

module_param_cb(ctrltype, &ctrltype_op_ops, NULL, 0660);

struct driver_data {
	struct device *hwmon_dev;
	int has_second_fan;
	int has_rpm;
};

static const int rpm_from_frdc(ssize_t input) {
	int res = 0;
	if (input && input != 0xFF) {
		res = (0x0003C000 + (input >> 1)) / input;
	}
	return res;
}

static int acpi_exists(const char * method) {
	acpi_status status;
    acpi_handle handle;
	status = acpi_get_handle(NULL, (acpi_string) method, &handle);
    if (ACPI_FAILURE(status))
    {
        return 0;
    }
	return 1;
}

static void acpi_call(const char * method, int argc, union acpi_object *argv, ssize_t *ret) {
    acpi_status status;
    acpi_handle handle;
    struct acpi_object_list arg;
    struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *res;

	if (debug)
    	printk(KERN_INFO "acpi_call: Calling %s\n", method);

    status = acpi_get_handle(NULL, (acpi_string) method, &handle);

    if (ACPI_FAILURE(status))
    {
		if (debug)
        	printk(KERN_ERR "acpi_call: Cannot get handle\n");
        return;
    }

    arg.count = argc;
    arg.pointer = argv;

    status = acpi_evaluate_object(handle, NULL, &arg, &buffer);
    if (ACPI_FAILURE(status))
    {
		if (debug)
        	printk(KERN_ERR "acpi_call: Method call failed\n");
		return;
    }
	res = buffer.pointer;
	
	if (res->type == ACPI_TYPE_INTEGER) {
		*ret = res->integer.value;
	}
}

static const int read_gfve(int channel) {
	ssize_t fanspeed;
	union acpi_object *arg = kmalloc(sizeof(union acpi_object), GFP_KERNEL);
	arg->type = ACPI_TYPE_INTEGER;
	arg->integer.value = channel + 1;
	acpi_call(ACPI_TZ_METHOD_GFVE, 1, arg, &fanspeed);
	kfree(arg);
	return rpm_from_frdc(fanspeed);
}

static const int read_gfsd(int channel) {
	ssize_t speed = 0;
	if (channel == 0) {
		acpi_call(ACPI_EC_METHOD_GFSD, 0, NULL, &speed);
	}
	return speed;
}

static const int read_kgfs(int channel) {
	ssize_t speed = 0;
	if (channel == 0) {
		acpi_call(ACPI_EC_METHOD_KGFS, 0, NULL, &speed);
		if (speed == 0x14) {
			speed = 0;
		}
	}
	if (channel == 1) {
		acpi_call(ACPI_EC_METHOD_KRFS, 0, NULL, &speed);
		if (speed == 0x1E) {
			speed = 0;
		}
	}
	return speed;
}

static const int read_gfrm(int channel) {
	ssize_t rpm = 0;
	if (channel == 0) {
		acpi_call(ACPI_TZ_METHOD_GFRM, 0, NULL, &rpm);
	}
	return rpm;
}

static const ssize_t ctrl_sfsd(int channel, unsigned long val) {
	ssize_t res = -1;
	if (val > 100) {
		val = 100;
	}
	if (channel == 0) {
		union acpi_object *arg = kmalloc(sizeof(union acpi_object), GFP_KERNEL);
		arg->type = ACPI_TYPE_INTEGER;
		arg->integer.value = val;
		acpi_call(ACPI_EC_METHOD_SFSD, 1, arg, &res);
		kfree(arg);
	}
	return res;
}

static ssize_t set_input(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	int channel = to_sensor_dev_attr(attr)->index;
	int err;
	ssize_t val;
	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	
	switch (ctrltype) {
	case HP_FANCTRL_SFSD:
		ctrl_sfsd(channel, val);
		break;
	case HP_FANCTRL_KSFS:
	case HP_FANCTRL_STMM:
	case HP_FANCTRL_KFCL:
	case HP_FANCTRL_I2CC:
	case HP_FANCTRL_AUTO:
	default:
		break;
	}

	return count;
}

static ssize_t get_input(struct device *dev, struct device_attribute *attr, char *buf) {
	int channel = to_sensor_dev_attr(attr)->index;
	ssize_t rpm = 0;
	switch (readtype) {
	case HP_FANREAD_GFRM:
		rpm = read_gfrm(channel);
		break;
	case HP_FANREAD_GFVE:
		rpm = read_gfve(channel);
		break;
	case HP_FANREAD_GFSD:
		rpm = read_gfsd(channel);
		break;
	case HP_FANREAD_KGFS:
		rpm = read_kgfs(channel);
		break;
	case HP_FANREAD_NONE:
	default:
		break;
	}
	return sprintf(buf, "%ld\n", rpm);
}

static ssize_t get_max(struct device *dev, struct device_attribute *attr, char *buf) {
	ssize_t maxspeed = 0;
	
	acpi_call(ACPI_TZ_VALUE_MRPM, 0, NULL, &maxspeed);
	
	return sprintf(buf, "%ld\n", maxspeed);
}

static ssize_t show_label(struct device *dev, struct device_attribute *attr, char *buf) {
	int channel = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "Fan %i\n", channel);
}

static struct platform_device *hp_fan_device;

static void try_detect_readtype(void) {
	if (acpi_exists(ACPI_TZ_METHOD_GFVE)) 
		readtype = HP_FANREAD_GFVE;
	else if (acpi_exists(ACPI_TZ_METHOD_GFRM))
		readtype = HP_FANREAD_GFRM;
	else if (acpi_exists(ACPI_EC_METHOD_GFSD))
		readtype = HP_FANREAD_GFSD;
	else if (acpi_exists(ACPI_EC_METHOD_KGFS))
		readtype = HP_FANREAD_KGFS;
}

static void try_detect_ctrltype(void) {

}

static int has_second_fan (void) {
	if (acpi_exists(ACPI_TZ_VALUE_FR2C)) {
		return 1;
	}
	return 0;
}

static SENSOR_DEVICE_ATTR(fan1_input, 0644, get_input, set_input, 0);
static SENSOR_DEVICE_ATTR(fan1_label, 0444, show_label, NULL, 0);
// static SENSOR_DEVICE_ATTR(fan1_target, 0444, get_target, NULL, 0);
// static SENSOR_DEVICE_ATTR(fan1_max, 0444, get_max, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, 0644, get_input, set_input, 1);
static SENSOR_DEVICE_ATTR(fan2_label, 0444, show_label, NULL, 1);
// static SENSOR_DEVICE_ATTR(fan2_target, 0444, get_target, NULL, 1);
// static SENSOR_DEVICE_ATTR(fan2_max, 0444, get_max, NULL, 1);
static struct attribute *fan_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_label.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_label.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fan);

static int hp_fan_probe(struct platform_device *pdev) {
	struct driver_data *data;

	data = kzalloc(sizeof(struct driver_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (!readtype){
		if (debug) 
			printk("No readtype specified. Trying auto-detection.\n");
		try_detect_readtype();
	}
	
	if (!ctrltype){
		if (debug) 
			printk("No ctrltype specified. Trying auto-detection.\n");
		try_detect_ctrltype();
	}

	data->hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev, "hp_fan", data, fan_groups);
	
	if (debug) 
		printk("hp fan driver initialized.\n");
	
	return 0;
}

static int hp_fan_remove(struct platform_device *pdev) {
	return 0;
}

static struct platform_driver hp_fan_driver = {
	.probe = hp_fan_probe,
	.remove = hp_fan_remove,
	.driver = {
		   .name = "hp_fan",
		   .owner = THIS_MODULE
		   },
};

static int __init hp_fan_init_module(void) {
	hp_fan_device = platform_device_register_simple("hp_fan", -1, NULL, 0);
	return platform_driver_probe(&hp_fan_driver, hp_fan_probe);
}

static void __exit hp_fan_cleanup_module(void) {
	if (debug) 
		printk("hp fan driver unloading.\n");
	
	platform_device_unregister(hp_fan_device);
	platform_driver_unregister(&hp_fan_driver);
}

module_init(hp_fan_init_module);
module_exit(hp_fan_cleanup_module);

MODULE_AUTHOR("Cristian Chelu <cristianchelu@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("HWMON ACPI HP laptop fan interface driver");
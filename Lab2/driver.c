// SPDX-License-Identifier: GPL-2.0
/*
 * Overbug Lab 2 GPIO button driver.
 *
 * This module exposes a simple character device:
 *
 *   /dev/overbug_buttons
 *
 * Each read returns one ASCII line:
 *
 *   OBTN XX\n
 *
 * where XX is a 6-bit button mask in hexadecimal.
 */

#include <linux/fs.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "overbug_buttons"

#define OB_BTN_UP      0x01
#define OB_BTN_DOWN    0x02
#define OB_BTN_LEFT    0x04
#define OB_BTN_RIGHT   0x08
#define OB_BTN_ACTION  0x10
#define OB_BTN_SOLDER  0x20

struct overbug_button {
	const char *name;
	int gpio;
	u8 mask;
	struct gpio_desc *desc;
};

static int gpio_up = 27;
static int gpio_down = 24;
static int gpio_left = 23;
static int gpio_right = 25;
static int gpio_action = 17;
static int gpio_solder = 22;
static int active_low = 1;
static int pull_up = 1;
static char *gpiochip_label = "pinctrl-bcm2711";

module_param(gpio_up, int, 0444);
MODULE_PARM_DESC(gpio_up, "BCM GPIO number for UP button");
module_param(gpio_down, int, 0444);
MODULE_PARM_DESC(gpio_down, "BCM GPIO number for DOWN button");
module_param(gpio_left, int, 0444);
MODULE_PARM_DESC(gpio_left, "BCM GPIO number for LEFT button");
module_param(gpio_right, int, 0444);
MODULE_PARM_DESC(gpio_right, "BCM GPIO number for RIGHT button");
module_param(gpio_action, int, 0444);
MODULE_PARM_DESC(gpio_action, "BCM GPIO number for ACTION button");
module_param(gpio_solder, int, 0444);
MODULE_PARM_DESC(gpio_solder, "BCM GPIO number for SOLDER button");
module_param(active_low, int, 0444);
MODULE_PARM_DESC(active_low, "Set to 1 when buttons connect GPIO to GND");
module_param(pull_up, int, 0444);
MODULE_PARM_DESC(pull_up, "Set to 1 to request internal pull-up bias");
module_param(gpiochip_label, charp, 0444);
MODULE_PARM_DESC(gpiochip_label, "GPIO chip label, for example pinctrl-bcm2711");

static struct overbug_button buttons[6];
static struct gpio_device *overbug_gdev;

static int overbug_match_gpiochip_contains(struct gpio_chip *chip, const void *data)
{
	const char *needle = data;

	return chip->label != NULL && strstr(chip->label, needle) != NULL;
}

static struct gpio_chip *overbug_find_gpiochip(void)
{
	struct gpio_device *gdev;

	gdev = gpio_device_find_by_label(gpiochip_label);
	if (gdev == NULL) {
		/*
		 * Raspberry Pi 4 usually reports "pinctrl-bcm2711". This fallback
		 * keeps the lab usable if the exact label changes slightly.
		 */
		gdev = gpio_device_find("pinctrl", overbug_match_gpiochip_contains);
	}

	if (gdev == NULL)
		return NULL;

	overbug_gdev = gdev;
	return gpio_device_get_chip(gdev);
}

static u8 overbug_read_mask(void)
{
	u8 mask = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(buttons); ++i) {
		int value = gpiod_get_value(buttons[i].desc);
		bool pressed = active_low ? (value == 0) : (value != 0);

		if (pressed)
			mask |= buttons[i].mask;
	}

	return mask;
}

static ssize_t overbug_buttons_read(
	struct file *file,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	char line[16];
	int len;
	u8 mask;

	(void)file;
	(void)ppos;

	mask = overbug_read_mask();
	len = scnprintf(line, sizeof(line), "OBTN %02X\n", mask);

	if (count < len)
		return -EINVAL;
	if (copy_to_user(user_buf, line, len))
		return -EFAULT;

	return len;
}

static const struct file_operations overbug_fops = {
	.owner = THIS_MODULE,
	.read = overbug_buttons_read,
};

static struct miscdevice overbug_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &overbug_fops,
	.mode = 0444,
};

static void overbug_free_gpios(int count)
{
	int i;

	for (i = 0; i < count; ++i) {
		if (buttons[i].desc != NULL) {
			gpiochip_free_own_desc(buttons[i].desc);
			buttons[i].desc = NULL;
		}
	}
}

static int __init overbug_buttons_init(void)
{
	struct gpio_chip *chip;
	int ret;
	int i;

	buttons[0] = (struct overbug_button){ "overbug_up", gpio_up, OB_BTN_UP };
	buttons[1] = (struct overbug_button){ "overbug_down", gpio_down, OB_BTN_DOWN };
	buttons[2] = (struct overbug_button){ "overbug_left", gpio_left, OB_BTN_LEFT };
	buttons[3] = (struct overbug_button){ "overbug_right", gpio_right, OB_BTN_RIGHT };
	buttons[4] = (struct overbug_button){ "overbug_action", gpio_action, OB_BTN_ACTION };
	buttons[5] = (struct overbug_button){ "overbug_solder", gpio_solder, OB_BTN_SOLDER };

	chip = overbug_find_gpiochip();
	if (chip == NULL) {
		pr_err("overbug_buttons: could not find GPIO chip label '%s'\n",
		       gpiochip_label);
		return -ENODEV;
	}
	pr_info("overbug_buttons: using gpiochip '%s'\n", chip->label);

	for (i = 0; i < ARRAY_SIZE(buttons); ++i) {
		enum gpio_lookup_flags lookup_flags = GPIO_ACTIVE_HIGH;

		if (pull_up)
			lookup_flags |= GPIO_PULL_UP;

		buttons[i].desc = gpiochip_request_own_desc(
			chip, buttons[i].gpio, buttons[i].name, lookup_flags, GPIOD_IN);
		if (IS_ERR(buttons[i].desc)) {
			ret = PTR_ERR(buttons[i].desc);
			buttons[i].desc = NULL;
			pr_err("overbug_buttons: failed to request GPIO line %d (%s): %d\n",
			       buttons[i].gpio, buttons[i].name, ret);
			goto fail_gpio;
		}

		/*
		 * Some recent Raspberry Pi kernels no longer expose the legacy
		 * gpio_set_debounce() helper to modules. Lab 2 keeps the driver
		 * simple and lets reader.c sample at a steady rate instead.
		 */
	}

	ret = misc_register(&overbug_miscdev);
	if (ret) {
		pr_err("overbug_buttons: failed to register /dev/%s: %d\n",
		       DEVICE_NAME, ret);
		goto fail_gpio;
	}

	pr_info("overbug_buttons: registered /dev/%s, active_low=%d, pull_up=%d\n",
		DEVICE_NAME, active_low, pull_up);
	return 0;

fail_gpio:
	overbug_free_gpios(i);
	if (overbug_gdev != NULL) {
		gpio_device_put(overbug_gdev);
		overbug_gdev = NULL;
	}
	return ret;
}

static void __exit overbug_buttons_exit(void)
{
	misc_deregister(&overbug_miscdev);
	overbug_free_gpios(ARRAY_SIZE(buttons));
	if (overbug_gdev != NULL) {
		gpio_device_put(overbug_gdev);
		overbug_gdev = NULL;
	}
	pr_info("overbug_buttons: unloaded\n");
}

module_init(overbug_buttons_init);
module_exit(overbug_buttons_exit);

MODULE_AUTHOR("Overbug Lab 2");
MODULE_DESCRIPTION("GPIO device-state reader for Overbug");
MODULE_LICENSE("GPL");

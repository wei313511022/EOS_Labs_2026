// SPDX-License-Identifier: GPL-2.0
/*
 * Overbug Lab 4 GPIO button driver.
 *
 * This is the interrupt-driven successor to Lab2/driver.c.  The device still
 * exposes /dev/overbug_buttons and returns one "OBTN XX\n" state line, but a
 * read now waits for a debounced state change.  A reader that enables O_ASYNC
 * receives SIGIO after the driver has stored the new state.
 */

#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define DEVICE_NAME "overbug_buttons"
#define BUTTON_COUNT 6

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
	int irq;
};

static int gpio_up = 27;
static int gpio_down = 24;
static int gpio_left = 23;
static int gpio_right = 25;
static int gpio_action = 17;
static int gpio_solder = 22;
static int active_low = 1;
static int pull_up = 1;
static int debounce_ms = 20;
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
module_param(debounce_ms, int, 0444);
MODULE_PARM_DESC(debounce_ms, "Debounce delay in milliseconds (default: 20)");
module_param(gpiochip_label, charp, 0444);
MODULE_PARM_DESC(gpiochip_label, "GPIO chip label, for example pinctrl-bcm2711");

static struct overbug_button buttons[BUTTON_COUNT];
static struct gpio_device *overbug_gdev;
static DEFINE_SPINLOCK(state_lock);
static DECLARE_WAIT_QUEUE_HEAD(button_waitq);
static struct fasync_struct *async_queue;
static struct timer_list debounce_timer;
static atomic_t device_open = ATOMIC_INIT(0);
static u8 stable_mask;
static bool state_ready;
static bool irq_ready;

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
		/* Raspberry Pi labels vary slightly across kernel versions. */
		gdev = gpio_device_find("pinctrl", overbug_match_gpiochip_contains);
	}
	if (gdev == NULL)
		return NULL;

	overbug_gdev = gdev;
	return gpio_device_get_chip(gdev);
}

/* This must be called only for GPIO controllers that do not sleep. */
static u8 overbug_read_mask(void)
{
	u8 mask = 0;
	int i;

	for (i = 0; i < BUTTON_COUNT; ++i) {
		int value = gpiod_get_value(buttons[i].desc);
		bool pressed = active_low ? (value == 0) : (value != 0);

		if (pressed)
			mask |= buttons[i].mask;
	}
	return mask;
}

static void overbug_publish_mask(u8 mask)
{
	unsigned long flags;
	bool changed = false;

	spin_lock_irqsave(&state_lock, flags);
	if (mask != stable_mask) {
		stable_mask = mask;
		state_ready = true;
		changed = true;
	}
	spin_unlock_irqrestore(&state_lock, flags);

	if (!changed)
		return;

	/* State is visible before either userspace notification is delivered. */
	wake_up_interruptible(&button_waitq);
	kill_fasync(&async_queue, SIGIO, POLL_IN);
}

static void overbug_debounce_timer(struct timer_list *timer)
{
	(void)timer;
	overbug_publish_mask(overbug_read_mask());
}

static irqreturn_t overbug_button_irq(int irq, void *data)
{
	(void)irq;
	(void)data;

	/* Do not arm the timer until every GPIO descriptor has been requested. */
	if (!READ_ONCE(irq_ready))
		return IRQ_HANDLED;

	/* IRQ context is intentionally small: sample after the contacts settle. */
	mod_timer(&debounce_timer,
		  jiffies + msecs_to_jiffies(max(debounce_ms, 0)));
	return IRQ_HANDLED;
}

static int overbug_buttons_open(struct inode *inode, struct file *file)
{
	int ret;

	(void)inode;

	/* One controller process is expected on each Raspberry Pi. */
	if (atomic_cmpxchg(&device_open, 0, 1) != 0)
		return -EBUSY;
	ret = nonseekable_open(inode, file);
	if (ret)
		atomic_set(&device_open, 0);
	return ret;
}

static int overbug_buttons_fasync(int fd, struct file *file, int on)
{
	return fasync_helper(fd, file, on, &async_queue);
}

static int overbug_buttons_release(struct inode *inode, struct file *file)
{
	(void)inode;
	overbug_buttons_fasync(-1, file, 0);
	atomic_set(&device_open, 0);
	return 0;
}

static ssize_t overbug_buttons_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	char line[16];
	unsigned long flags;
	u8 mask;
	int len;
	int ret;

	(void)ppos;
	len = scnprintf(line, sizeof(line), "OBTN %02X\n", 0);
	if (count < len)
		return -EINVAL;

	for (;;) {
		spin_lock_irqsave(&state_lock, flags);
		if (state_ready) {
			mask = stable_mask;
			state_ready = false;
			spin_unlock_irqrestore(&state_lock, flags);
			break;
		}
		spin_unlock_irqrestore(&state_lock, flags);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(button_waitq, READ_ONCE(state_ready));
		if (ret)
			return ret;
	}

	len = scnprintf(line, sizeof(line), "OBTN %02X\n", mask);
	if (copy_to_user(user_buf, line, len))
		return -EFAULT;
	return len;
}

static __poll_t overbug_buttons_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &button_waitq, wait);
	if (READ_ONCE(state_ready))
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static const struct file_operations overbug_fops = {
	.owner = THIS_MODULE,
	.open = overbug_buttons_open,
	.release = overbug_buttons_release,
	.read = overbug_buttons_read,
	.poll = overbug_buttons_poll,
	.fasync = overbug_buttons_fasync,
};

static struct miscdevice overbug_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &overbug_fops,
	.mode = 0444,
};

static void overbug_free_irqs(int count)
{
	int i;

	for (i = 0; i < count; ++i) {
		if (buttons[i].irq >= 0) {
			free_irq(buttons[i].irq, &buttons[i]);
			buttons[i].irq = -1;
		}
	}
}

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

	if (debounce_ms < 0) {
		pr_err("overbug_buttons: debounce_ms must not be negative\n");
		return -EINVAL;
	}

	buttons[0] = (struct overbug_button){ "overbug_up", gpio_up, OB_BTN_UP, NULL, -1 };
	buttons[1] = (struct overbug_button){ "overbug_down", gpio_down, OB_BTN_DOWN, NULL, -1 };
	buttons[2] = (struct overbug_button){ "overbug_left", gpio_left, OB_BTN_LEFT, NULL, -1 };
	buttons[3] = (struct overbug_button){ "overbug_right", gpio_right, OB_BTN_RIGHT, NULL, -1 };
	buttons[4] = (struct overbug_button){ "overbug_action", gpio_action, OB_BTN_ACTION, NULL, -1 };
	buttons[5] = (struct overbug_button){ "overbug_solder", gpio_solder, OB_BTN_SOLDER, NULL, -1 };

	chip = overbug_find_gpiochip();
	if (chip == NULL) {
		pr_err("overbug_buttons: could not find GPIO chip label '%s'\n",
		       gpiochip_label);
		return -ENODEV;
	}
	pr_info("overbug_buttons: using gpiochip '%s'\n", chip->label);

	timer_setup(&debounce_timer, overbug_debounce_timer, 0);
	for (i = 0; i < BUTTON_COUNT; ++i) {
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
			goto fail_irq;
		}
		if (gpiod_cansleep(buttons[i].desc)) {
			pr_err("overbug_buttons: GPIO line %d can sleep; direct Raspberry Pi GPIO is required\n",
			       buttons[i].gpio);
			ret = -EOPNOTSUPP;
			goto fail_irq;
		}

		buttons[i].irq = gpiod_to_irq(buttons[i].desc);
		if (buttons[i].irq < 0) {
			ret = buttons[i].irq;
			pr_err("overbug_buttons: GPIO line %d has no IRQ: %d\n",
			       buttons[i].gpio, ret);
			buttons[i].irq = -1;
			goto fail_irq;
		}
		ret = request_irq(buttons[i].irq, overbug_button_irq,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				  buttons[i].name, &buttons[i]);
		if (ret) {
			pr_err("overbug_buttons: failed to request IRQ for GPIO %d: %d\n",
			       buttons[i].gpio, ret);
			buttons[i].irq = -1;
			goto fail_irq;
		}
	}

	stable_mask = overbug_read_mask();
	state_ready = false;
	ret = misc_register(&overbug_miscdev);
	if (ret) {
		pr_err("overbug_buttons: failed to register /dev/%s: %d\n",
		       DEVICE_NAME, ret);
		goto fail_irq;
	}
	WRITE_ONCE(irq_ready, true);

	pr_info("overbug_buttons: registered /dev/%s, debounce=%dms, active_low=%d, pull_up=%d\n",
		DEVICE_NAME, debounce_ms, active_low, pull_up);
	return 0;

fail_irq:
	WRITE_ONCE(irq_ready, false);
	overbug_free_irqs(BUTTON_COUNT);
	timer_shutdown_sync(&debounce_timer);
	overbug_free_gpios(BUTTON_COUNT);
	if (overbug_gdev != NULL) {
		gpio_device_put(overbug_gdev);
		overbug_gdev = NULL;
	}
	return ret;
}

static void __exit overbug_buttons_exit(void)
{
	misc_deregister(&overbug_miscdev);
	WRITE_ONCE(irq_ready, false);
	overbug_free_irqs(BUTTON_COUNT);
	timer_shutdown_sync(&debounce_timer);
	overbug_free_gpios(BUTTON_COUNT);
	async_queue = NULL;
	if (overbug_gdev != NULL) {
		gpio_device_put(overbug_gdev);
		overbug_gdev = NULL;
	}
	pr_info("overbug_buttons: unloaded\n");
}

module_init(overbug_buttons_init);
module_exit(overbug_buttons_exit);

MODULE_AUTHOR("Overbug Lab 4");
MODULE_DESCRIPTION("Interrupt-driven GPIO button device for Overbug");
MODULE_LICENSE("GPL");

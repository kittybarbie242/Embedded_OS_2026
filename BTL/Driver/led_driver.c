#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define LED_GPIO 18

static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    char user_data;
    if (copy_from_user(&user_data, buf, 1)) return -EFAULT;
    
    if (user_data == '1') gpio_set_value(LED_GPIO, 1);
    else if (user_data == '0') gpio_set_value(LED_GPIO, 0);
    return count;
}

static const struct file_operations led_fops = { .owner = THIS_MODULE, .write = led_write };
static struct miscdevice led_dev = { .minor = MISC_DYNAMIC_MINOR, .name = "my_led", .fops = &led_fops };

static int __init led_init(void) {
    gpio_request(LED_GPIO, "LED");
    gpio_direction_output(LED_GPIO, 0);
    misc_register(&led_dev);
    printk(KERN_INFO "LED Driver Init\n");
    return 0;
}

static void __exit led_exit(void) {
    misc_deregister(&led_dev);
    gpio_free(LED_GPIO);
    printk(KERN_INFO "LED Driver Exit\n");
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");

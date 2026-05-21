#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define RELAY_GPIO 28

static ssize_t relay_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    char user_data;
    if (copy_from_user(&user_data, buf, 1)) return -EFAULT;
    
    if (user_data == '1') gpio_set_value(RELAY_GPIO, 1);
    else if (user_data == '0') gpio_set_value(RELAY_GPIO, 0);
    return count;
}

static const struct file_operations relay_fops = { .owner = THIS_MODULE, .write = relay_write };
static struct miscdevice relay_dev = { .minor = MISC_DYNAMIC_MINOR, .name = "my_relay", .fops = &relay_fops };

static int __init relay_init(void) {
    gpio_request(RELAY_GPIO, "Relay");
    gpio_direction_output(RELAY_GPIO, 0); // Mặc định tắt
    misc_register(&relay_dev);
    printk(KERN_INFO "Relay Driver Init\n");
    return 0;
}

static void __exit relay_exit(void) {
    misc_deregister(&relay_dev);
    gpio_free(RELAY_GPIO);
    printk(KERN_INFO "Relay Driver Exit\n");
}

module_init(relay_init);
module_exit(relay_exit);
MODULE_LICENSE("GPL");

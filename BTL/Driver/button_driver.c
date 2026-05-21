#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define BUTTON_GPIO 16

static ssize_t button_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    char state;
    int btn_val;

    if (*ppos > 0) return 0;

    btn_val = gpio_get_value(BUTTON_GPIO);
    state = btn_val ? '1' : '0'; // Nhấn thì 0, nhả thì 1 (Active Low)
    
    if (copy_to_user(buf, &state, 1)) return -EFAULT;
    
    *ppos += 1; // Cập nhật vị trí con trỏ báo là đã đọc 1 byte
    return 1;   // Trả về số byte đã đọc
}

static const struct file_operations button_fops = { .owner = THIS_MODULE, .read = button_read };
static struct miscdevice button_dev = { .minor = MISC_DYNAMIC_MINOR, .name = "my_button", .fops = &button_fops };

static int __init button_init(void) {
    gpio_request(BUTTON_GPIO, "Button");
    gpio_direction_input(BUTTON_GPIO);
    misc_register(&button_dev);
    printk(KERN_INFO "Button Driver Init\n");
    return 0;
}

static void __exit button_exit(void) {
    misc_deregister(&button_dev);
    gpio_free(BUTTON_GPIO);
    printk(KERN_INFO "Button Driver Exit\n");
}

module_init(button_init);
module_exit(button_exit);
MODULE_LICENSE("GPL");

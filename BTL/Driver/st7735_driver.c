#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h> // Thư viện điều khiển chân DC, RST
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>   
#include <linux/slab.h>

#define DEVICE_NAME "tft_st7735"
#define CLASS_NAME  "st7735_class"
#define TFT_WIDTH   128
#define TFT_HEIGHT  160

static struct spi_device *tft_spi_device = NULL;
static struct gpio_desc *dc_gpio = NULL;
static struct gpio_desc *rst_gpio = NULL;

static int major_number;
static struct class* tft_class  = NULL;
static struct device* tft_device = NULL;

/* 1. Hàm Gửi Lệnh và Dữ Liệu qua SPI */
static void st7735_write_cmd(uint8_t cmd) {
    gpiod_set_value(dc_gpio, 0); // DC = 0: Gửi Lệnh
    spi_write(tft_spi_device, &cmd, 1);
}

static void st7735_write_data(uint8_t data) {
    gpiod_set_value(dc_gpio, 1); // DC = 1: Gửi Dữ liệu
    spi_write(tft_spi_device, &data, 1);
}

/* 2. Cài đặt khung viền vẽ (Address Window) */
static void st7735_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    st7735_write_cmd(0x2A); 
    st7735_write_data(0x00);
    st7735_write_data(x0);
    st7735_write_data(0x00);
    st7735_write_data(x1);

    st7735_write_cmd(0x2B); 
    st7735_write_data(0x00);
    st7735_write_data(y0);
    st7735_write_data(0x00);
    st7735_write_data(y1);

    st7735_write_cmd(0x2C); 
}

/* 3. Chuỗi Khởi tạo ST7735 */
static void st7735_init(void) {
    pr_info("ST7735: Bat dau khoi tao...\n");

    // Reset cứng (Hardware Reset)
    gpiod_set_value(rst_gpio, 1); msleep(10);
    gpiod_set_value(rst_gpio, 0); msleep(50);
    gpiod_set_value(rst_gpio, 1); msleep(120);

    // Chuỗi lệnh chuẩn của ST7735
    st7735_write_cmd(0x01); msleep(150); // Software Reset
    st7735_write_cmd(0x11); msleep(500); // Sleep Out

    st7735_write_cmd(0x3A); // Color Mode
    st7735_write_data(0x05); // 16-bit color (RGB565)

    st7735_write_cmd(0x36); // Memory Data Access Control (Xoay màn hình)
    st7735_write_data(0x00); // Tùy chỉnh hướng hiển thị (A0, 60, C0...)

    st7735_write_cmd(0x29); msleep(100); // Display ON
    
    pr_info("ST7735: Khoi tao hoan tat.\n");
}

/* 4. Giao diện nhận Dữ liệu khung hình (Frame) từ User-space */
static ssize_t tft_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset) {
    uint8_t *kbuf;
    size_t bytes_sent = 0;
    size_t chunk_size = 512; // Băm nhỏ 4KB mỗi nhịp để bảo vệ SPI Controller

    if (count > (TFT_WIDTH * TFT_HEIGHT * 2)) count = (TFT_WIDTH * TFT_HEIGHT * 2);

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    // Set window tràn màn hình
    st7735_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    gpiod_set_value(dc_gpio, 1); // DC = 1 (Data)
    
    // Gửi dữ liệu theo từng cục nhỏ (Chunking)
    while (bytes_sent < count) {
        size_t send_len = (count - bytes_sent < chunk_size) ? (count - bytes_sent) : chunk_size;
        int ret = spi_write(tft_spi_device, kbuf + bytes_sent, send_len);
        if (ret) {
            pr_err("ST7735: Loi SPI Write o byte %zu\n", bytes_sent);
            break;
        }
        bytes_sent += send_len;
    }

    kfree(kbuf);
    return count;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = tft_dev_write,
};

/* 5. Hàm Probe: Chạy khi Device Tree khớp */
static int st7735_probe(struct spi_device *spi) {
    pr_info("ST7735: Probing SPI device...\n");
    tft_spi_device = spi;

    // A. Bắt lấy chân DC và RST từ Device Tree
    dc_gpio = gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
    if (IS_ERR(dc_gpio)) {
        pr_err("ST7735: Khong the lay chan DC GPIO\n");
        return PTR_ERR(dc_gpio);
    }

    rst_gpio = gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(rst_gpio)) {
        pr_err("ST7735: Khong the lay chan RESET GPIO\n");
        gpiod_put(dc_gpio);
        return PTR_ERR(rst_gpio);
    }

    // B. Đăng ký Character Device
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    tft_class = class_create(THIS_MODULE, CLASS_NAME);
    tft_device = device_create(tft_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);

    // C. Khởi tạo màn hình
    st7735_init();

    // D. Xóa màn hình thành màu đen lúc khởi động (tùy chọn)
    st7735_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    // (Bỏ qua thao tác tô đen trong Kernel để khởi động nhanh hơn, dành việc đó cho App C)

    pr_info("ST7735: Driver load thanh cong. File: /dev/%s\n", DEVICE_NAME);
    return 0;
}

static void st7735_remove(struct spi_device *spi) {
    device_destroy(tft_class, MKDEV(major_number, 0));
    class_unregister(tft_class);
    class_destroy(tft_class);
    unregister_chrdev(major_number, DEVICE_NAME);

    // Giải phóng GPIO
    gpiod_put(dc_gpio);
    gpiod_put(rst_gpio);

    pr_info("ST7735: Driver unloaded.\n");
}

static const struct of_device_id st7735_of_match[] = {
    { .compatible = "custom,st7735", },
    { },
};
MODULE_DEVICE_TABLE(of, st7735_of_match);

static struct spi_driver st7735_spi_driver = {
    .driver = {
        .name = "st7735_tft",
        .of_match_table = st7735_of_match,
    },
    .probe = st7735_probe,
    .remove = st7735_remove,
};

module_spi_driver(st7735_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("txt");
MODULE_DESCRIPTION("SPI Driver for ST7735 TFT");

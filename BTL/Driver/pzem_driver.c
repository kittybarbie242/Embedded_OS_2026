#include <linux/module.h>
#include <linux/serdev.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/string.h>

#define DEVICE_NAME "pzem_sensor"
#define CLASS_NAME  "pzem"

static int major;
static struct class* pzem_class  = NULL;
static struct device* pzem_device = NULL;
static struct serdev_device *pzem_serdev = NULL;

static char message[128] = "No data yet\n";

// ---------------------------------------------------
// VŨ KHÍ MỚI: BỘ ĐỆM GOM DỮ LIỆU CHỐNG PHÂN MẢNH
// ---------------------------------------------------
static unsigned char rx_buffer[64];
static int rx_index = 0;

static uint16_t modbus_crc(const uint8_t *buf, uint8_t len) {
    uint16_t crc = 0xFFFF;
    uint8_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (j = 0; j < 8; j++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

// 1. Hàm nhận dữ liệu từ UART (Gom từng byte một)
static int pzem_receive_buf(struct serdev_device *serdev, const unsigned char *buf, size_t len) {
    int i;
    uint16_t crc_calc, crc_recv;
    uint16_t v_val, f_val; // THÊM BIẾN f_val ĐỂ LƯU TẦN SỐ
    uint32_t i_val, p_val;

    for (i = 0; i < len; i++) {
        // ĐỒNG BỘ: Bắt buộc byte đầu tiên phải là 0x01 (Địa chỉ Slave)
        if (rx_index == 0 && buf[i] != 0x01) {
            continue; // Rác -> Bỏ qua
        }

        // Nhặt byte bỏ vào giỏ
        rx_buffer[rx_index++] = buf[i];

        // Nếu đã nhặt đủ 25 bytes
        if (rx_index >= 25) {
            // Kiểm tra mã hàm phản hồi 0x04
            if (rx_buffer[1] == 0x04) {
                crc_calc = modbus_crc(rx_buffer, 23);
                crc_recv = rx_buffer[23] | (rx_buffer[24] << 8);

                if (crc_calc == crc_recv) {
                    v_val = (rx_buffer[3] << 8) | rx_buffer[4];
                    i_val = ((uint32_t)rx_buffer[7] << 24) | ((uint32_t)rx_buffer[8] << 16) | 
                            ((uint32_t)rx_buffer[5] << 8)  | rx_buffer[6];
                    p_val = ((uint32_t)rx_buffer[11] << 24) | ((uint32_t)rx_buffer[12] << 16) | 
                            ((uint32_t)rx_buffer[9] << 8)  | rx_buffer[10];
                            
                    // BỔ SUNG: Đọc byte 17 và 18 để lấy Tần số (Hz)
                    f_val = (rx_buffer[17] << 8) | rx_buffer[18];

                    // CHÍNH XÁC HÓA ĐỊNH DẠNG: Thêm F: %u.%uHz và sửa I thành %u.%03uA
                    snprintf(message, sizeof(message), 
                             "U: %u.%uV | I: %u.%03uA | P: %u.%uW | F: %u.%uHz\n", 
                             v_val / 10, v_val % 10, 
                             i_val / 1000, i_val % 1000,
                             p_val / 10, p_val % 10,
                             f_val / 10, f_val % 10);
                }
            }
            // Giải mã xong, đổ rác để đón chu kỳ đo mới
            rx_index = 0;
        }

        // Chống tràn giỏ nếu bị nhiễu đường truyền
        if (rx_index >= sizeof(rx_buffer)) {
            rx_index = 0;
        }
    }
    return len; // Luôn báo kernel là đã xử lý xong 'len' bytes
}

static const struct serdev_device_ops pzem_ops = {
    .receive_buf = pzem_receive_buf,
};

// 2. Hàm đọc từ User-space
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int error_count = 0;
    size_t datalen;
    unsigned char cmd[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x0A, 0x70, 0x0D};

    if (*offset > 0) return 0;

    if (pzem_serdev == NULL) return -EIO;

    strcpy(message, "Mat ket noi voi PZEM hoac mat dien 220V!\n");

    if (pzem_serdev) {
        // CỰC KỲ QUAN TRỌNG: Quét sạch giỏ trước khi gửi lệnh hỏi thăm mới
        rx_index = 0; 
        serdev_device_write_buf(pzem_serdev, cmd, sizeof(cmd));
        msleep(300); 
    } 

    datalen = strlen(message);
    error_count = copy_to_user(buffer, message, datalen);
    if (error_count == 0) {
        *offset = datalen;
        return datalen;
    } else return -EFAULT;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
};

static int pzem_probe(struct serdev_device *serdev) {
    int status;
    pzem_serdev = serdev;

    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) return major;

    pzem_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(pzem_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(pzem_class);
    }

    pzem_device = device_create(pzem_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(pzem_device)) {
        class_destroy(pzem_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(pzem_device);
    }

    serdev_device_set_client_ops(serdev, &pzem_ops);
    status = serdev_device_open(serdev);
    if (status) {
        device_destroy(pzem_class, MKDEV(major, 0));
        class_destroy(pzem_class);
        unregister_chrdev(major, DEVICE_NAME);
        return status;
    }

    serdev_device_set_baudrate(serdev, 9600);
    serdev_device_set_flow_control(serdev, false);

    return 0;
}

static void pzem_remove(struct serdev_device *serdev) {
    serdev_device_close(serdev);
    if (pzem_class) {
        device_destroy(pzem_class, MKDEV(major, 0));
        class_destroy(pzem_class);
    }
    if (major > 0) unregister_chrdev(major, DEVICE_NAME);
}

static const struct of_device_id pzem_ids[] = {
    { .compatible = "my-pzem,pzem004t", },
    { }
};
MODULE_DEVICE_TABLE(of, pzem_ids);

static struct serdev_device_driver pzem_driver = {
    .probe = pzem_probe,
    .remove = pzem_remove,
    .driver = {
        .name = "pzem-driver",
        .of_match_table = pzem_ids,
    },
};

module_serdev_device_driver(pzem_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("txt");
MODULE_DESCRIPTION("Driver for PZEM-004T (Fix Fragment Bug)");

# Bài 1: Biên Dịch Và Cài Đặt UBoot

## 1. Mục tiêu

- Xây dựng Bootloader: Biên dịch thành công mã nguồn U-Boot phiên bản v2024.04 

- Quản lý Toolchain: Sử dụng bộ công cụ Cross-compiler để biên dịch mã nguồn cho kiến trúc ARM trên máy chủ X86.

- Quản lý bộ nhớ: Thực hiện phân vùng thẻ nhớ (SD Card).

- Hiểu quy trình khởi động của BeagleBone Black (S2 Button) và giao tiếp qua cổng Serial UART.

---

## 2. Quá trình thực hiện
### Bước 1: Chuẩn bị môi trường và Tải nguồn
- Thêm toolchain vào biến môi trường
``` 
export PATH=$PATH:~/x-tools/arm-cortex_a8-linux-gnueabi/bin/ 
```
- Tạo thư mục làm việc và tải source code
```
mkdir -p ~/workspace/bootloader
cd ~/workspace/bootloader
git clone https://github.com/u-boot/u-boot.git
cd u-boot
git checkout v2024.04
```
### Bước 2: Biên dịch U-Boot
Cài đặt các thư viện cần thiết và tiến hành biên dịch để tạo ra 2 file MLO và u-boot.img.
- Thiết lập trình biên dịch chéo
```
export CROSS_COMPILE=arm-cortex_a8-linux-gnueabi-
```
- Cài đặt các package hỗ trợ biên dịch
```
sudo apt update
sudo apt install libssl-dev device-tree-compiler swig python3-dev python3-setuptools bison flex
```
- Cấu hình và biên dịch cho BeagleBone Black
```
make am335x_evm_defconfig
make DEVICE_TREE=am335x-boneblack -j$(nproc)
```
- Kiểm tra kết quả biên dịch
```
ls -l MLO u-boot.img
```
### Bước 3: Tạo phân vùng và Nạp thẻ nhớ
Sử dụng cfdisk để định dạng thẻ nhớ.
- Phân vùng: Tạo 1 phân vùng Primary, kích thước 64MB.
- Bootable: Đánh dấu cờ [ Bootable ] cho phân vùng này (Bắt buộc).
- Định dạng: Chọn Type là W95 FAT32 (LBA).

Format & Copy:
- Định dạng FAT32 với tên nhãn là "boot"
```
sudo mkfs.vfat -F 32 -n boot /dev/sdb1
```
- Gắn thẻ nhớ và chép file
```
sudo mkdir -p /mnt/sdcard
sudo mount /dev/sdb1 /mnt/sdcard
sudo cp MLO u-boot.img /mnt/sdcard/
sync
sudo umount /mnt/sdcard
```
### Bước 4: Khởi động và Kiểm tra 
Kết nối USB-to-TTL vào hàng chân J1 trên BeagleBone Black và theo dõi quá trình boot.
- Kết nối UART: Baudrate 115200.
- Thao tác vật lý: Nhấn giữ nút S2 (USER) và cấp nguồn để ép board đọc dữ liệu từ thẻ nhớ thay vì bộ nhớ trong (eMMC).
## 3. Kết quả đạt được
Khi U-Boot khởi động thành công, hệ thống sẽ dừng tại dấu nhắc lệnh =>.  Nhập các lệnh sau để kiểm tra:
- **version**: Kiểm tra phiên bản và ngày giờ biên dịch (để xác nhận file mới nạp).
- **bdinfo**: Hiển thị thông tin chi tiết về phần cứng (RAM, địa chỉ IP, kiến trúc chip).
- **printenv**: Xem các biến môi trường cấu hình cho quá trình khởi động.
# Bài 2: 

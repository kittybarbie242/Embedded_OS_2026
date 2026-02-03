# Bài 1: BIÊN DỊCH VÀ CÀI ĐẶT UBOOT

## 1. Mục tiêu

- Xây dựng Bootloader: Biên dịch thành công mã nguồn U-Boot phiên bản v2024.04 

- Quản lý Toolchain: Sử dụng bộ công cụ Cross-compiler để biên dịch mã nguồn cho kiến trúc ARM trên máy chủ X86.

- Quản lý bộ nhớ: Thực hiện phân vùng thẻ nhớ (SD Card) .

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
Khi U-Boot khởi động thành công, hệ thống sẽ dừng tại dấu nhắc lệnh **=>**.  Nhập các lệnh sau để kiểm tra:
- **version**: Kiểm tra phiên bản và ngày giờ biên dịch (để xác nhận file mới nạp).
- **bdinfo**: Hiển thị thông tin chi tiết về phần cứng (RAM, địa chỉ IP, kiến trúc chip).
- **printenv**: Xem các biến môi trường cấu hình cho quá trình khởi động.
# Bài 2: BIÊN DỊCH VÀ CÀI ĐẶT KERNEL
## 1. Mục tiêu
- Biên dịch  Kernel: Xây dựng Linux Kernel từ mã nguồn chuẩn phiên bản ổn định 6.6.y.

- Cấu hình Device Tree: Biên dịch file cấu hình phần cứng (.dtb) để Kernel hiểu và điều khiển được các thành phần trên Board BeagleBone Black.

- Quản lý bộ nhớ RAM: Hiểu cách nạp các thành phần hệ điều hành vào các vùng địa chỉ khác nhau trên RAM thông qua U-Boot.

- Xác nhận quá trình Boot
## 2. Quy trình thực hiện
### Bước 1: Chuẩn bị mã nguồn và Môi trường
- Tạo đường dẫn mới
```
cd ~/workspace
mkdir -p kernel
cd kernel
```
- Tải mã nguồn Kernel phiên bản 6.6.y
```
git clone --depth 1 -b linux-6.6.y https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
```
```
cd linux
```
- Thiết lập biến môi trường
```
export PATH=$HOME/x-tools/arm-cortex_a8-linux-gnueabi/bin:$PATH
export CROSS_COMPILE=arm-cortex_a8-linux-gnueabi-
export ARCH=arm
```
### Bước 2: Cấu hình và Biên dịch
- Sử dụng cấu hình mặc định cho các dòng chip AM335x
```
make omap2plus_defconfig 
```
- Biên dịch Kernel (zImage) và Device Tree Blobs (dtbs) với tối đa số CPU
```
make -j$(nproc) zImage dtbs
```
- Kiểm tra kết quả biên dịch
```
ls -l arch/arm/boot/zImage arch/arm/boot/dts/ti/omap/am335x-boneblack.dtb
```
### Bước 3: Cài đặt vào Thẻ nhớ (SD Card)
Copy các file vừa biên dịch vào phân vùng boot của thẻ nhớ (chung chỗ với MLO và u-boot.img đã làm ở bài 1).
- Kiểm tra vị trí mount của thẻ nhớ
```
lsblk
```
- Copy Kernel và Device Tree vào thẻ
```
cp arch/arm/boot/zImage /media/txt/boot/
cp arch/arm/boot/dts/ti/omap/am335x-boneblack.dtb /media/txt/boot/
```
- Kiểm tra lại các file trong thẻ : MLO, u-boot.img, zImage, am335x-boneblack.dtb
```
ls /media/txt/boot/ 
```
- Đảm bảo dữ liệu đã ghi hoàn tất và gỡ thẻ an toàn
```
sync
sudo umount /media/txt/boot
```
### Bước 4: Khởi động Kernel trên BeagleBone Black
Cắm lại thẻ nhớ vào Board, thực hiện lại thao tác nhấn giữ nút S2 và cấp nguồn và chờ cho đến khi hiện thị dấu lệnh **=>** (tương tự như đã làm ở bài 1). Sau đó thực hiện quá trình nạp vào RAM.
- Nạp Kernel vào RAM: ***load mmc 0:1 0x82000000 zImage***
- Nạp Device Tree vào RAM: ***load mmc 0:1 0x88000000 am335x-boneblack.dtb***
- Khởi động nhân Linux: ***bootz 0x82000000 - 0x88000000***

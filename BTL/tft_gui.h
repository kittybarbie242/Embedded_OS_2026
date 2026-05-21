#ifndef TFT_GUI_H
#define TFT_GUI_H

#include <stdint.h>

// Bảng màu RGB565
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF

// Các hàm giao tiếp GUI
int tft_init(const char* dev_path);
void tft_close(void);
void tft_fill_screen(uint16_t color);
void tft_draw_string(int x, int y, const char *str, uint16_t color, uint16_t bg, uint8_t scale);
void tft_update(void);

#endif

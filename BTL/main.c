#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <mosquitto.h>
#include "tft_gui.h"

// ========================================================
// CẤU HÌNH BLYNK (HYBRID: MQTT + HTTP)
// ========================================================
#define BLYNK_TOKEN  "wnDIzmJiOS3PpQ9UwwrhIJlHwDr4dTGj"
#define BLYNK_SERVER "sgp1.blynk.cloud"

// Biến toàn cục hệ thống (IPC)
float current_U = 0.0, current_I = 0.0, current_P = 0.0, current_Hz = 0.0;
double current_E_Ws = 0.0;
int pzem_connected = 1;  
int relay_state = 1;     
int alarm_flag = 0;      
int current_led_state = 0; // Biến mới: Theo dõi trạng thái thực tế của LED

float max_voltage = 250.0; 
float max_current = 5.0; 

int retry_count = 0;     
int trip_timer = 0;      
int safe_timer = 0;      
int lockout = 0;         
int force_redraw = 0;    

pthread_mutex_t lock;
struct mosquitto *global_mosq = NULL;

void write_device(const char* path, const char* val) {
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, val, 1); close(fd); }
}

// ---------------------------------------------------------
// XỬ LÝ NGẮT (GRACEFUL SHUTDOWN)
// ---------------------------------------------------------
void handle_sigint(int sig) {
    printf("\n[SYSTEM] Dang dong chuong trinh va don dep tai nguyen...\n");
    if (global_mosq) {
        mosquitto_disconnect(global_mosq);
        mosquitto_destroy(global_mosq);
        mosquitto_lib_cleanup();
    }
    tft_fill_screen(COLOR_BLACK);
    tft_draw_string(20, 70, "SHUTTING DOWN...", COLOR_RED, COLOR_BLACK, 1);
    tft_update();
    pthread_mutex_destroy(&lock);
    printf("[SYSTEM] Don dep hoan tat. Thoat an toan!\n");
    exit(0);
}

// ---------------------------------------------------------
// THREAD 1: Đọc PZEM & Xử lý logic Bảo vệ
// ---------------------------------------------------------
void* sensor_thread_func(void* arg) {
    char buf[128];
    while(1) {
        int fd_pzem = open("/dev/pzem_sensor", O_RDONLY);
        if (fd_pzem >= 0) {
            memset(buf, 0, sizeof(buf));
            int bytes = read(fd_pzem, buf, sizeof(buf) - 1); 
            close(fd_pzem); 
            
            if (bytes > 0) {
                if (strstr(buf, "Mat ket noi") != NULL) {
                    pthread_mutex_lock(&lock);
                    if (pzem_connected != 0) force_redraw = 1; 
                    pzem_connected = 0;
                    current_U = 0.0; current_I = 0.0; current_P = 0.0; current_Hz = 0.0;
                    pthread_mutex_unlock(&lock);
                } 
                else {
                    int u_int, u_dec, i_int, i_dec, p_int, p_dec, f_int, f_dec;
                    if (sscanf(buf, "U: %d.%dV | I: %d.%dA | P: %d.%dW | F: %d.%dHz", 
                               &u_int, &u_dec, &i_int, &i_dec, &p_int, &p_dec, &f_int, &f_dec) == 8) {
                        
                        pthread_mutex_lock(&lock);
                        if (pzem_connected == 0) force_redraw = 1; 
                        current_U = u_int + u_dec / 10.0;
                        current_I = i_int + i_dec / 1000.0; 
                        current_P = p_int + p_dec / 10.0;
                        if (f_int > 0) current_Hz = f_int + f_dec / 10.0;
                        current_E_Ws += (current_P * 0.35); 
                        pzem_connected = 1; 
                        pthread_mutex_unlock(&lock);
                    }
                }
                
                pthread_mutex_lock(&lock);
                int is_over_limit = (current_U > max_voltage || current_I > max_current);

                if (is_over_limit && pzem_connected == 1) {
                    safe_timer = 0; 
                    if (relay_state == 1 && !lockout) {
                        relay_state = 0;     
                        write_device("/dev/my_relay", "0"); 
                        alarm_flag = 1;      
                        retry_count++;       
                        force_redraw = 1;    
                        if (retry_count > 3) lockout = 1;
                        else trip_timer = 30; 
                    }
                } 
                else if (!is_over_limit && pzem_connected == 1 && relay_state == 1 && alarm_flag == 1) {
                    safe_timer++;
                    if (safe_timer >= 9) { 
                        alarm_flag = 0; retry_count = 0; lockout = 0; safe_timer = 0; force_redraw = 1;
                    }
                }

                if (alarm_flag == 1 && !lockout && relay_state == 0) {
                    trip_timer--;
                    if (trip_timer <= 0) {
                        relay_state = 1;
                        write_device("/dev/my_relay", "1"); 
                        force_redraw = 1; 
                    }
                }
                pthread_mutex_unlock(&lock);
            }
        }
        usleep(50000); 
    }
    return NULL;
}

// ---------------------------------------------------------
// THREAD 2: Nút nhấn
// ---------------------------------------------------------
void* button_thread_func(void* arg) {
    char btn_val, last_state = '1';
    while(1) {
        int fd_button = open("/dev/my_button", O_RDONLY);
        if (fd_button >= 0) {
            if (read(fd_button, &btn_val, 1) > 0) {
                if (btn_val == '0' && last_state == '1') { 
                    pthread_mutex_lock(&lock);
                    if (alarm_flag == 1 || lockout == 1) { 
                        relay_state = 1; alarm_flag = 0; lockout = 0; retry_count = 0; trip_timer = 0; safe_timer = 0;
                    } else { 
                        relay_state = !relay_state; 
                    }
                    write_device("/dev/my_relay", relay_state ? "1" : "0"); 
                    force_redraw = 1; 
                    pthread_mutex_unlock(&lock);
                }
                last_state = btn_val; 
            }
            close(fd_button);
        }
        usleep(30000); 
    }
    return NULL;
}

// ---------------------------------------------------------
// THREAD 3: Đèn LED báo động (Đã được đồng bộ biến)
// ---------------------------------------------------------
void* action_thread_func(void* arg) {
    int led_toggle = 0;
    while(1) {
        pthread_mutex_lock(&lock);
        int local_alarm = alarm_flag;
        int local_lockout = lockout; 
        pthread_mutex_unlock(&lock);

        if (local_lockout) {
            write_device("/dev/my_led", "0");
            pthread_mutex_lock(&lock); current_led_state = 0; pthread_mutex_unlock(&lock);
            usleep(500000);
        } else if (local_alarm) {
            led_toggle = !led_toggle; // Nhịp chớp
            write_device("/dev/my_led", led_toggle ? "1" : "0");
            pthread_mutex_lock(&lock); current_led_state = led_toggle; pthread_mutex_unlock(&lock);
            usleep(500000); // Tốc độ nháy 250ms/lần cho đẹp mắt trên App
        } else {
            write_device("/dev/my_led", "0");
            pthread_mutex_lock(&lock); current_led_state = 0; pthread_mutex_unlock(&lock);
            usleep(500000);
        }
    }
    return NULL;
}

// ---------------------------------------------------------
// THREAD 4: Động cơ hiển thị TFT 
// ---------------------------------------------------------
void* tft_thread_func(void* arg) {
    if (tft_init("/dev/tft_st7735") < 0) return NULL;
    int tick_counter = 0; 
    while(1) {
        int urgent_update = 0;
        
        pthread_mutex_lock(&lock);
        if (force_redraw) { urgent_update = 1; force_redraw = 0; }
        pthread_mutex_unlock(&lock);

        if (!urgent_update && tick_counter < 15) { 
            tick_counter++; usleep(20000); continue;
        }
        tick_counter = 0; 

        pthread_mutex_lock(&lock);
        int t_connected = pzem_connected; 
        float t_U = current_U, t_I = current_I, t_Hz = current_Hz, t_P = current_P;
        int t_relay = relay_state, t_alarm = alarm_flag, t_lockout = lockout, t_timer = trip_timer;
        double t_E_Ws = current_E_Ws;
        pthread_mutex_unlock(&lock);

        if (t_connected == 0) {
            tft_fill_screen(COLOR_BLACK);
            tft_draw_string(4, 70, "PZEM DISCONNECT", COLOR_RED, COLOR_BLACK, 2);
            tft_update();
            continue; 
        }

        float t_E_Wh = t_E_Ws / 3600.0; 
        char val_str[16];
        tft_fill_screen(COLOR_BLACK);
        
        tft_draw_string(30, 5, "HE THONG GIAM SAT", COLOR_CYAN, COLOR_BLACK, 1);
        
        tft_draw_string(8,  28, "U:", COLOR_YELLOW, COLOR_BLACK, 2);
        sprintf(val_str, "%.1f", t_U); 
        tft_draw_string(40, 28, val_str, COLOR_YELLOW, COLOR_BLACK, 2); 
        tft_draw_string(95, 28, "V", COLOR_YELLOW, COLOR_BLACK, 2);    

        tft_draw_string(8,  50, "I:", COLOR_GREEN, COLOR_BLACK, 2);
        sprintf(val_str, "%.2f", t_I); 
        tft_draw_string(40, 50, val_str, COLOR_GREEN, COLOR_BLACK, 2);
        tft_draw_string(95, 50, "A", COLOR_GREEN, COLOR_BLACK, 2);

        tft_draw_string(8,  72, "F:", COLOR_CYAN, COLOR_BLACK, 2);
        sprintf(val_str, "%.1f", t_Hz);
        tft_draw_string(40, 72, val_str, COLOR_CYAN, COLOR_BLACK, 2);
        tft_draw_string(95, 72, "HZ", COLOR_CYAN, COLOR_BLACK, 2); 

        tft_draw_string(8,  94, "P:", COLOR_WHITE, COLOR_BLACK, 2);
        sprintf(val_str, "%.1f", t_P);
        tft_draw_string(40, 94, val_str, COLOR_WHITE, COLOR_BLACK, 2);
        tft_draw_string(95, 94, "W", COLOR_WHITE, COLOR_BLACK, 2);

        tft_draw_string(8,  116, "E:", COLOR_YELLOW, COLOR_BLACK, 2);
        sprintf(val_str, "%.2f", t_E_Wh); 
        tft_draw_string(40, 116, val_str, COLOR_YELLOW, COLOR_BLACK, 2); 
        tft_draw_string(95, 116, "WH", COLOR_YELLOW, COLOR_BLACK, 2);
        
        if (t_lockout) {
            tft_draw_string(40, 145, "STATUS: LOCK", COLOR_WHITE, COLOR_RED, 1);
        } else if (t_alarm) {
            char wait_str[32];
            int secs_left = (t_timer / 3) + 1; 
            sprintf(wait_str, "KIEM TRA LAI (%d)", secs_left); 
            int pos_x = (secs_left >= 10) ? 30 : 32; 
            tft_draw_string(pos_x, 145, wait_str, COLOR_YELLOW, COLOR_BLACK, 1);
        } else {
            if (t_relay) tft_draw_string(44, 145, "STATUS: ON", COLOR_GREEN, COLOR_BLACK, 1);
            else         tft_draw_string(42, 145, "STATUS: OFF", COLOR_RED, COLOR_BLACK, 1);
        }
        tft_update();
    }
    tft_close();
    return NULL;
}

// ---------------------------------------------------------
// THREAD 5: (MQTT Heartbeat + HTTP Data)
// ---------------------------------------------------------
void* thread_blynk_hybrid(void* arg) {
    // === BƯỚC 1: DÙNG MQTT ĐỂ LẤY CHẤM XANH ONLINE ===
    mosquitto_lib_init();
    global_mosq = mosquitto_new("BBB_SmartBreaker", true, NULL);
    mosquitto_username_pw_set(global_mosq, "device", BLYNK_TOKEN);
    
    if (mosquitto_connect(global_mosq, BLYNK_SERVER, 1883, 60) == MOSQ_ERR_SUCCESS) {
        printf("[HYBRID] MQTT Connected -> KICH HOAT CHAM ONLINE XANH!\n");
        mosquitto_loop_start(global_mosq); 
    }

    // === BƯỚC 2: DÙNG HTTP ĐỂ BƠM DỮ LIỆU & ĐỌC SLIDER SIÊU NHANH ===
    char cmd[512];
    char resp[64];
    int loop_counter = 0;
    int last_led_state = -1;

    while(1) {
        float temp_max_u = -1.0;
        float temp_max_i = -1.0;
        
        // Đọc Slider nhanh
        FILE *fp_u = popen("wget -qO- -T 1 \"http://" BLYNK_SERVER "/external/api/get?token=" BLYNK_TOKEN "&v5\" 2>/dev/null", "r");
        if (fp_u) {
            if (fgets(resp, sizeof(resp), fp_u) != NULL) temp_max_u = atof(resp); 
            pclose(fp_u);
        }
        
        FILE *fp_i = popen("wget -qO- -T 1 \"http://" BLYNK_SERVER "/external/api/get?token=" BLYNK_TOKEN "&v6\" 2>/dev/null", "r");
        if (fp_i) {
            if (fgets(resp, sizeof(resp), fp_i) != NULL) temp_max_i = atof(resp);
            pclose(fp_i);
        }

        pthread_mutex_lock(&lock);
        if (temp_max_u > 0 && temp_max_u != max_voltage) max_voltage = temp_max_u;
        if (temp_max_i > 0 && temp_max_i != max_current) max_current = temp_max_i;
        
        float t_U = current_U, t_I = current_I, t_P = current_P, t_Hz = current_Hz;
        double t_E_Ws = current_E_Ws;
        int t_led_state = current_led_state; // Lấy trạng thái của LED vật lý
        pthread_mutex_unlock(&lock);

        // --- CẬP NHẬT NHẤP NHÁY LED LÊN APP (Chạy ngầm không giật lag) ---
        if (t_led_state != last_led_state) {
            // Dấu & ở cuối lệnh giúp wget chạy ngầm dưới background
            sprintf(cmd, "wget -qO- -T 1 \"http://" BLYNK_SERVER "/external/api/update?token=%s&v7=%d\" > /dev/null 2>&1 &", BLYNK_TOKEN, t_led_state);
            system(cmd);
            last_led_state = t_led_state;
        }

        // --- BƠM DỮ LIỆU ĐIỆN THEO CHU KỲ ÉP XUNG (0.5 Giây/Lần) ---
        loop_counter++;
        if (loop_counter >= 5) { // 5 nhịp * 100ms = 500ms
            float t_E_Wh = t_E_Ws / 3600.0;
            // Bỏ v7 ra khỏi cụm batch update để không đụng chạm đến tốc độ nháy LED
            sprintf(cmd, "wget -qO- -T 1 \"http://" BLYNK_SERVER "/external/api/batch/update?token=%s&v0=%.1f&v1=%.2f&v2=%.1f&v3=%.2f&v4=%.1f\" > /dev/null 2>&1 &", 
                    BLYNK_TOKEN, t_U, t_I, t_P, t_E_Wh, t_Hz);
            system(cmd);
            loop_counter = 0;
        }

        usleep(100000); // Quét siêu tốc 100ms/lần
    }
    return NULL;
}

// ---------------------------------------------------------
// HÀM MAIN
// ---------------------------------------------------------
int main() {
    signal(SIGINT, handle_sigint); 
    signal(SIGTERM, handle_sigint); 
    signal(SIGSEGV, handle_sigint);
    signal(SIGABRT, handle_sigint);

    write_device("/dev/my_relay", "1"); 
    pthread_mutex_init(&lock, NULL);
    
    pthread_t t1, t2, t3, t4, t5; 
    
    pthread_create(&t1, NULL, sensor_thread_func, NULL);
    pthread_create(&t2, NULL, button_thread_func, NULL);
    pthread_create(&t3, NULL, action_thread_func, NULL);
    pthread_create(&t4, NULL, tft_thread_func, NULL);
    pthread_create(&t5, NULL, thread_blynk_hybrid, NULL); 
    
    pthread_join(t1, NULL); pthread_join(t2, NULL); pthread_join(t3, NULL); pthread_join(t4, NULL); pthread_join(t5, NULL);
    
    pthread_mutex_destroy(&lock);
    return 0;
}

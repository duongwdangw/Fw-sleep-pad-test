#ifndef __SLEEP_PAD_H__
#define __SLEEP_PAD_H__

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

typedef enum{
    SLEEP_PAD_MONITOR_MODE,
    SLEEP_PAD_DATA_DEBUG_MODE,
    SLEEP_PAD_BLE_DEBUG_MODE,
    SLEEP_PAD_UPD_MODE
}sleep_pad_mode_t;

typedef struct{
    float enter_exit_bed_threshold; 
    float body_movement_threshold;
    float pressure_threshold;
    float snoring_sensitivity;  
    float weak_breath_sensitivity; 
    float sleep_rt_staging_threshold_1;  
    float sleep_rt_staging_threshold_2;  
    float sleep_rt_staging_threshold_3;  
    float sleep_staging_threshold_1;  
    float sleep_staging_threshold_2;  
    float sleep_staging_threshold_3;  
    float daily_report_time; 
    float internal_parameter;  
    float Reserved_parameter; 
    float hardware_enable; 
}sleep_pad_parameter_t;

// Dung DUNG theo tai lieu chinh hang "Luc Cam Khoa Ky UART Hardware Spec V106"
// muc 4.3/4.4 (TAOSF/TAOSG), gia tri byte trang thai anh xa TRUC TIEP (khong can
// bang chuyen doi) vi thu tu enum duoi day khop chinh xac voi gia tri thuc te
// thiet bi gui ve:
//   0 = dang nam tren giuong (in bed)
//   1 = da roi giuong (off bed)
//   2 = co cu dong co the (body movement)
//   3 = tho yeu (weak breathing)
//   4 = co vat nang tren giuong nhung khong co sinh hieu (heavy object)
//   5 = dang ngay (snoring)
typedef enum{
    SP_STATUS_IN_BED = 0,
    SP_STATUS_OFF_BED = 1,
    SP_STATUS_BODY_MOVEMENT = 2,
    SP_STATUS_WEAK_BREATHING = 3,
    SP_STATUS_HEAVY_OBJECT = 4,
    SP_STATUS_SNORING = 5
}sleep_status_t;

typedef struct{
    int serial_number;
    int time_stamp;
    sleep_status_t sleep_status;
    int heart_rate;
    int breathing_rate;
    int sdata;
    int pdata;
}sleep_pad_data_second_report_t;

typedef struct{
    int serial_number;
    int time_stamp;
    sleep_status_t sleep_status;
    int heart_rate;
    int breathing_rate;
    int body_movement_count;
    int snoring_count;
    int respiratory_disorder_count;
    int PTHD;
    int TEMP;
}sleep_pad_data_minute_report_t;

// Khai báo extern để các tệp khác có thể truy cập vào Queue
extern QueueHandle_t sp_data_queue_second_report;
extern QueueHandle_t sp_data_queue_minute_report;

void sp_read_uart_data_task(void *pvParameter);

// Cac lenh dung dung theo tai lieu UART Hardware Spec V106, muc 4.
void sp_request_send_data_once_a_second(); // [TAOSG] - bat dau lam viec, tra ve 66 byte/giay (goc + ket qua)
void sp_start_result_only();               // [TAOSF] - bat dau lam viec, tra ve 9 byte/giay (chi ket qua)
void sp_change_to_idle_mode();              // [TAOSE] - dung, ve che do cho
void sp_check_hardware_issue();             // [TAOSH] - kiem tra loi phan cung
void sp_set_sensor_sensitivity(); // Lenh TAOSO de tinh chinh nguong ap luc va bien do song
#endif
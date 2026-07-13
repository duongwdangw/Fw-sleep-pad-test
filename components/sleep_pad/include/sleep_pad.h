#ifndef __SLEEP_PAD_H__
#define __SLEEP_PAD_H__

typedef enum{
    SLEEP_PAD_MONITOR_MODE,
    SLEEP_PAD_DATA_DEBUG_MODE,
    SLEEP_PAD_BLE_DEBUG_MODE,
    SLEEP_PAD_UPD_MODE
}sleep_pad_mode_t;

typedef struct{
    float enter_exit_bed_threshold; //Dynamic threshold for entering/exiting the bed.AI-adjusted; modification not recommended
    float body_movement_threshold;//Body movement threshold. AI-adjusted; modification not recommended.
    float pressure_threshold;//Static pressure threshold. AI-adjusted; modification not recommended.
    float snoring_sensitivity;// Snoring sensitivity threshold. Higher value means harder to detect snoring.Can be modified by user.  
    float weak_breath_sensitivity;//Weak breathing sensitivity. Lower value makes detection easier. Can be modified by user. 
    float sleep_rt_staging_threshold_1; //Sleep real-time staging algorithm threshold. Do not modify. 
    float sleep_rt_staging_threshold_2; //Sleep real-time staging algorithm threshold. Do not modify. 
    float sleep_rt_staging_threshold_3; //Sleep real-time staging algorithm threshold. Do not modify. 
    float sleep_staging_threshold_1; //Sleep staging algorithm threshold. Do not modify. 
    float sleep_staging_threshold_2; //Sleep staging algorithm threshold. Do not modify.  
    float sleep_staging_threshold_3; //Sleep staging algorithm threshold. Do not modify.  
    float daily_report_time; //Daily report generation time (in hours, 0-23). can be modified by user.
    float internal_parameter; //AI-adjusted internal parameter. Do not modify. 
    float Reserved_parameter; //Reserved parameter. Not enabled
    float hardware_enable; //Hardware enable/lock parameter. Do not modify.
}sleep_pad_parameter_t;


typedef enum{
    OUT_OF_BED,
    BODY_MOVEMENT,
    SITTING_UP,
    SLEEPING,
    WAKE_UP,
    HAVY_THING_ON_BED,
    SNORING,
    WEAK_BREATH
}sleep_status_t;

typedef struct{
    int serial_number;
    int time_stamp;
    sleep_status_t sleep_status;
    int heart_rate;
    float breathing_rate;
    int sdata;
    int pdata;
}sleep_pad_data_second_report_t;

typedef struct{
    int serial_number;
    int time_stamp;
    sleep_status_t sleep_status;
    int heart_rate;
    float breathing_rate;
    int body_movement_count;
    int snoring_count;
    int respiratory_disorder_count;
    int PTHD;
    int TEMP;
}sleep_pad_data_minute_report_t;

void sp_set_mode(sleep_pad_mode_t mode);
void sp_read_uart_data_task(void *pvParameter);

void sp_request_send_data_once_a_second();
void sp_change_to_idle_mode();

#endif
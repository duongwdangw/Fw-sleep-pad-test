#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdint.h>
#include <driver/uart.h>
#include <string.h>

#include "sleep_pad.h"
#include "config_parameter.h"

#define TAG "SleepPad"

// ============================================================================
// Giao thuc chinh xac theo tai lieu "Luc Cam Khoa Ky UART Hardware Spec V106"
// (danh rieng cho model LSM-800-T - ban UART noi bo, KHONG phai ban WiFi/4G
// len cloud). Cac lenh la chuoi ASCII 5 ky tu bat dau bang [TAOS], KHONG lien
// quan gi den khung 0x7D dung trong file datasheet TCP/cloud truoc day.
// ============================================================================

// Nhan dien goi ket qua [Bdata]: 5 byte ASCII co dinh, luon dung ngay truoc
// 4 byte du lieu that (serial, trang thai, nhip tim, nhip tho). Dung marker
// nay de tim goi ket qua trong dong byte, thay vi doan theo tong do dai doc
// duoc moi lan (ben vung hon voi ca khung 9-byte cua TAOSF lan 66-byte cua
// TAOSG, va khong bi vo neu du lieu bi cat/ghep giua cac lan doc UART).
static const uint8_t BDATA_TAG[5] = {0x42, 0x64, 0x61, 0x74, 0x61}; // "Bdata"
#define SP_RX_BUF_SIZE 256

QueueHandle_t sp_data_queue_second_report;
QueueHandle_t sp_data_queue_minute_report;

sleep_pad_data_second_report_t sp_data_second_report;
sleep_pad_data_minute_report_t sp_data_minute_report;
sleep_pad_parameter_t sp_data_parameter_report;

static void sp_send_cmd(const char *cmd){
    // Muc 4 datasheet: "mot lenh phai duoc gui lien tuc 1 lan, khong duoc
    // gui [TAOS] roi tam dung roi gui [E]" -> luon gui nguyen cum 1 lan goi.
    uart_write_bytes(SLEEP_PAD_UART_NUM, cmd, strlen(cmd));
}

void sp_request_send_data_once_a_second(){ sp_send_cmd("TAOSG"); } // bat dau, tra ve 66 byte/giay
void sp_start_result_only(){ sp_send_cmd("TAOSF"); }               // bat dau, tra ve 9 byte/giay
void sp_change_to_idle_mode(){ sp_send_cmd("TAOSE"); }             // dung, ve che do cho
void sp_check_hardware_issue(){ sp_send_cmd("TAOSH"); }            // kiem tra loi phan cung

void sp_queue_init(){
    sp_data_queue_second_report = xQueueCreate(10, sizeof(sleep_pad_data_second_report_t));
    sp_data_queue_minute_report = xQueueCreate(10, sizeof(sleep_pad_data_minute_report_t));
}

// Muc 4.3/4.4: sau tag [Bdata] la dung 4 byte:
//   [serial 1 byte, 0-59] [trang thai 1 byte] [nhip tim 1 byte] [nhip tho 1 byte, /10]
// LUU Y QUAN TRONG (theo dung tai lieu, khong suy doan):
//   - Trang thai =0 la DANG NAM TREN GIUONG - KHONG duoc ep nhip tim ve 0 luc nay.
//   - Trang thai =1 moi la DA ROI GIUONG - luc nay ep nhip tim/nhip tho ve 0.
//   - Nhip tim/nhip tho CHỈ co gia tri sau 25 giay nam yen lien tuc tren giuong,
//     nen bang 0 trong 25 giay dau la dung, khong phai loi.
//
// tag_pos: vi tri byte 'B' cua [Bdata] trong rx_buf. Theo dung bang offset
// trong tai lieu (Bdata o byte 58-62 tinh tu 1, tuc offset 57-61 tinh tu 0),
// neu tag_pos >= 57 nghia la co du 57 byte phia truoc (Odata 5 byte + 50 byte
// song ap dien + 2 byte ap luc dien tro) - tuc day la khung day du (TAOSG),
// nen co the doc them pdata/sdata.
//   - pdata (byte 56-57 tinh tu dau [Odata]): tin hieu ap luc DIEN TRO tho (0-4096),
//     dung DUNG offset trong tai lieu, khong suy doan.
//   - sdata: tai lieu KHONG dinh nghia ro day la mot gia tri don le ma la 25 mau
//     tin hieu AP DIEN (piezoelectric) lien tiep. De co MOT gia tri "sdata" duy
//     nhat cho JSON theo yeu cau, toi lay BIEN DO DAO DONG LON NHAT (gia tri
//     tuyet doi lon nhat) trong 25 mau do trong khung hien tai, dai dien cho
//     muc do bien dong tin hieu ap luc trong giay do. Day la LUA CHON DIEN GIAI
//     hop ly cua toi, KHONG phai dinh nghia tuong minh tu tai lieu - can doi
//     chieu lai voi doi yeu cau MQTT neu ho co dinh nghia khac.
static void sp_handle_bdata_result(const uint8_t *rx_buf, int tag_pos, int rx_len){
    const uint8_t *p = &rx_buf[tag_pos + 5];

    sp_data_second_report.serial_number   = p[0];
    sp_data_second_report.sleep_status    = (sleep_status_t)p[1];
    sp_data_second_report.heart_rate      = p[2];
    sp_data_second_report.breathing_rate  = p[3] / 10.0f;
    sp_data_second_report.sdata = 0;
    sp_data_second_report.pdata = 0;

    if(tag_pos >= 57 && tag_pos + 9 <= rx_len){
        // Khung day du (TAOSG) - co khoi [Odata] phia truoc, doc them pdata/sdata
        int pressure_offset = tag_pos - 2; // byte 56-57 (0-indexed 55-56) = tag_pos-2, tag_pos-1
        int16_t pressure = (int16_t)(rx_buf[pressure_offset] | (rx_buf[pressure_offset + 1] << 8));
        sp_data_second_report.pdata = pressure;

        int piezo_start = tag_pos - 52; // 5 (Odata) + 50 (piezo) + 2 (pressure) = 57; piezo bat dau tu idx 5
        int16_t peak = 0;
        for(int i = 0; i < 25; i++){
            int16_t sample = (int16_t)(rx_buf[piezo_start + i*2] | (rx_buf[piezo_start + i*2 + 1] << 8));
            int16_t abs_sample = sample < 0 ? -sample : sample;
            if(abs_sample > peak) peak = abs_sample;
        }
        sp_data_second_report.sdata = peak;
    }

    if(sp_data_second_report.sleep_status == SP_STATUS_OFF_BED){
        sp_data_second_report.heart_rate     = 0;
        sp_data_second_report.breathing_rate = 0;
    }

    xQueueSend(sp_data_queue_second_report, &sp_data_second_report, 0);

    static const char *status_name[6] = {
        "Dang nam tren giuong", "Da roi giuong", "Co cu dong",
        "Tho yeu", "Khong phat hien nguoi nam", "Dang ngay"
    };
    const char *name = (sp_data_second_report.sleep_status <= SP_STATUS_SNORING)
                        ? status_name[sp_data_second_report.sleep_status] : "Khong xac dinh";

    ESP_LOGI(TAG, "[%s] Trang thai=%d | Nhip tim: %d BPM | Nhip tho: %.1f lan/phut | sdata=%d pdata=%d",
             name,
             sp_data_second_report.sleep_status,
             sp_data_second_report.heart_rate,
             sp_data_second_report.breathing_rate,
             sp_data_second_report.sdata,
             sp_data_second_report.pdata);
}

// Doc UART theo dong byte, tim tag [Bdata] o bat ky vi tri nao trong bo dem,
// ben vung voi ca khung 9-byte (TAOSF) lan 66-byte (TAOSG), va khong bi vo neu
// du lieu bi chia cat giua cac lan doc.
void sp_read_uart_data_task(void *pvParameter){
    sp_queue_init();
    uart_flush_input(SLEEP_PAD_UART_NUM);
    ESP_LOGI(TAG, "sp_read_uart_data_task: da khoi dong, bat dau lang nghe UART%d", SLEEP_PAD_UART_NUM);

    static uint8_t rx_buf[SP_RX_BUF_SIZE];
    int rx_len = 0;
    TickType_t last_heartbeat = xTaskGetTickCount();

    while(1){
        uint8_t chunk[128];
        int n = uart_read_bytes(SLEEP_PAD_UART_NUM, chunk, sizeof(chunk), 20 / portTICK_PERIOD_MS);

        if(n > 0){
            if(rx_len + n > (int)sizeof(rx_buf)){
                // Bo dem day bat thuong (khong tim thay tag trong thoi gian dai)
                // -> chi giu lai vai byte cuoi phong khi tag bi cat doi, tranh tran bo nho.
                int keep = 8;
                if(rx_len > keep){
                    memmove(rx_buf, &rx_buf[rx_len - keep], keep);
                    rx_len = keep;
                }
            }
            memcpy(&rx_buf[rx_len], chunk, n);
            rx_len += n;
        }

        // Tim tag [Bdata] va xu ly toan bo cac goi ket qua hoan chinh dang co
        while(1){
            int tag_pos = -1;
            for(int i = 0; i + 5 <= rx_len; i++){
                if(memcmp(&rx_buf[i], BDATA_TAG, 5) == 0){ tag_pos = i; break; }
            }
            if(tag_pos < 0){
                // Khong tim thay tag: giu lai toi da 4 byte cuoi (phong khi tag
                // dang bi cat dang giua), xoa phan con lai.
                int keep = rx_len < 4 ? rx_len : 4;
                if(rx_len > keep){
                    memmove(rx_buf, &rx_buf[rx_len - keep], keep);
                }
                rx_len = keep;
                break;
            }
            // Can du 4 byte ngay sau tag (serial, trang thai, nhip tim, nhip tho)
            if(tag_pos + 5 + 4 > rx_len){
                // Chua du du lieu, doi lan doc UART tiep theo. Bo phan rac
                // truoc tag di, giu lai tu tag tro ve sau.
                memmove(rx_buf, &rx_buf[tag_pos], rx_len - tag_pos);
                rx_len -= tag_pos;
                break;
            }

            sp_handle_bdata_result(rx_buf, tag_pos, rx_len);

            int consumed = tag_pos + 5 + 4;
            memmove(rx_buf, &rx_buf[consumed], rx_len - consumed);
            rx_len -= consumed;
        }

        if((xTaskGetTickCount() - last_heartbeat) > pdMS_TO_TICKS(5000)){
            ESP_LOGI(TAG, "sp_read_uart_data_task: task van dang chay binh thuong");
            last_heartbeat = xTaskGetTickCount();
        }
    }
}

// Ham cau hinh sau tham so noi bo cam bien (Giai quyet sdata bao hoa va loi ap luc nem)
void sp_set_sensor_sensitivity() {
    uint8_t cmd[38];
    // Khoi tao lenh TAOSO= (Dai 6 byte)[cite: 3]
    memcpy(cmd, "TAOSO=", 6);
    
    // Khai bao 8 tham so float. (ESP32 luu float duoi dang little-endian, khop chuan yeu cau)[cite: 3]
    float params[8] = {
        35.0f,     // Para 1: Nguong Piezoelectric (mac dinh 35)[cite: 3]
        10000.0f,  // Para 2: Nguong cu dong (mac dinh 10000)[cite: 3]
        3000.0f,   // Para 3: TANG LEN 3000 de loai bo trong luong cua dem khong, tranh loi bao "Dang nam"[cite: 3]
        15.0f,     // Para 4: TANG LEN 15 de giam do khuech dai, chong bao hoa sdata (32767)[cite: 3]
        4.9f,      // Para 5: Do nhay tho yeu (mac dinh 4.9)[cite: 3]
        18.5f,     // Para 6: Do nhay ngay (mac dinh 18.5)[cite: 3]
        3500.0f,   // Para 7: Chua dung[cite: 3]
        3500.0f    // Para 8: Chua dung[cite: 3]
    };
    
    // Copy 32 byte tham so (8 so float * 4 byte) vao mang lenh[cite: 3]
    memcpy(&cmd[6], params, 32);
    
    // Gui lenh xuong chip cam bien
    uart_write_bytes(SLEEP_PAD_UART_NUM, cmd, sizeof(cmd));
    //ESP_LOGW(TAG, "Da gui lenh TAOSO= de tinh chinh Para 3 va Para 4");
}
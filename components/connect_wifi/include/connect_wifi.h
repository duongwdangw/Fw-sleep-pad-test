#ifndef CONNECT_WIFI_H
#define CONNECT_WIFI_H


extern bool got_ip;
void wifi_init(void);
void wifi_connect_sta(const char *ssid, const char *pass);

#endif
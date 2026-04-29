#ifndef __ESP_HOSTED_WIFI_H__
#define __ESP_HOSTED_WIFI_H__

void init_wifi(void);
void do_wifi_scan(void);

void wifi_connect_sta(const char *ssid, const char *pass);
void wifi_init_sntp(void);

#endif
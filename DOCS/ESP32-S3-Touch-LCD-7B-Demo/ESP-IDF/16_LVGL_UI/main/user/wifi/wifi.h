#ifndef _WIFI_
#define _WIFI_

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"

#include "esp_mac.h"

// Tag for SoftAP and STA mode logging
extern const char *TAG_AP ;
extern const char *TAG_STA;

// Task handle for Wi-Fi task
extern TaskHandle_t wifi_TaskHandle;

// Array to store information about available Access Points (AP)
extern wifi_ap_record_t ap_info[];

// Default scan list size (max 20 APs)
#define DEFAULT_SCAN_LIST_SIZE 20 // 0 ~ 20

/* DHCP server option */
#define DHCPS_OFFER_DNS             0x02 // DNS option for DHCP server

// Function prototypes

// Initialize Wi-Fi and networking stack
void wifi_init(void);

// Set the default network interface for STA mode
void wifi_set_default_netif();

// Enable Network Address and Port Translation (NAPT)
void wifi_napt_enable();

// Set DNS address for SoftAP
void wifi_ap_set_dns_addr(esp_netif_t *sta_netif, esp_netif_t *ap_netif);

// Print the authentication mode (e.g., WPA2, WEP, etc.)
void print_auth_mode(int authmode);

// Print cipher types for the security protocols used in Wi-Fi
void print_cipher_type(int pairwise_cipher, int group_cipher);

// Scan for available Wi-Fi networks
void wifi_scan(void);

// Wi-Fi task that handles Wi-Fi operations
void wifi_task(void *arg);

/* STA (Station) Mode Functions */

// Index of the last selected Wi-Fi network
extern int8_t wifi_last_index;

// The button for the last selected Wi-Fi network in the UI
extern lv_obj_t *wifi_last_Button;

// Connection flags to track Wi-Fi connection status
extern bool connection_flag;
extern bool connection_last_flag;

// Start Wi-Fi event handling
void start_wifi_events();

// Initialize Wi-Fi in STA mode with SSID, password, and auth mode
void wifi_sta_init(uint8_t *ssid, uint8_t *pwd, wifi_auth_mode_t authmode);

// Wait for Wi-Fi connection to be established
void wifi_wait_connect();

// Open Wi-Fi in STA (Station) mode
void wifi_open_sta();

// Close Wi-Fi in STA mode
void wifi_close_sta();

/* AP (Access Point) Mode Functions */

// Initialize SoftAP with SSID, password, and channel
void wifi_ap_init(uint8_t *ssid, uint8_t *pwd, uint8_t channel);

// Open SoftAP (Access Point) mode
void wifi_open_ap();

// Close SoftAP mode
void wifi_close_ap();

#endif

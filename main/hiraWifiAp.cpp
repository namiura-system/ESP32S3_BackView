#include "hiraWifiAp.h"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define AP_SSID      "VINTAGE_BACKVIEW"
#define AP_PASS      "VINTAGEAMERICAN"
#define AP_CHANNEL   1
// Number of devices that can join the Wi-Fi (number of video viewers is hiraHttpServer::MAX_STREAM)
#define AP_MAX_CONN  4    // Wi-Fi でつなげる台数（映像を送る台数は hiraHttpServer::MAX_STREAM）

hiraWifiAp::hiraWifiAp(backViewEcst *p)
    : parent(p)
{
}

hiraWifiAp::~hiraWifiAp()
{
}

bool hiraWifiAp::go()
{
    // NVS (used by Wi-Fi to store its settings)
    // NVS（Wi-Fi が設定の保存に使う）
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        std::printf("HWA: nvs init failed (%s)\n", esp_err_to_name(err));
        return false;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        std::printf("HWA: netif init failed (%s)\n", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        std::printf("HWA: event loop failed (%s)\n", esp_err_to_name(err));
        return false;
    }
    // 192.168.4.1, with a DHCP server
    esp_netif_create_default_wifi_ap();   // 192.168.4.1、DHCP サーバーあり

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        std::printf("HWA: wifi init failed (%s)\n", esp_err_to_name(err));
        return false;
    }
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &hiraWifiAp::onEvent, this, nullptr);

    wifi_config_t wc = {};
    std::memcpy(wc.ap.ssid, AP_SSID, std::strlen(AP_SSID));
    wc.ap.ssid_len = std::strlen(AP_SSID);
    std::memcpy(wc.ap.password, AP_PASS, std::strlen(AP_PASS));
    wc.ap.channel = AP_CHANNEL;
    wc.ap.max_connection = AP_MAX_CONN;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wc.ap.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &wc);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        std::printf("HWA: wifi start failed (%s)\n", esp_err_to_name(err));
        return false;
    }
    std::printf("HWA: AP \"%s\" started (ch %d, max %d)\n", AP_SSID, AP_CHANNEL, AP_MAX_CONN);
    return true;
}

void hiraWifiAp::onEvent(void *arg, const char *base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        auto *e = static_cast<wifi_event_ap_staconnected_t *>(data);
        std::printf("HWA: connected " MACSTR " aid=%d\n", MAC2STR(e->mac), e->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        auto *e = static_cast<wifi_event_ap_stadisconnected_t *>(data);
        std::printf("HWA: disconnected " MACSTR " aid=%d\n", MAC2STR(e->mac), e->aid);
    }
}

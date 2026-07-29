#include "network_manager.h"

#include "board_config.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "sdkconfig.h"
#include "secret_manager.h"

#if CONFIG_ROBOT_NETWORK_WAVESHARE_P4_WIFI6_KIT
#include "driver/gpio.h"
#if CONFIG_ROBOT_WIFI6_KIT_ENABLE_HOSTED
#include "esp_hosted.h"
#endif
#endif

#if CONFIG_SOC_WIFI_SUPPORTED || CONFIG_ROBOT_WIFI6_KIT_ENABLE_HOSTED
#define ROBOT_USE_ESP_WIFI_API 1
#else
#define ROBOT_USE_ESP_WIFI_API 0
#endif

#if ROBOT_USE_ESP_WIFI_API
#include "esp_wifi.h"
#include <string.h>
#endif

#if CONFIG_ROBOT_NETWORK_WAVESHARE_P4_ETH
#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_eth_phy_ip101.h"
#endif

static const char *TAG = "network_manager";
static bool s_connected;
static bool s_started;
static char s_ip_addr[16] = "0.0.0.0";

#if CONFIG_ROBOT_NETWORK_WAVESHARE_P4_ETH
static esp_eth_handle_t s_eth_handle;
#endif

#if ROBOT_USE_ESP_WIFI_API
static const char *wifi_reason_hint(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return "SSID not found; ESP32-C6 supports 2.4 GHz Wi-Fi only, not 5 GHz";
    case WIFI_REASON_AUTH_FAIL:
        return "authentication failed; check Wi-Fi password";
    case WIFI_REASON_ASSOC_FAIL:
        return "association failed; check AP compatibility/security";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake timeout; check password, RSSI, or AP WPA settings";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "SSID found but security is not compatible";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "SSID found but auth mode is below the configured threshold";
    default:
        return "see esp_wifi reason code";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        esp_wifi_connect();
    } else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = disc ? disc->reason : 0;
        s_connected = false;
        snprintf(s_ip_addr, sizeof(s_ip_addr), "0.0.0.0");
        ESP_LOGW(TAG, "Wi-Fi disconnected: reason=%u (%s), reconnecting", reason, wifi_reason_hint(reason));
        esp_wifi_connect();
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_connected = true;
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t network_start_wifi_sta(const char *backend_name)
{
    if (!secret_manager_has_wifi()) {
        ESP_LOGW(TAG, "Wi-Fi secret not configured, network disabled");
        return ESP_OK;
    }

    secret_config_t secret = secret_manager_get_config();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL),
                        TAG, "wifi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL),
                        TAG, "wifi ip handler failed");

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, secret.wifi_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, secret.wifi_password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "set wifi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    ESP_LOGI(TAG, "%s Wi-Fi start, ssid=%s", backend_name, secret.wifi_ssid);
    return ESP_OK;
}
#endif

#if CONFIG_ROBOT_NETWORK_WAVESHARE_P4_WIFI6_KIT
static esp_err_t network_start_wifi6_kit_c6(void)
{
    ESP_LOGI(TAG, "Waveshare ESP32-P4-WIFI6-KIT detected: external ESP32-C6 module");
    ESP_LOGI(TAG, "C6 control pins: IO2=P4 GPIO%d CHIP_PU=P4 GPIO%d",
             BOARD_C6_IO2_GPIO,
             BOARD_C6_CHIP_PU_GPIO);

    /*
     * P4 GPIO6 is wired to C6_IO2 on this board. It is not part of the
     * STM32/car protocol and should not be driven during normal hosted
     * operation; keep it high-Z before resetting/starting the C6.
     */
    gpio_config_t io2_cfg = {
        .pin_bit_mask = 1ULL << BOARD_C6_IO2_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io2_cfg), TAG, "C6 IO2 gpio config failed");

    gpio_config_t pu_cfg = {
        .pin_bit_mask = 1ULL << BOARD_C6_CHIP_PU_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pu_cfg), TAG, "C6 CHIP_PU gpio config failed");
    gpio_set_level(BOARD_C6_CHIP_PU_GPIO, 1);

#if CONFIG_ROBOT_WIFI6_KIT_ENABLE_HOSTED
    ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init failed");
    return network_start_wifi_sta("ESP32-C6 hosted");
#else
    ESP_LOGW(TAG, "ESP32-C6 hosted Wi-Fi driver is disabled in menuconfig");
    ESP_LOGW(TAG, "Build/flash can continue, but network stays offline until hosted Wi-Fi is enabled");
    return ESP_OK;
#endif
}
#endif

#if CONFIG_ROBOT_NETWORK_WAVESHARE_P4_ETH
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    if (event_id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Ethernet link up");
    } else if (event_id == ETHERNET_EVENT_DISCONNECTED) {
        s_connected = false;
        snprintf(s_ip_addr, sizeof(s_ip_addr), "0.0.0.0");
        ESP_LOGW(TAG, "Ethernet link down");
    } else if (event_id == ETHERNET_EVENT_START) {
        ESP_LOGI(TAG, "Ethernet started");
    } else if (event_id == ETHERNET_EVENT_STOP) {
        s_connected = false;
        ESP_LOGW(TAG, "Ethernet stopped");
    }
}

static void eth_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_connected = true;
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Ethernet got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t network_start_waveshare_eth(void)
{
    ESP_LOGI(TAG, "starting Waveshare ESP32-P4-ETH backend: mdc=%d mdio=%d pwr=%d clk=%d phy_addr=%d",
             BOARD_ETH_MDC_GPIO,
             BOARD_ETH_MDIO_GPIO,
             BOARD_ETH_PHY_POWER_GPIO,
             BOARD_ETH_RMII_CLK_GPIO,
             BOARD_ETH_PHY_ADDR);

    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << BOARD_ETH_PHY_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pwr_cfg), TAG, "eth power gpio config failed");
    gpio_set_level(BOARD_ETH_PHY_POWER_GPIO, 1);

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    ESP_RETURN_ON_FALSE(eth_netif != NULL, ESP_ERR_NO_MEM, TAG, "eth netif alloc failed");

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = BOARD_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = -1;

    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.interface = EMAC_DATA_INTERFACE_RMII;
    emac_config.smi_gpio.mdc_num = BOARD_ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = BOARD_ETH_MDIO_GPIO;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = BOARD_ETH_RMII_CLK_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    ESP_RETURN_ON_FALSE((mac != NULL) && (phy != NULL), ESP_ERR_NO_MEM, TAG, "eth mac/phy alloc failed");

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_cfg, &s_eth_handle), TAG, "eth driver install failed");
    ESP_RETURN_ON_ERROR(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(s_eth_handle)), TAG, "eth attach failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL, NULL),
                        TAG, "eth event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_ip_event_handler, NULL, NULL),
                        TAG, "eth ip event handler failed");
    ESP_RETURN_ON_ERROR(esp_eth_start(s_eth_handle), TAG, "eth start failed");
    return ESP_OK;
}
#endif

esp_err_t network_manager_init(void)
{
    if (s_started) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t loop_ret = esp_event_loop_create_default();
    if ((loop_ret != ESP_OK) && (loop_ret != ESP_ERR_INVALID_STATE)) {
        return loop_ret;
    }

#if CONFIG_ROBOT_NETWORK_WAVESHARE_P4_ETH
    ESP_RETURN_ON_ERROR(network_start_waveshare_eth(), TAG, "waveshare eth start failed");
    s_started = true;
#elif CONFIG_ROBOT_NETWORK_WAVESHARE_P4_WIFI6_KIT
    ESP_RETURN_ON_ERROR(network_start_wifi6_kit_c6(), TAG, "wifi6 kit c6 start failed");
    s_started = true;
#elif CONFIG_SOC_WIFI_SUPPORTED
    ESP_RETURN_ON_ERROR(network_start_wifi_sta("native"), TAG, "native wifi start failed");
    s_started = true;
#else
    if (!secret_manager_has_wifi()) {
        ESP_LOGW(TAG, "Wi-Fi secret not configured, network disabled");
        s_started = true;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Wi-Fi secrets are configured, but this ESP-IDF target has no native Wi-Fi driver");
    ESP_LOGW(TAG, "ESP32-P4 needs a board-specific external Wi-Fi/ETH driver before Qwen can connect");
    s_started = true;
#endif

    return ESP_OK;
}

bool network_manager_is_connected(void)
{
    return s_connected;
}

bool network_manager_is_started(void)
{
    return s_started;
}

bool network_manager_has_native_wifi(void)
{
#if CONFIG_SOC_WIFI_SUPPORTED
    return true;
#else
    return false;
#endif
}

const char *network_manager_backend_name(void)
{
#if CONFIG_ROBOT_NETWORK_WAVESHARE_P4_ETH
    return "waveshare_p4_eth";
#elif CONFIG_ROBOT_NETWORK_WAVESHARE_P4_WIFI6_KIT
    return "waveshare_p4_wifi6_kit_c6";
#elif CONFIG_SOC_WIFI_SUPPORTED
    return "native_wifi";
#else
    return "none";
#endif
}

const char *network_manager_ip_string(void)
{
    return s_ip_addr;
}

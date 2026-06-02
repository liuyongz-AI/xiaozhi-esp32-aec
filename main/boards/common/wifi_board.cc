#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_network.h>
#include <esp_log.h>
#include <utility>

#include <font_awesome.h>
#include <wifi_manager.h>
#include <wifi_station.h>
#include <ssid_manager.h>
#include "afsk_demod.h"
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
#include "blufi.h"
#endif

#include <lwip/sockets.h>
#include <arpa/inet.h>

static const char *TAG = "WifiBoard";

WifiBoard::WifiBoard() {
    esp_timer_create_args_t timer_args = {
        .callback = OnWifiConnectTimeout,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_connect_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&timer_args, &connect_timer_);

    // Create internet check timer
    esp_timer_create_args_t internet_timer_args = {
        .callback = OnInternetCheckTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "internet_check_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&internet_timer_args, &internet_check_timer_);
}

WifiBoard::~WifiBoard() {
    if (connect_timer_) {
        esp_timer_stop(connect_timer_);
        esp_timer_delete(connect_timer_);
    }
    if (internet_check_timer_) {
        esp_timer_stop(internet_check_timer_);
        esp_timer_delete(internet_check_timer_);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::StartNetwork() {
    auto& wifi_manager = WifiManager::GetInstance();

    WifiManagerConfig config;
    config.ssid_prefix = "Xiaozhi";
    config.language = Lang::CODE;
    wifi_manager.Initialize(config);

    wifi_manager.SetEventCallback([this](WifiEvent event, const std::string& data) {
        switch (event) {
            case WifiEvent::Scanning:
                OnNetworkEvent(NetworkEvent::Scanning);
                break;
            case WifiEvent::Connecting:
                OnNetworkEvent(NetworkEvent::Connecting, data);
                break;
            case WifiEvent::Connected:
                OnNetworkEvent(NetworkEvent::Connected, data);
                break;
            case WifiEvent::Disconnected:
                OnNetworkEvent(NetworkEvent::Disconnected);
                break;
            case WifiEvent::ConfigModeEnter:
                OnNetworkEvent(NetworkEvent::WifiConfigModeEnter);
                break;
            case WifiEvent::ConfigModeExit:
                OnNetworkEvent(NetworkEvent::WifiConfigModeExit);
                break;
        }
    });

    TryWifiConnect();
}

void WifiBoard::TryWifiConnect() {
    auto& ssid_manager = SsidManager::GetInstance();
    auto& ssid_list = ssid_manager.GetSsidList();

    if (ssid_list.empty()) {
        ESP_LOGI(TAG, "No SSID configured, entering config mode");
        vTaskDelay(pdMS_TO_TICKS(1500));
        StartWifiConfigMode();
        return;
    }

    // Reset to first SSID when starting fresh
    current_ssid_index_ = 0;
    internet_check_failures_ = 0;

    ESP_LOGI(TAG, "Starting WiFi connection, total configured SSIDs: %d", ssid_list.size());
    for (size_t i = 0; i < ssid_list.size(); i++) {
        ESP_LOGI(TAG, "  SSID[%d]: %s", i, ssid_list[i].ssid.c_str());
    }

    // Start connection with per-SSID timeout
    esp_timer_start_once(connect_timer_, CONNECT_TIMEOUT_PER_SSID_SEC * 1000000ULL);
    WifiManager::GetInstance().StartStation();
}

std::string WifiBoard::GetCurrentSsid() const {
    return WifiManager::GetInstance().GetSsid();
}

int WifiBoard::GetSsidCount() const {
    return SsidManager::GetInstance().GetSsidList().size();
}

std::vector<std::string> WifiBoard::GetConfiguredSsids() const {
    std::vector<std::string> ssids;
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        ssids.push_back(item.ssid);
    }
    return ssids;
}

bool WifiBoard::CheckInternetConnectivity() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGW(TAG, "Failed to create socket for internet check");
        return true; // Assume connected if socket creation fails (avoid false positive)
    }

    // Set 3-second timeout
    struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);

    // Try multiple DNS servers for reliability
    const char* dns_servers[] = {
        "8.8.8.8",    // Google DNS
        "223.5.5.5",  // Alibaba DNS
        "114.114.114.114" // 114 DNS
    };

    bool reachable = false;
    for (auto dns : dns_servers) {
        inet_pton(AF_INET, dns, &dest.sin_addr);
        if (connect(sock, (struct sockaddr*)&dest, sizeof(dest)) == 0) {
            reachable = true;
            ESP_LOGD(TAG, "Internet check OK via %s", dns);
            break;
        }
    }

    close(sock);
    return reachable;
}

void WifiBoard::ForceWifiRescan() {
    ESP_LOGW(TAG, "Force WiFi rescan to try other SSIDs");
    
    // Stop current connection and restart station to trigger a new scan cycle
    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StopStation();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Start station again ¡ª WifiStation will reload SSIDs from SsidManager
    // and try all matching APs automatically
    wifi_manager.StartStation();
}

void WifiBoard::NotifyWifiSwitch(const std::string& ssid) {
    ESP_LOGI(TAG, "Switched to WiFi: %s", ssid.c_str());
    
    // Show notification on display
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        std::string msg = "WiFi: ";
        msg += ssid;
        display->ShowNotification(msg.c_str(), 5000);
    }

    // Play a sound prompt to notify user of WiFi switch
    Application::GetInstance().PlaySound("popup");
}

void WifiBoard::StartInternetCheckTimer() {
    internet_check_failures_ = 0;
    esp_timer_start_periodic(internet_check_timer_, INTERNET_CHECK_INTERVAL_SEC * 1000000ULL);
    ESP_LOGI(TAG, "Internet check timer started (interval: %d seconds)", INTERNET_CHECK_INTERVAL_SEC);
}

void WifiBoard::StopInternetCheckTimer() {
    esp_timer_stop(internet_check_timer_);
    ESP_LOGI(TAG, "Internet check timer stopped");
}

void WifiBoard::OnInternetCheckTimer(void* arg) {
    auto* board = static_cast<WifiBoard*>(arg);
    
    // Skip check if in config mode or not connected
    auto& wifi = WifiManager::GetInstance();
    if (wifi.IsConfigMode() || !wifi.IsConnected()) {
        return;
    }

    if (!board->CheckInternetConnectivity()) {
        board->internet_check_failures_++;
        ESP_LOGW(TAG, "Internet check failed (%d/%d)", 
                 board->internet_check_failures_, MAX_INTERNET_CHECK_FAILURES);
        
        if (board->internet_check_failures_ >= MAX_INTERNET_CHECK_FAILURES) {
            ESP_LOGW(TAG, "Internet unreachable, switching to next WiFi...");
            board->StopInternetCheckTimer();
            
            // Notify user
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                display->ShowNotification("Network unavailable, switching...", 3000);
            }
            Application::GetInstance().PlaySound("exclamation");
            
            // Force rescan to try other SSIDs
            board->ForceWifiRescan();
        }
    } else {
        // Internet is reachable, reset failure count
        if (board->internet_check_failures_ > 0) {
            board->internet_check_failures_ = 0;
            ESP_LOGI(TAG, "Internet check recovered");
        }
    }
}

void WifiBoard::OnNetworkEvent(NetworkEvent event, const std::string& data) {
    switch (event) {
        case NetworkEvent::Connected: {
            esp_timer_stop(connect_timer_);
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
            Blufi::GetInstance().deinit();
#endif
            in_config_mode_ = false;

            std::string ssid = data.empty() ? GetCurrentSsid() : data;
            ESP_LOGI(TAG, "Connected to WiFi: %s", ssid.c_str());

            // Notify user of WiFi switch if not the first connection
            if (current_ssid_index_ > 0) {
                NotifyWifiSwitch(ssid);
            }

            // Reset index and start internet monitoring
            current_ssid_index_ = 0;
            internet_check_failures_ = 0;
            StartInternetCheckTimer();
            break;
        }
        case NetworkEvent::Scanning:
            ESP_LOGI(TAG, "WiFi scanning");
            break;
        case NetworkEvent::Connecting:
            ESP_LOGI(TAG, "WiFi connecting to %s", data.c_str());
            break;
        case NetworkEvent::Disconnected: {
            ESP_LOGW(TAG, "WiFi disconnected");
            StopInternetCheckTimer();

            auto& ssid_list = SsidManager::GetInstance().GetSsidList();
            if (!ssid_list.empty() && !in_config_mode_) {
                // Try to reconnect ¡ª WifiStation handles this internally
                // But if we were connected and got kicked off, force a rescan
                ESP_LOGI(TAG, "WiFi disconnected, will try to reconnect automatically");
            }
            break;
        }
        case NetworkEvent::WifiConfigModeEnter:
            ESP_LOGI(TAG, "WiFi config mode entered");
            in_config_mode_ = true;
            StopInternetCheckTimer();
            break;
        case NetworkEvent::WifiConfigModeExit:
            ESP_LOGI(TAG, "WiFi config mode exited");
            in_config_mode_ = false;
            TryWifiConnect();
            break;
        default:
            break;
    }

    if (network_event_callback_) {
        network_event_callback_(event, data);
    }
}

void WifiBoard::SetNetworkEventCallback(NetworkEventCallback callback) {
    network_event_callback_ = std::move(callback);
}

void WifiBoard::OnWifiConnectTimeout(void* arg) {
    auto* board = static_cast<WifiBoard*>(arg);
    auto& ssid_manager = SsidManager::GetInstance();
    auto& ssid_list = ssid_manager.GetSsidList();

    ESP_LOGW(TAG, "WiFi connection timeout for current SSID");

    if (ssid_list.empty()) {
        WifiManager::GetInstance().StopStation();
        board->StartWifiConfigMode();
        return;
    }

    // Check if there are more SSIDs to try
    bool all_tried = true;
    for (const auto& item : ssid_list) {
        // If this SSID hasn't been the current target, we haven't tried it yet
        // Since WifiStation handles this internally, we check if we should
        // stop and restart to force trying other SSIDs
    }

    // Restart station to force a new scan cycle ¡ª WifiStation will
    // try all SSIDs from SsidManager automatically
    ESP_LOGI(TAG, "Restarting WiFi scan to try other configured SSIDs...");
    WifiManager::GetInstance().StopStation();
    vTaskDelay(pdMS_TO_TICKS(300));
    WifiManager::GetInstance().StartStation();

    // Restart the timeout timer for the next attempt
    esp_timer_start_once(board->connect_timer_, 
                         board->CONNECT_TIMEOUT_PER_SSID_SEC * 1000000ULL);
}

void WifiBoard::StartWifiConfigMode() {
    in_config_mode_ = true;
    Application::GetInstance().SetDeviceState(kDeviceStateStarting);

    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StopStation();
    wifi_manager.StartConfigAp();

    esp_timer_stop(connect_timer_);
    StopInternetCheckTimer();

    ESP_LOGI(TAG, "WiFi config mode started, AP SSID: %s", wifi_manager.GetApSsid().c_str());
}

void WifiBoard::EnterWifiConfigMode() {
    auto state = Application::GetInstance().GetDeviceState();

    if (state == kDeviceStateSpeaking || state == kDeviceStateListening || state == kDeviceStateIdle) {
        Application::GetInstance().ResetProtocol();

        xTaskCreate([](void* arg) {
            auto* board = static_cast<WifiBoard*>(arg);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_timer_stop(board->connect_timer_);
            WifiManager::GetInstance().StopStation();
            board->StartWifiConfigMode();
            vTaskDelete(NULL);
        }, "wifi_cfg_delay", 4096, this, 2, NULL);
        return;
    }

    if (state != kDeviceStateStarting) {
        ESP_LOGE(TAG, "EnterWifiConfigMode called but device state is not starting or speaking, device state: %d", state);
        return;
    }

    esp_timer_stop(connect_timer_);
    WifiManager::GetInstance().StopStation();
    StartWifiConfigMode();
}

bool WifiBoard::IsInWifiConfigMode() const {
    return WifiManager::GetInstance().IsConfigMode();
}

NetworkInterface* WifiBoard::GetNetwork() {
    static EspNetwork network;
    return &network;
}

const char* WifiBoard::GetNetworkStateIcon() {
    auto& wifi = WifiManager::GetInstance();

    if (wifi.IsConfigMode()) {
        return FONT_AWESOME_WIFI;
    }
    if (!wifi.IsConnected()) {
        return FONT_AWESOME_WIFI_SLASH;
    }

    int rssi = wifi.GetRssi();
    if (rssi >= -65) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -75) {
        return FONT_AWESOME_WIFI_FAIR;
    }
    return FONT_AWESOME_WIFI_WEAK;
}

std::string WifiBoard::GetBoardJson() {
    auto& wifi = WifiManager::GetInstance();
    std::string json = R"({"type":")" + std::string(BOARD_TYPE) + R"(",)";
    json += R"("name":")" + std::string(BOARD_NAME) + R"(",)";

    if (!wifi.IsConfigMode()) {
        json += R"("ssid":")" + wifi.GetSsid() + R"(",)";
        json += R"("rssi":)" + std::to_string(wifi.GetRssi()) + R"(,)";
        json += R"("channel":)" + std::to_string(wifi.GetChannel()) + R"(,)";
        json += R"("ip":")" + wifi.GetIpAddress() + R"(",)";
    }

    json += R"("mac":")" + SystemInfo::GetMacAddress() + R"("})";
    return json;
}

void WifiBoard::SetPowerSaveLevel(PowerSaveLevel level) {
    WifiPowerSaveLevel wifi_level;
    switch (level) {
        case PowerSaveLevel::LOW_POWER:
            wifi_level = WifiPowerSaveLevel::LOW_POWER;
            break;
        case PowerSaveLevel::BALANCED:
            wifi_level = WifiPowerSaveLevel::BALANCED;
            break;
        case PowerSaveLevel::PERFORMANCE:
        default:
            wifi_level = WifiPowerSaveLevel::PERFORMANCE;
            break;
    }
    WifiManager::GetInstance().SetPowerSaveLevel(wifi_level);
}

std::string WifiBoard::GetDeviceStatusJson() {
    auto& board = Board::GetInstance();
    auto root = cJSON_CreateObject();

    auto audio_speaker = cJSON_CreateObject();
    if (auto codec = board.GetAudioCodec()) {
        cJSON_AddNumberToObject(audio_speaker, "volume", codec->output_volume());
    }
    cJSON_AddItemToObject(root, "audio_speaker", audio_speaker);

    auto screen = cJSON_CreateObject();
    if (auto backlight = board.GetBacklight()) {
        cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
    }
    if (auto display = board.GetDisplay(); display && display->height() > 64) {
        if (auto theme = display->GetTheme()) {
            cJSON_AddStringToObject(screen, "theme", theme->name().c_str());
        }
    }
    cJSON_AddItemToObject(root, "screen", screen);

    int level = 0;
    bool charging = false, discharging = false;
    if (board.GetBatteryLevel(level, charging, discharging)) {
        auto battery = cJSON_CreateObject();
        cJSON_AddNumberToObject(battery, "level", level);
        cJSON_AddBoolToObject(battery, "charging", charging);
        cJSON_AddItemToObject(root, "battery", battery);
    }

    auto& wifi = WifiManager::GetInstance();
    auto network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "type", "wifi");
    cJSON_AddStringToObject(network, "ssid", wifi.GetSsid().c_str());
    int rssi = wifi.GetRssi();
    const char* signal = rssi >= -60 ? "strong" : (rssi >= -70 ? "medium" : "weak");
    cJSON_AddStringToObject(network, "signal", signal);
    cJSON_AddItemToObject(root, "network", network);

    float temp = 0.0f;
    if (board.GetTemperature(temp)) {
        auto chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "temperature", temp);
        cJSON_AddItemToObject(root, "chip", chip);
    }

    // Add multi-WiFi info
    auto wifi_list = cJSON_CreateArray();
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        cJSON_AddItemToArray(wifi_list, cJSON_CreateString(item.ssid.c_str()));
    }
    cJSON_AddItemToObject(root, "configured_wifi", wifi_list);

    // Add current WiFi info
    auto wifi_info = cJSON_CreateObject();
    cJSON_AddBoolToObject(wifi_info, "internet_ok", (internet_check_failures_ == 0));
    cJSON_AddItemToObject(root, "wifi_status", wifi_info);

    auto str = cJSON_PrintUnformatted(root);
    std::string result(str);
    cJSON_free(str);
    cJSON_Delete(root);
    return result;
}

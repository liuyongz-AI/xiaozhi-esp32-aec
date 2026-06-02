#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>
#include <string>
#include <vector>

class WifiBoard : public Board {
protected:
    esp_timer_handle_t connect_timer_ = nullptr;
    esp_timer_handle_t internet_check_timer_ = nullptr;
    bool in_config_mode_ = false;
    NetworkEventCallback network_event_callback_ = nullptr;
    int current_ssid_index_ = 0;
    int internet_check_failures_ = 0;

    virtual std::string GetBoardJson() override;

    void OnNetworkEvent(NetworkEvent event, const std::string& data = "");
    void TryWifiConnect();
    void StartWifiConfigMode();
    void StartInternetCheckTimer();
    void StopInternetCheckTimer();
    static void OnWifiConnectTimeout(void* arg);
    static void OnInternetCheckTimer(void* arg);

public:
    WifiBoard();
    virtual ~WifiBoard();
    
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    virtual NetworkInterface* GetNetwork() override;
    virtual void SetNetworkEventCallback(NetworkEventCallback callback) override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveLevel(PowerSaveLevel level) override;
    virtual AudioCodec* GetAudioCodec() override { return nullptr; }
    virtual std::string GetDeviceStatusJson() override;

    std::string GetCurrentSsid() const;
    int GetSsidCount() const;
    std::vector<std::string> GetConfiguredSsids() const;
    void ForceWifiRescan();
    bool CheckInternetConnectivity();
    void EnterWifiConfigMode();
    bool IsInWifiConfigMode() const;

private:
    static constexpr int CONNECT_TIMEOUT_PER_SSID_SEC = 20;
    static constexpr int INTERNET_CHECK_INTERVAL_SEC = 30;
    static constexpr int MAX_INTERNET_CHECK_FAILURES = 2;
    void NotifyWifiSwitch(const std::string& ssid);
};

#endif // WIFI_BOARD_H

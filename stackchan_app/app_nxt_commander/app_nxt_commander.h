#pragma once
#include <mooncake.h>
#include <esp_http_server.h>
#include <atomic>
#include <cstdint>

class AppNxtCommander : public mooncake::AppAbility {
public:
    AppNxtCommander();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class CmdState : uint8_t { Idle = 0, Forward, Reverse, Left, Right, Tone };

    httpd_handle_t _httpd      = nullptr;
    bool _usb_host_started     = false;
    uint32_t _unplugged_since  = 0;
    uint32_t _last_status_ms   = 0;
    int _last_speech_state     = -1;

    std::atomic<uint8_t> _pending_state{0};
    std::atomic<bool> _state_dirty{false};

    void start_softap();
    void start_http_server();
    void stop_http_server();
    void apply_avatar_reaction(CmdState state);
    void update_status_speech();

    static esp_err_t handle_root(httpd_req_t* req);
    static esp_err_t handle_drive(httpd_req_t* req);
    static esp_err_t handle_stop(httpd_req_t* req);
    static esp_err_t handle_tone(httpd_req_t* req);
    static esp_err_t handle_status(httpd_req_t* req);
};

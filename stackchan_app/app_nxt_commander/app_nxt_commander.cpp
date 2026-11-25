#include "app_nxt_commander.h"
#include "nxt_usb_host.h"
#include "nxt_web_ui.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace stackchan;

static constexpr const char* kApSsid = "StackChan-NXT";
static constexpr const char* kApPass = "12345678";
static esp_netif_t* s_ap_netif       = nullptr;

AppNxtCommander::AppNxtCommander()
{
    setAppInfo().name           = "NXT.CMD";
    static auto icon            = assets::get_image("icon_controller.bin");
    setAppInfo().icon           = (void*)&icon;
    static uint32_t theme_color = 0x00ADB5;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppNxtCommander::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppNxtCommander::start_softap()
{
    esp_netif_init();
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        mclog::tagWarn(getAppInfo().name, "event loop err: {}", esp_err_to_name(err));
    }
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        mclog::tagWarn(getAppInfo().name, "wifi init err: {}", esp_err_to_name(err));
    }

    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);

    wifi_config_t ap_cfg = {};
    strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), kApSsid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len       = strlen(kApSsid);
    strncpy(reinterpret_cast<char*>(ap_cfg.ap.password), kApPass, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();
    mclog::tagInfo(getAppInfo().name, "SoftAP started: {} (192.168.4.1)", kApSsid);
}

void AppNxtCommander::start_http_server()
{
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    if (httpd_start(&_httpd, &config) != ESP_OK) {
        mclog::tagError(getAppInfo().name, "Failed to start HTTP server");
        return;
    }

    const httpd_uri_t uris[] = {
        {"/", HTTP_GET, handle_root, this},
        {"/api/drive", HTTP_GET, handle_drive, this},
        {"/api/stop", HTTP_GET, handle_stop, this},
        {"/api/tone", HTTP_GET, handle_tone, this},
        {"/api/status", HTTP_GET, handle_status, this},
    };
    for (const auto& u : uris) {
        httpd_register_uri_handler(_httpd, &u);
    }
    mclog::tagInfo(getAppInfo().name, "HTTP server started on port 80");
}

void AppNxtCommander::stop_http_server()
{
    if (_httpd) {
        httpd_stop(_httpd);
        _httpd = nullptr;
    }
}

void AppNxtCommander::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _usb_host_started = false;
    _unplugged_since  = 0;
    _last_speech_state = -1;

    start_softap();
    start_http_server();

    LvglLockGuard lock;
    auto& sc    = GetStackChan();
    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active());
    sc.attachAvatar(std::move(avatar));

    sc.clearModifiers();
    sc.addModifier(std::make_unique<BreathModifier>());
    sc.addModifier(std::make_unique<BlinkModifier>());
    sc.motion().setAutoAngleSyncEnabled(false);
    sc.motion().moveWithSpeed(0, 80, 300);

    view::create_home_indicator([this]() { close(); }, 0x00ADB5, 0x093538);
    view::create_status_bar(0x00ADB5, 0x093538);

    update_status_speech();
}

void AppNxtCommander::apply_avatar_reaction(CmdState state)
{
    auto& sc = GetStackChan();
    if (!sc.hasAvatar()) return;

    switch (state) {
        case CmdState::Forward:
            sc.avatar().setEmotion(avatar::Emotion::Happy);
            sc.motion().moveWithSpeed(0, 140, 400);
            break;
        case CmdState::Reverse:
            sc.avatar().setEmotion(avatar::Emotion::Doubt);
            sc.motion().moveWithSpeed(0, 20, 400);
            break;
        case CmdState::Left:
            sc.avatar().setEmotion(avatar::Emotion::Neutral);
            sc.motion().moveWithSpeed(-350, 80, 450);
            break;
        case CmdState::Right:
            sc.avatar().setEmotion(avatar::Emotion::Neutral);
            sc.motion().moveWithSpeed(350, 80, 450);
            break;
        case CmdState::Tone:
            sc.avatar().setEmotion(avatar::Emotion::Happy);
            sc.motion().moveWithSpeed(0, 240, 500);
            break;
        case CmdState::Idle:
        default:
            sc.avatar().setEmotion(avatar::Emotion::Neutral);
            sc.motion().moveWithSpeed(0, 80, 300);
            break;
    }
}

void AppNxtCommander::update_status_speech()
{
    auto& sc = GetStackChan();
    if (!sc.hasAvatar()) return;

    int state = 0;
    if (_usb_host_started && NxtUsbHost::instance().isConnected()) {
        state = 2;
    } else if (_usb_host_started) {
        state = 1;
    }

    if (state == _last_speech_state) return;
    _last_speech_state = state;

    if (state == 2) {
        sc.avatar().setSpeech("NXT Connected!\nhttp://192.168.4.1");
    } else if (state == 1) {
        sc.avatar().setSpeech("WiFi: StackChan-NXT\nWaiting NXT USB...");
    } else {
        sc.avatar().setSpeech("WiFi: StackChan-NXT\nUnplug PC -> Plug NXT");
    }
}

void AppNxtCommander::onRunning()
{
    // Safely start USB Host + 5V boost only when PC/Charger VBUS is NOT present
    if (!_usb_host_started) {
        if (!GetHAL().isUsbVbusPresent()) {
            if (_unplugged_since == 0) {
                _unplugged_since = GetHAL().millis();
            } else if (GetHAL().millis() - _unplugged_since > 500) {
                mclog::tagInfo(getAppInfo().name, "No PC VBUS; enabling USB 5V boost & NXT Host");
                GetHAL().setUsbOutput(true);
                GetHAL().delay(50);
                NxtUsbHost::instance().begin();
                _usb_host_started = true;
            }
        } else {
            _unplugged_since = 0;
        }
    }

    LvglLockGuard lock;

    if (_state_dirty.exchange(false)) {
        apply_avatar_reaction(static_cast<CmdState>(_pending_state.load()));
    }

    if (GetHAL().millis() - _last_status_ms > 500) {
        _last_status_ms = GetHAL().millis();
        update_status_speech();
    }

    GetStackChan().update();
    view::update_home_indicator();
    view::update_status_bar();
}

void AppNxtCommander::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_usb_host_started) {
        NxtUsbHost::instance().stopMotors();
        GetHAL().setUsbOutput(false);
    }
    stop_http_server();

    LvglLockGuard lock;
    view::destroy_home_indicator();
    view::destroy_status_bar();

    // Warm reboot cleanly restores USB PHY and Wi-Fi state back to launcher
    GetHAL().requestWarmReboot(6);
}

static int get_query_int(httpd_req_t* req, const char* key, int fallback = 0)
{
    char query[96] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[24] = {};
        if (httpd_query_key_value(query, key, val, sizeof(val)) == ESP_OK) {
            return std::atoi(val);
        }
    }
    return fallback;
}

esp_err_t AppNxtCommander::handle_root(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, NXT_INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t AppNxtCommander::handle_drive(httpd_req_t* req)
{
    auto* self = static_cast<AppNxtCommander*>(req->user_ctx);
    int left   = get_query_int(req, "l", 0);
    int right  = get_query_int(req, "r", 0);
    if (left < -100) left = -100;
    if (left > 100) left = 100;
    if (right < -100) right = -100;
    if (right > 100) right = 100;

    if (self->_usb_host_started) {
        NxtUsbHost::instance().drive(static_cast<int8_t>(left), static_cast<int8_t>(right));
    }

    CmdState st = CmdState::Idle;
    if (left > 0 && right > 0) st = CmdState::Forward;
    else if (left < 0 && right < 0) st = CmdState::Reverse;
    else if (left < right) st = CmdState::Left;
    else if (left > right) st = CmdState::Right;
    self->_pending_state.store(static_cast<uint8_t>(st));
    self->_state_dirty.store(true);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

esp_err_t AppNxtCommander::handle_stop(httpd_req_t* req)
{
    auto* self = static_cast<AppNxtCommander*>(req->user_ctx);
    if (self->_usb_host_started) {
        NxtUsbHost::instance().stopMotors();
    }
    self->_pending_state.store(static_cast<uint8_t>(CmdState::Idle));
    self->_state_dirty.store(true);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"stopped\"}");
}

esp_err_t AppNxtCommander::handle_tone(httpd_req_t* req)
{
    auto* self = static_cast<AppNxtCommander*>(req->user_ctx);
    int freq   = get_query_int(req, "f", 440);
    int dur    = get_query_int(req, "d", 200);
    if (self->_usb_host_started) {
        NxtUsbHost::instance().playTone(static_cast<uint16_t>(freq), static_cast<uint16_t>(dur));
    }
    self->_pending_state.store(static_cast<uint8_t>(CmdState::Tone));
    self->_state_dirty.store(true);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"played\"}");
}

esp_err_t AppNxtCommander::handle_status(httpd_req_t* req)
{
    auto* self     = static_cast<AppNxtCommander*>(req->user_ctx);
    bool nxt_ready = self->_usb_host_started && NxtUsbHost::instance().isConnected();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{\"nxt\":%s,\"host\":%s}",
                  nxt_ready ? "true" : "false",
                  self->_usb_host_started ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

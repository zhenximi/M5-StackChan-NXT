#pragma once
#include <cstdint>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <usb/usb_host.h>

#define NXT_VID     0x0694
#define NXT_PID     0x0002
#define NXT_OUT_EP  0x01
#define NXT_IN_EP   0x82

enum NxtPort : uint8_t {
    NXT_PORT_A   = 0x00,
    NXT_PORT_B   = 0x01,  // Left Motor
    NXT_PORT_C   = 0x02,  // Right Motor
    NXT_PORT_ALL = 0xFF
};

class NxtUsbHost {
public:
    static NxtUsbHost& instance()
    {
        static NxtUsbHost inst;
        return inst;
    }

    bool isConnected() const { return _device_connected; }
    bool isStarted() const { return _initialized; }

    void begin()
    {
        if (_initialized) return;
        _mutex        = xSemaphoreCreateMutex();
        _transfer_sem = xSemaphoreCreateBinary();

        usb_host_config_t host_config = {};
        host_config.skip_phy_setup    = false;
        host_config.intr_flags        = ESP_INTR_FLAG_LEVEL1;

        esp_err_t err = usb_host_install(&host_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
            return;
        }

        xTaskCreatePinnedToCore(usbHostTask, "nxt_usb_host", 4096, this, 2, nullptr, 0);

        usb_host_client_config_t client_config       = {};
        client_config.is_synchronous                 = false;
        client_config.max_num_event_msg              = 5;
        client_config.async.client_event_callback    = clientEventCallback;
        client_config.async.callback_arg             = this;

        err = usb_host_client_register(&client_config, &_client_hdl);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_client_register failed: %s", esp_err_to_name(err));
            return;
        }

        xTaskCreatePinnedToCore(clientTask, "nxt_usb_client", 4096, this, 2, nullptr, 0);
        _initialized = true;
        ESP_LOGI(TAG, "USB Host initialized. Waiting for Lego NXT...");
    }

    void setMotor(uint8_t port, int8_t power)
    {
        if (!_device_connected || !_out_transfer) return;

        uint8_t pkt[12] = {};
        pkt[0] = 0x80;                             // Direct command, no reply
        pkt[1] = 0x04;                             // SETOUTPUTSTATE
        pkt[2] = port;                             // Port B or C
        pkt[3] = static_cast<uint8_t>(power);      // -100 to 100
        pkt[4] = (power == 0) ? 0x02 : 0x07;       // BRAKE or MOTORON|BRAKE|REGULATED
        pkt[5] = (power == 0) ? 0x00 : 0x01;       // MOTOR_SPEED
        pkt[6] = 0x00;                             // Turn ratio
        pkt[7] = (power == 0) ? 0x00 : 0x20;       // IDLE or RUNNING
        // pkt[8..11] = 0 (run forever)

        sendPacket(pkt, sizeof(pkt));
    }

    void drive(int8_t left_power, int8_t right_power)
    {
        setMotor(NXT_PORT_B, left_power);
        setMotor(NXT_PORT_C, right_power);
    }

    void stopMotors()
    {
        setMotor(NXT_PORT_B, 0);
        setMotor(NXT_PORT_C, 0);
    }

    void playTone(uint16_t freq_hz, uint16_t duration_ms)
    {
        if (!_device_connected || !_out_transfer) return;

        uint8_t pkt[6];
        pkt[0] = 0x80;
        pkt[1] = 0x03;  // PLAYTONE
        pkt[2] = freq_hz & 0xFF;
        pkt[3] = (freq_hz >> 8) & 0xFF;
        pkt[4] = duration_ms & 0xFF;
        pkt[5] = (duration_ms >> 8) & 0xFF;

        sendPacket(pkt, sizeof(pkt));
    }

private:
    static constexpr const char* TAG = "NxtUsbHost";
    NxtUsbHost() = default;

    usb_host_client_handle_t _client_hdl = nullptr;
    usb_device_handle_t _dev_hdl         = nullptr;
    usb_transfer_t* _out_transfer        = nullptr;
    volatile bool _device_connected      = false;
    bool _initialized                    = false;
    SemaphoreHandle_t _mutex             = nullptr;
    SemaphoreHandle_t _transfer_sem      = nullptr;

    bool sendPacket(const uint8_t* data, size_t len)
    {
        if (!_device_connected || !_out_transfer) return false;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool ok = false;
            if (_device_connected && _out_transfer) {
                xSemaphoreTake(_transfer_sem, 0);
                memcpy(_out_transfer->data_buffer, data, len);
                _out_transfer->num_bytes        = len;
                _out_transfer->bEndpointAddress = NXT_OUT_EP;
                _out_transfer->device_handle    = _dev_hdl;

                if (usb_host_transfer_submit(_out_transfer) == ESP_OK) {
                    xSemaphoreTake(_transfer_sem, pdMS_TO_TICKS(50));
                    ok = true;
                }
            }
            xSemaphoreGive(_mutex);
            return ok;
        }
        return false;
    }

    static void transferCallback(usb_transfer_t* transfer)
    {
        auto* self = static_cast<NxtUsbHost*>(transfer->context);
        if (self && self->_transfer_sem) {
            xSemaphoreGive(self->_transfer_sem);
        }
    }

    static void usbHostTask(void*)
    {
        while (true) {
            uint32_t event_flags = 0;
            usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        }
    }

    static void clientTask(void* arg)
    {
        auto* self = static_cast<NxtUsbHost*>(arg);
        while (true) {
            usb_host_client_handle_events(self->_client_hdl, portMAX_DELAY);
        }
    }

    static void clientEventCallback(const usb_host_client_event_msg_t* event_msg, void* arg)
    {
        auto* self = static_cast<NxtUsbHost*>(arg);
        switch (event_msg->event) {
            case USB_HOST_CLIENT_EVENT_NEW_DEV: {
                usb_device_handle_t dev_hdl = nullptr;
                if (usb_host_device_open(self->_client_hdl, event_msg->new_dev.address, &dev_hdl) == ESP_OK) {
                    const usb_device_desc_t* desc = nullptr;
                    usb_host_get_device_descriptor(dev_hdl, &desc);
                    if (desc && desc->idVendor == NXT_VID && desc->idProduct == NXT_PID) {
                        ESP_LOGI(TAG, "Lego Mindstorms NXT detected!");
                        self->_dev_hdl = dev_hdl;
                        if (usb_host_interface_claim(self->_client_hdl, dev_hdl, 0, 0) == ESP_OK &&
                            usb_host_transfer_alloc(64, 0, &self->_out_transfer) == ESP_OK) {
                            self->_out_transfer->callback = transferCallback;
                            self->_out_transfer->context  = self;
                            self->_device_connected       = true;
                        } else {
                            ESP_LOGE(TAG, "Failed to claim NXT interface");
                            usb_host_device_close(self->_client_hdl, dev_hdl);
                            self->_dev_hdl = nullptr;
                        }
                    } else {
                        usb_host_device_close(self->_client_hdl, dev_hdl);
                    }
                }
                break;
            }
            case USB_HOST_CLIENT_EVENT_DEV_GONE: {
                ESP_LOGI(TAG, "NXT disconnected");
                self->_device_connected = false;
                if (xSemaphoreTake(self->_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (self->_out_transfer) {
                        usb_host_transfer_free(self->_out_transfer);
                        self->_out_transfer = nullptr;
                    }
                    if (self->_dev_hdl) {
                        usb_host_interface_release(self->_client_hdl, self->_dev_hdl, 0);
                        usb_host_device_close(self->_client_hdl, self->_dev_hdl);
                        self->_dev_hdl = nullptr;
                    }
                    xSemaphoreGive(self->_mutex);
                }
                break;
            }
            default:
                break;
        }
    }
};

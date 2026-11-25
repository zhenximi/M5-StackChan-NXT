#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <usb/usb_host.h>
#include <usb/usb_helpers.h>

#define NXT_VID         0x0694
#define NXT_PID         0x0002
#define NXT_DEFAULT_EP  0x01

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

    std::string debugInfo() const
    {
        return std::string(_debug_buf);
    }

    void begin()
    {
        if (_initialized) return;
        _mutex        = xSemaphoreCreateMutex();
        _transfer_sem = xSemaphoreCreateBinary();
        setDebug("installing host...");

        usb_host_config_t host_config   = {};
        host_config.skip_phy_setup      = false;
        host_config.root_port_unpowered = false;
        host_config.intr_flags          = ESP_INTR_FLAG_LEVEL1;

        esp_err_t err = usb_host_install(&host_config);
        if (err != ESP_OK) {
            setDebug("install err: %s", esp_err_to_name(err));
            ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
            return;
        }

        // Register client BEFORE starting event loops so we never miss NEW_DEV
        usb_host_client_config_t client_config    = {};
        client_config.is_synchronous              = false;
        client_config.max_num_event_msg           = 8;
        client_config.async.client_event_callback = clientEventCallback;
        client_config.async.callback_arg          = this;

        err = usb_host_client_register(&client_config, &_client_hdl);
        if (err != ESP_OK) {
            setDebug("client_reg err: %s", esp_err_to_name(err));
            ESP_LOGE(TAG, "usb_host_client_register failed: %s", esp_err_to_name(err));
            return;
        }

        xTaskCreatePinnedToCore(usbHostTask, "nxt_usb_host", 4096, this, 5, nullptr, 0);
        xTaskCreatePinnedToCore(clientTask, "nxt_usb_client", 4096, this, 4, nullptr, 0);

        _initialized = true;
        setDebug("host ready (0 devs)");
        ESP_LOGI(TAG, "USB Host initialized. Waiting for Lego NXT...");
    }

    // Call periodically if not yet connected to catch any already-enumerated device
    void pollExistingDevices()
    {
        if (!_initialized || _device_connected || !_client_hdl) return;
        uint8_t dev_addrs[8] = {};
        int num_devs         = 0;
        if (usb_host_device_addr_list_fill(8, dev_addrs, &num_devs) == ESP_OK && num_devs > 0) {
            for (int i = 0; i < num_devs && !_device_connected; ++i) {
                tryOpenDevice(dev_addrs[i]);
            }
        }
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
    uint8_t _out_ep                      = NXT_DEFAULT_EP;
    uint8_t _iface_num                   = 0;
    volatile bool _device_connected      = false;
    bool _initialized                    = false;
    SemaphoreHandle_t _mutex             = nullptr;
    SemaphoreHandle_t _transfer_sem      = nullptr;
    char _debug_buf[64]                  = "idle";

    void setDebug(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vsnprintf(_debug_buf, sizeof(_debug_buf), fmt, args);
        va_end(args);
    }

    void tryOpenDevice(uint8_t dev_addr)
    {
        if (_device_connected) return;
        usb_device_handle_t dev_hdl = nullptr;
        esp_err_t err               = usb_host_device_open(_client_hdl, dev_addr, &dev_hdl);
        if (err != ESP_OK) {
            setDebug("open(%u) err:%s", dev_addr, esp_err_to_name(err));
            return;
        }

        const usb_device_desc_t* desc = nullptr;
        if (usb_host_get_device_descriptor(dev_hdl, &desc) != ESP_OK || !desc) {
            setDebug("desc err");
            usb_host_device_close(_client_hdl, dev_hdl);
            return;
        }

        ESP_LOGI(TAG, "USB dev addr=%u VID=%04x PID=%04x", dev_addr, desc->idVendor, desc->idProduct);
        if (desc->idVendor != NXT_VID) {
            setDebug("other %04x:%04x", desc->idVendor, desc->idProduct);
            usb_host_device_close(_client_hdl, dev_hdl);
            return;
        }

        // Inspect active config descriptor to find interface & Bulk OUT endpoint
        uint8_t iface_num = 0;
        uint8_t out_ep    = NXT_DEFAULT_EP;
        uint16_t max_pkt  = 64;
        const usb_config_desc_t* cfg_desc = nullptr;
        if (usb_host_get_active_config_descriptor(dev_hdl, &cfg_desc) == ESP_OK && cfg_desc) {
            int offset = 0;
            const usb_standard_desc_t* d = reinterpret_cast<const usb_standard_desc_t*>(cfg_desc);
            while (d != nullptr) {
                if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                    auto* idesc = reinterpret_cast<const usb_intf_desc_t*>(d);
                    iface_num   = idesc->bInterfaceNumber;
                } else if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                    auto* ep = reinterpret_cast<const usb_ep_desc_t*>(d);
                    if ((ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK &&
                        !(ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK)) {
                        out_ep  = ep->bEndpointAddress;
                        max_pkt = ep->wMaxPacketSize ? ep->wMaxPacketSize : 64;
                        break;
                    }
                }
                d = usb_parse_next_descriptor(d, cfg_desc->wTotalLength, &offset);
            }
        }

        err = usb_host_interface_claim(_client_hdl, dev_hdl, iface_num, 0);
        if (err != ESP_OK) {
            setDebug("claim(%u) err:%s", iface_num, esp_err_to_name(err));
            usb_host_device_close(_client_hdl, dev_hdl);
            return;
        }

        err = usb_host_transfer_alloc(max_pkt, 0, &_out_transfer);
        if (err != ESP_OK) {
            setDebug("alloc err:%s", esp_err_to_name(err));
            usb_host_interface_release(_client_hdl, dev_hdl, iface_num);
            usb_host_device_close(_client_hdl, dev_hdl);
            return;
        }

        _dev_hdl                = dev_hdl;
        _iface_num              = iface_num;
        _out_ep                 = out_ep;
        _out_transfer->callback = transferCallback;
        _out_transfer->context  = this;
        _device_connected       = true;
        setDebug("NXT %04x:%04x ep=0x%02x", desc->idVendor, desc->idProduct, out_ep);
        ESP_LOGI(TAG, "Lego NXT ready! VID=%04x PID=%04x iface=%u ep=0x%02x",
                 desc->idVendor, desc->idProduct, iface_num, out_ep);
    }

    bool sendPacket(const uint8_t* data, size_t len)
    {
        if (!_device_connected || !_out_transfer) return false;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool ok = false;
            if (_device_connected && _out_transfer) {
                xSemaphoreTake(_transfer_sem, 0);
                memcpy(_out_transfer->data_buffer, data, len);
                _out_transfer->num_bytes        = len;
                _out_transfer->bEndpointAddress = _out_ep;
                _out_transfer->device_handle    = _dev_hdl;

                esp_err_t err = usb_host_transfer_submit(_out_transfer);
                if (err == ESP_OK) {
                    xSemaphoreTake(_transfer_sem, pdMS_TO_TICKS(50));
                    ok = true;
                } else {
                    ESP_LOGW(TAG, "transfer submit err: %s", esp_err_to_name(err));
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
            case USB_HOST_CLIENT_EVENT_NEW_DEV:
                self->tryOpenDevice(event_msg->new_dev.address);
                break;
            case USB_HOST_CLIENT_EVENT_DEV_GONE: {
                ESP_LOGI(TAG, "NXT disconnected");
                self->_device_connected = false;
                self->setDebug("disconnected");
                if (xSemaphoreTake(self->_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (self->_out_transfer) {
                        usb_host_transfer_free(self->_out_transfer);
                        self->_out_transfer = nullptr;
                    }
                    if (self->_dev_hdl) {
                        usb_host_interface_release(self->_client_hdl, self->_dev_hdl, self->_iface_num);
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

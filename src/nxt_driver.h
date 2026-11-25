#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "usb/usb_host.h"

// Lego Mindstorms NXT USB Identifiers
#define NXT_VID         0x0694
#define NXT_PID         0x0002
#define NXT_OUT_EP      0x01
#define NXT_IN_EP       0x82

enum NxtPort {
    PORT_A   = 0x00,
    PORT_B   = 0x01, // Left Motor
    PORT_C   = 0x02, // Right Motor
    PORT_ALL = 0xFF
};

class NxtUsbHost {
public:
    static NxtUsbHost& instance() {
        static NxtUsbHost inst;
        return inst;
    }

    bool isConnected() const { return _device_connected; }

    void begin() {
        _mutex = xSemaphoreCreateMutex();
        _transfer_sem = xSemaphoreCreateBinary();

        // 1. Install USB Host Library
        const usb_host_config_t host_config = {
            .skip_phy_setup = false,
            .intr_flags = ESP_INTR_FLAG_LEVEL1,
        };
        esp_err_t err = usb_host_install(&host_config);
        if (err != ESP_OK) {
            Serial.printf("[USB] Failed to install USB host: 0x%x\n", err);
            return;
        }

        // 2. Start USB Host background event loop task
        xTaskCreatePinnedToCore(usbHostTask, "usb_host", 4096, this, 2, NULL, 0);

        // 3. Register USB client
        const usb_host_client_config_t client_config = {
            .is_synchronous = false,
            .max_num_event_msg = 5,
            .async = {
                .client_event_callback = clientEventCallback,
                .callback_arg = this
            }
        };
        err = usb_host_client_register(&client_config, &_client_hdl);
        if (err != ESP_OK) {
            Serial.printf("[USB] Failed to register client: 0x%x\n", err);
            return;
        }

        // 4. Start Client event processing task
        xTaskCreatePinnedToCore(clientTask, "usb_client", 4096, this, 2, NULL, 0);
        Serial.println("[USB] USB Host initialized. Waiting for Lego NXT...");
    }

    // Set speed for a single motor (-100 to 100)
    void setMotor(uint8_t port, int8_t power) {
        if (!_device_connected || !_out_transfer) return;

        uint8_t pkt[12];
        pkt[0] = 0x80;                                // Direct command, no response expected
        pkt[1] = 0x04;                                // Command: SETOUTPUTSTATE
        pkt[2] = port;                                // Port (0x01 = B, 0x02 = C)
        pkt[3] = (uint8_t)power;                      // Power (-100 to 100)
        pkt[4] = (power == 0) ? 0x02 : 0x07;          // Mode: BRAKE if 0, else MOTORON | BRAKE | REGULATED
        pkt[5] = (power == 0) ? 0x00 : 0x01;          // Regulation: MOTOR_SPEED
        pkt[6] = 0x00;                                // Turn ratio (0)
        pkt[7] = (power == 0) ? 0x00 : 0x20;          // RunState: RUNNING or IDLE
        pkt[8] = 0; pkt[9] = 0; pkt[10] = 0; pkt[11] = 0; // Tacho limit (0 = run forever)

        sendPacket(pkt, sizeof(pkt));
    }

    // Differential steering: Port B = Left, Port C = Right
    void drive(int8_t left_power, int8_t right_power) {
        setMotor(PORT_B, left_power);
        setMotor(PORT_C, right_power);
    }

    // Stop all driving motors
    void stopMotors() {
        setMotor(PORT_B, 0);
        setMotor(PORT_C, 0);
    }

    // Play a tone on NXT internal speaker
    void playTone(uint16_t freq_hz, uint16_t duration_ms) {
        if (!_device_connected || !_out_transfer) return;

        uint8_t pkt[6];
        pkt[0] = 0x80;
        pkt[1] = 0x03; // Command: PLAYTONE
        pkt[2] = freq_hz & 0xFF;
        pkt[3] = (freq_hz >> 8) & 0xFF;
        pkt[4] = duration_ms & 0xFF;
        pkt[5] = (duration_ms >> 8) & 0xFF;

        sendPacket(pkt, sizeof(pkt));
    }

private:
    NxtUsbHost() {}
    usb_host_client_handle_t _client_hdl = NULL;
    usb_device_handle_t _dev_hdl = NULL;
    usb_transfer_t* _out_transfer = NULL;
    volatile bool _device_connected = false;
    SemaphoreHandle_t _mutex = NULL;
    SemaphoreHandle_t _transfer_sem = NULL;

    bool sendPacket(const uint8_t* data, size_t len) {
        if (!_device_connected || !_out_transfer) return false;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (_device_connected && _out_transfer) {
                memcpy(_out_transfer->data_buffer, data, len);
                _out_transfer->num_bytes = len;
                _out_transfer->bEndpointAddress = NXT_OUT_EP;
                _out_transfer->device_handle = _dev_hdl;
                
                esp_err_t err = usb_host_transfer_submit(_out_transfer);
                if (err == ESP_OK) {
                    xSemaphoreTake(_transfer_sem, pdMS_TO_TICKS(50));
                }
            }
            xSemaphoreGive(_mutex);
            return true;
        }
        return false;
    }

    static void transferCallback(usb_transfer_t* transfer) {
        NxtUsbHost* self = (NxtUsbHost*)transfer->context;
        if (self && self->_transfer_sem) {
            xSemaphoreGive(self->_transfer_sem);
        }
    }

    static void usbHostTask(void* arg) {
        while (1) {
            uint32_t event_flags;
            usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        }
    }

    static void clientTask(void* arg) {
        NxtUsbHost* self = (NxtUsbHost*)arg;
        while (1) {
            usb_host_client_handle_events(self->_client_hdl, portMAX_DELAY);
        }
    }

    static void clientEventCallback(const usb_host_client_event_msg_t* event_msg, void* arg) {
        NxtUsbHost* self = (NxtUsbHost*)arg;
        switch (event_msg->event) {
            case USB_HOST_CLIENT_EVENT_NEW_DEV: {
                usb_device_handle_t dev_hdl;
                if (usb_host_device_open(self->_client_hdl, event_msg->new_dev.address, &dev_hdl) == ESP_OK) {
                    const usb_device_desc_t* desc;
                    usb_host_get_device_descriptor(dev_hdl, &desc);
                    if (desc->idVendor == NXT_VID && desc->idProduct == NXT_PID) {
                        Serial.println("[USB] Lego Mindstorms NXT brick detected!");
                        self->_dev_hdl = dev_hdl;
                        
                        // Claim Interface 0 (Alt Setting 0)
                        usb_host_interface_claim(self->_client_hdl, dev_hdl, 0, 0);

                        // Allocate 64-byte OUT transfer buffer
                        usb_host_transfer_alloc(64, 0, &self->_out_transfer);
                        self->_out_transfer->callback = transferCallback;
                        self->_out_transfer->context = self;

                        self->_device_connected = true;
                    } else {
                        usb_host_device_close(self->_client_hdl, dev_hdl);
                    }
                }
                break;
            }
            case USB_HOST_CLIENT_EVENT_DEV_GONE: {
                Serial.println("[USB] NXT Disconnected");
                self->_device_connected = false;
                if (xSemaphoreTake(self->_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (self->_out_transfer) {
                        usb_host_transfer_free(self->_out_transfer);
                        self->_out_transfer = NULL;
                    }
                    if (self->_dev_hdl) {
                        usb_host_interface_release(self->_client_hdl, self->_dev_hdl, 0);
                        usb_host_device_close(self->_client_hdl, self->_dev_hdl);
                        self->_dev_hdl = NULL;
                    }
                    xSemaphoreGive(self->_mutex);
                }
                break;
            }
            default: break;
        }
    }
};

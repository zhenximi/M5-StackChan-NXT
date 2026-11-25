#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include "nxt_driver.h"
#include "avatar.h"
#include "web_ui.h"

// Wi-Fi Access Point credentials
const char* AP_SSID = "StackChan-NXT";
const char* AP_PASS = "12345678";

WebServer server(80);
StackChanFace face;
bool usbHostStarted = false;
uint32_t unpluggedSinceMs = 0;

void updateStatusDisplay() {
    bool nxtOnline = usbHostStarted && NxtUsbHost::instance().isConnected();
    if (nxtOnline) {
        face.setStatusText("192.168.4.1 | NXT Connected!");
    } else if (usbHostStarted) {
        face.setStatusText("192.168.4.1 | Waiting NXT USB...");
    } else {
        face.setStatusText("192.168.4.1 | PC USB (Unplug for NXT)");
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);

    Serial.println("\n=================================");
    Serial.println("  Stack-chan NXT Commander Boot  ");
    Serial.println("=================================");
    Serial.printf("[Board] id=%d, Display=%dx%d, PMIC=%d\n",
                  (int)M5.getBoard(),
                  M5.Display.width(),
                  M5.Display.height(),
                  (int)M5.Power.getType());
    Serial.printf("[Power] isVBUS=%d, VBUS=%.2fV, Bat=%ld%%, Charging=%d\n",
                  M5.Power.Axp2101.isVBUS(),
                  M5.Power.Axp2101.getVBUSVoltage(),
                  (long)M5.Power.getBatteryLevel(),
                  M5.Power.isCharging());

    // 1. Initialize screen and animated face immediately
    M5.Display.setBrightness(255);
    face.begin();
    face.setExpression(EXP_NORMAL);

    // 2. Start Wi-Fi Access Point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP started! SSID: %s, Pass: %s\n", AP_SSID, AP_PASS);
    Serial.printf("[WiFi] Control WebUI: http://%s\n", WiFi.softAPIP().toString().c_str());

    // 3. Setup WebServer Endpoints
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/api/drive", HTTP_GET, []() {
        int left = server.arg("l").toInt();
        int right = server.arg("r").toInt();

        if (usbHostStarted) {
            NxtUsbHost::instance().drive(left, right);
        }

        if (left > 0 && right > 0) face.setExpression(EXP_FORWARD);
        else if (left < 0 && right < 0) face.setExpression(EXP_BACKWARD);
        else if (left < right) face.setExpression(EXP_LEFT);
        else if (left > right) face.setExpression(EXP_RIGHT);

        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/stop", HTTP_GET, []() {
        if (usbHostStarted) {
            NxtUsbHost::instance().stopMotors();
        }
        face.setExpression(EXP_NORMAL);
        server.send(200, "application/json", "{\"status\":\"stopped\"}");
    });

    server.on("/api/tone", HTTP_GET, []() {
        uint16_t freq = server.arg("f").toInt();
        uint16_t dur = server.arg("d").toInt();
        if (usbHostStarted) {
            NxtUsbHost::instance().playTone(freq, dur);
        }

        face.setExpression(EXP_SURPRISED);
        server.send(200, "application/json", "{\"status\":\"played\"}");
    });

    server.on("/api/status", HTTP_GET, []() {
        bool nxtOnline = usbHostStarted && NxtUsbHost::instance().isConnected();
        String json = "{\"nxt\":" + String(nxtOnline ? "true" : "false") +
                      ",\"host\":" + String(usbHostStarted ? "true" : "false") +
                      ",\"board\":" + String((int)M5.getBoard()) +
                      ",\"vbus\":" + String(M5.Power.Axp2101.isVBUS() ? "true" : "false") + "}";
        server.send(200, "application/json", json);
    });

    server.begin();
    updateStatusDisplay();
    Serial.println("[HTTP] Web server started.");
}

void loop() {
    M5.update();
    server.handleClient();
    face.update();

    // Safely activate USB Host only when AXP2101 is active AND PC/Charger VBUS is NOT present
    if (!usbHostStarted && M5.Power.getType() == m5::Power_Class::pmic_axp2101) {
        if (!M5.Power.Axp2101.isVBUS()) {
            if (unpluggedSinceMs == 0) {
                unpluggedSinceMs = millis();
            } else if (millis() - unpluggedSinceMs > 500) {
                Serial.println("[Power] No PC VBUS detected. Enabling 5V USB Host for Lego NXT...");
                M5.Power.setUsbOutput(true);
                delay(50);
                NxtUsbHost::instance().begin();
                usbHostStarted = true;
                updateStatusDisplay();
            }
        } else {
            unpluggedSinceMs = 0;
        }
    }

    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 1000) {
        lastCheck = millis();
        updateStatusDisplay();
    }

    delay(2);
}

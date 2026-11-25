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

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // 1. Initialize screen and face first
    M5.Display.setBrightness(180);
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

        // Update facial expression according to movement
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
                      ",\"host\":" + String(usbHostStarted ? "true" : "false") + "}";
        server.send(200, "application/json", json);
    });

    server.begin();
    Serial.println("[HTTP] Web server started.");
}

void loop() {
    M5.update();
    server.handleClient();
    face.update();

    // Dynamically manage USB Host:
    // If not connected to external PC/charger (e.g. running on battery connected to NXT),
    // safely enable 5V VBUS boost and start the USB Host stack.
    if (!usbHostStarted) {
        if (!M5.Power.Axp2101.isVBUS()) {
            Serial.println("[Power] Not connected to PC power. Starting USB Host for Lego NXT...");
            M5.Power.setUsbOutput(true);
            NxtUsbHost::instance().begin();
            usbHostStarted = true;
        }
    }

    delay(2);
}

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

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // 1. CoreS3 AXP2101 power: output 5V on USB-C port to power NXT USB transceiver
    M5.Power.setUsbOutput(true);
    M5.Power.setExtOutput(true);
    delay(200);

    // 2. Initialize Stack-chan animated face
    face.begin();
    face.setExpression(EXP_NORMAL);

    // 3. Start Lego Mindstorms NXT USB Host driver
    NxtUsbHost::instance().begin();

    // 4. Start Wi-Fi Access Point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP started! SSID: %s, Pass: %s\n", AP_SSID, AP_PASS);
    Serial.printf("[WiFi] Control WebUI: http://%s\n", WiFi.softAPIP().toString().c_str());

    // 5. Setup WebServer Endpoints
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/api/drive", HTTP_GET, []() {
        int left = server.arg("l").toInt();
        int right = server.arg("r").toInt();

        NxtUsbHost::instance().drive(left, right);

        // Update facial expression according to movement
        if (left > 0 && right > 0) face.setExpression(EXP_FORWARD);
        else if (left < 0 && right < 0) face.setExpression(EXP_BACKWARD);
        else if (left < right) face.setExpression(EXP_LEFT);
        else if (left > right) face.setExpression(EXP_RIGHT);

        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/stop", HTTP_GET, []() {
        NxtUsbHost::instance().stopMotors();
        face.setExpression(EXP_NORMAL);
        server.send(200, "application/json", "{\"status\":\"stopped\"}");
    });

    server.on("/api/tone", HTTP_GET, []() {
        uint16_t freq = server.arg("f").toInt();
        uint16_t dur = server.arg("d").toInt();
        NxtUsbHost::instance().playTone(freq, dur);

        face.setExpression(EXP_SURPRISED);
        server.send(200, "application/json", "{\"status\":\"played\"}");
    });

    server.on("/api/status", HTTP_GET, []() {
        String json = "{\"nxt\":" + String(NxtUsbHost::instance().isConnected() ? "true" : "false") + "}";
        server.send(200, "application/json", json);
    });

    server.begin();
    Serial.println("[HTTP] Web server started.");
}

void loop() {
    M5.update();
    server.handleClient();
    face.update();
    delay(2);
}

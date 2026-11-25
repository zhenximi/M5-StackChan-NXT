#pragma once
#include <M5Unified.h>

enum Expression {
    EXP_NORMAL,
    EXP_LEFT,
    EXP_RIGHT,
    EXP_FORWARD,
    EXP_BACKWARD,
    EXP_HAPPY,
    EXP_SURPRISED
};

class StackChanFace {
public:
    void begin() {
        M5.Display.setRotation(1);
        M5.Display.setBrightness(200);
        M5.Display.fillScreen(TFT_BLACK);

        _useCanvas = _canvas.createSprite(320, 240);
        if (!_useCanvas) {
            Serial.println("[Avatar] PSRAM canvas failed, falling back to direct display");
        }
        _lastBlink = millis();
        draw();
    }

    void setExpression(Expression exp) {
        if (_currentExp != exp) {
            _currentExp = exp;
            draw();
        }
    }

    void setStatusText(const String& text) {
        if (_statusText != text) {
            _statusText = text;
            draw();
        }
    }

    void update() {
        if (_currentExp == EXP_NORMAL && millis() - _lastBlink > 3500) {
            blink();
            _lastBlink = millis();
        }
    }

    void draw() {
        int eyeOffsetX = 0;
        int eyeOffsetY = 0;

        switch (_currentExp) {
            case EXP_LEFT:     eyeOffsetX = -18; break;
            case EXP_RIGHT:    eyeOffsetX = 18;  break;
            case EXP_FORWARD:  eyeOffsetY = -12; break;
            case EXP_BACKWARD: eyeOffsetY = 12;  break;
            default: break;
        }

        int eyeRadius = 24;
        int leftEyeX = 100 + eyeOffsetX;
        int rightEyeX = 220 + eyeOffsetX;
        int eyeY = 98 + eyeOffsetY;

        if (_useCanvas) {
            drawScene(_canvas, leftEyeX, rightEyeX, eyeY, eyeRadius, false);
            _canvas.pushSprite(0, 0);
        } else {
            M5.Display.startWrite();
            drawScene(M5.Display, leftEyeX, rightEyeX, eyeY, eyeRadius, false);
            M5.Display.endWrite();
        }
    }

    void blink() {
        if (_useCanvas) {
            drawScene(_canvas, 100, 220, 98, 24, true);
            _canvas.pushSprite(0, 0);
        } else {
            M5.Display.startWrite();
            drawScene(M5.Display, 100, 220, 98, 24, true);
            M5.Display.endWrite();
        }
        delay(120);
        draw();
    }

private:
    template <typename T>
    void drawScene(T& gfx, int leftEyeX, int rightEyeX, int eyeY, int eyeRadius, bool closed) {
        gfx.fillScreen(TFT_BLACK);

        if (closed) {
            gfx.fillRoundRect(75, eyeY - 2, 50, 6, 3, TFT_CYAN);
            gfx.fillRoundRect(195, eyeY - 2, 50, 6, 3, TFT_CYAN);
        } else {
            gfx.fillCircle(leftEyeX, eyeY, eyeRadius, TFT_CYAN);
            gfx.fillCircle(rightEyeX, eyeY, eyeRadius, TFT_CYAN);
            gfx.fillCircle(leftEyeX - 6, eyeY - 6, 6, TFT_WHITE);
            gfx.fillCircle(rightEyeX - 6, eyeY - 6, 6, TFT_WHITE);
        }

        if (_currentExp == EXP_SURPRISED) {
            gfx.fillCircle(160, 158, 15, TFT_PINK);
            gfx.fillCircle(160, 158, 10, TFT_BLACK);
        } else if (_currentExp == EXP_HAPPY || _currentExp == EXP_FORWARD) {
            gfx.fillTriangle(140, 148, 180, 148, 160, 168, TFT_WHITE);
            gfx.fillTriangle(145, 150, 175, 150, 160, 165, TFT_PINK);
        } else {
            gfx.fillRoundRect(145, 154, 30, 8, 4, TFT_WHITE);
        }

        if (_statusText.length() > 0) {
            gfx.fillRect(0, 218, 320, 22, 0x18E3);
            gfx.setTextColor(TFT_LIGHTGREY);
            gfx.setTextDatum(textdatum_t::middle_center);
            gfx.setTextSize(1);
            gfx.drawString(_statusText, 160, 229);
        }
    }

    M5Canvas _canvas{&M5.Display};
    bool _useCanvas = false;
    Expression _currentExp = EXP_NORMAL;
    uint32_t _lastBlink = 0;
    String _statusText = "WiFi: StackChan-NXT (192.168.4.1)";
};

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
        M5.Display.setBrightness(180); // Ensure backlight is ON
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
        int eyeY = 105 + eyeOffsetY;

        if (_useCanvas) {
            _canvas.fillScreen(TFT_BLACK);
            _canvas.fillCircle(leftEyeX, eyeY, eyeRadius, TFT_CYAN);
            _canvas.fillCircle(rightEyeX, eyeY, eyeRadius, TFT_CYAN);
            _canvas.fillCircle(leftEyeX - 6, eyeY - 6, 6, TFT_WHITE);
            _canvas.fillCircle(rightEyeX - 6, eyeY - 6, 6, TFT_WHITE);

            if (_currentExp == EXP_SURPRISED) {
                _canvas.fillCircle(160, 165, 15, TFT_PINK);
                _canvas.fillCircle(160, 165, 10, TFT_BLACK);
            } else if (_currentExp == EXP_HAPPY || _currentExp == EXP_FORWARD) {
                _canvas.fillTriangle(140, 155, 180, 155, 160, 175, TFT_WHITE);
                _canvas.fillTriangle(145, 157, 175, 157, 160, 172, TFT_PINK);
            } else {
                _canvas.fillRoundRect(145, 160, 30, 8, 4, TFT_WHITE);
            }
            _canvas.pushSprite(0, 0);
        } else {
            M5.Display.startWrite();
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.fillCircle(leftEyeX, eyeY, eyeRadius, TFT_CYAN);
            M5.Display.fillCircle(rightEyeX, eyeY, eyeRadius, TFT_CYAN);
            M5.Display.fillCircle(leftEyeX - 6, eyeY - 6, 6, TFT_WHITE);
            M5.Display.fillCircle(rightEyeX - 6, eyeY - 6, 6, TFT_WHITE);

            if (_currentExp == EXP_SURPRISED) {
                M5.Display.fillCircle(160, 165, 15, TFT_PINK);
                M5.Display.fillCircle(160, 165, 10, TFT_BLACK);
            } else if (_currentExp == EXP_HAPPY || _currentExp == EXP_FORWARD) {
                M5.Display.fillTriangle(140, 155, 180, 155, 160, 175, TFT_WHITE);
                M5.Display.fillTriangle(145, 157, 175, 157, 160, 172, TFT_PINK);
            } else {
                M5.Display.fillRoundRect(145, 160, 30, 8, 4, TFT_WHITE);
            }
            M5.Display.endWrite();
        }
    }

    void blink() {
        if (_useCanvas) {
            _canvas.fillScreen(TFT_BLACK);
            _canvas.fillRoundRect(75, 103, 50, 6, 3, TFT_CYAN);
            _canvas.fillRoundRect(195, 103, 50, 6, 3, TFT_CYAN);
            _canvas.fillRoundRect(145, 160, 30, 8, 4, TFT_WHITE);
            _canvas.pushSprite(0, 0);
        } else {
            M5.Display.startWrite();
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.fillRoundRect(75, 103, 50, 6, 3, TFT_CYAN);
            M5.Display.fillRoundRect(195, 103, 50, 6, 3, TFT_CYAN);
            M5.Display.fillRoundRect(145, 160, 30, 8, 4, TFT_WHITE);
            M5.Display.endWrite();
        }
        delay(120);
        draw();
    }

private:
    M5Canvas _canvas{&M5.Display};
    bool _useCanvas = false;
    Expression _currentExp = EXP_NORMAL;
    uint32_t _lastBlink = 0;
};

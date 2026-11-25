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
        _canvas.setColorDepth(16);
        _canvas.createSprite(320, 240);
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
        // Automatic natural blink every 3.5 seconds when idle
        if (_currentExp == EXP_NORMAL && millis() - _lastBlink > 3500) {
            blink();
            _lastBlink = millis();
        }
    }

    void draw() {
        _canvas.fillScreen(TFT_BLACK);

        int eyeOffsetX = 0;
        int eyeOffsetY = 0;

        switch (_currentExp) {
            case EXP_LEFT:     eyeOffsetX = -18; break;
            case EXP_RIGHT:    eyeOffsetX = 18;  break;
            case EXP_FORWARD:  eyeOffsetY = -12; break;
            case EXP_BACKWARD: eyeOffsetY = 12;  break;
            default: break;
        }

        // Eyes
        int eyeRadius = 24;
        int leftEyeX = 100 + eyeOffsetX;
        int rightEyeX = 220 + eyeOffsetX;
        int eyeY = 105 + eyeOffsetY;

        _canvas.fillCircle(leftEyeX, eyeY, eyeRadius, TFT_CYAN);
        _canvas.fillCircle(rightEyeX, eyeY, eyeRadius, TFT_CYAN);

        // Eye glint highlights
        _canvas.fillCircle(leftEyeX - 6, eyeY - 6, 6, TFT_WHITE);
        _canvas.fillCircle(rightEyeX - 6, eyeY - 6, 6, TFT_WHITE);

        // Mouth
        if (_currentExp == EXP_SURPRISED) {
            _canvas.fillCircle(160, 165, 15, TFT_PINK);
            _canvas.fillCircle(160, 165, 10, TFT_BLACK);
        } else if (_currentExp == EXP_HAPPY || _currentExp == EXP_FORWARD) {
            // Big open smile
            _canvas.fillTriangle(140, 155, 180, 155, 160, 175, TFT_WHITE);
            _canvas.fillTriangle(145, 157, 175, 157, 160, 172, TFT_PINK);
        } else {
            // Calm rounded mouth line
            _canvas.fillRoundRect(145, 160, 30, 8, 4, TFT_WHITE);
        }

        _canvas.pushSprite(0, 0);
    }

    void blink() {
        _canvas.fillScreen(TFT_BLACK);
        // Closed eye slits
        _canvas.fillRoundRect(75, 103, 50, 6, 3, TFT_CYAN);
        _canvas.fillRoundRect(195, 103, 50, 6, 3, TFT_CYAN);
        // Mouth
        _canvas.fillRoundRect(145, 160, 30, 8, 4, TFT_WHITE);
        _canvas.pushSprite(0, 0);

        delay(130);
        draw();
    }

private:
    M5Canvas _canvas{&M5.Display};
    Expression _currentExp = EXP_NORMAL;
    uint32_t _lastBlink = 0;
};

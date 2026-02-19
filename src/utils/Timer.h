#ifndef TIMER_H
#define TIMER_H

#include <Arduino.h>

namespace Utils {

    class Timer {
    private:
        unsigned long previousMillis;
        unsigned long interval;
        bool enabled;

    public:
        Timer(unsigned long intervalMs = 1000, bool startEnabled = true);

        bool isReady();
        void reset();
        void setInterval(unsigned long intervalMs);
        void enable() { enabled = true; }
        void disable() { enabled = false; }
        bool isEnabled() const { return enabled; }
        unsigned long getElapsed() const;
        unsigned long getRemaining() const;
    };

    class ButtonDebouncer {
    private:
        int pin;
        bool lastState;
        bool currentState;
        unsigned long lastDebounceTime;
        unsigned long debounceDelay;
        unsigned long pressStartTime;
        unsigned long lastPressDuration;
        bool isPressed;
        bool justPressed;
        bool justReleased;

    public:
        ButtonDebouncer(int buttonPin, unsigned long debounceMs = 50);

        void update();
        bool wasPressed();
        bool wasReleased();
        bool isCurrentlyPressed() const { return isPressed; }
        unsigned long getPressedDuration() const;
    };

}

#endif
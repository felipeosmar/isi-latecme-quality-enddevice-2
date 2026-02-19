#include "Timer.h"

namespace Utils {

    Timer::Timer(unsigned long intervalMs, bool startEnabled)
        : previousMillis(0), interval(intervalMs), enabled(startEnabled) {
    }

    bool Timer::isReady() {
        if (!enabled) return false;

        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            return true;
        }
        return false;
    }

    void Timer::reset() {
        previousMillis = millis();
    }

    void Timer::setInterval(unsigned long intervalMs) {
        interval = intervalMs;
    }

    unsigned long Timer::getElapsed() const {
        return millis() - previousMillis;
    }

    unsigned long Timer::getRemaining() const {
        unsigned long elapsed = getElapsed();
        if (elapsed >= interval) return 0;
        return interval - elapsed;
    }

    // ButtonDebouncer implementation
    ButtonDebouncer::ButtonDebouncer(int buttonPin, unsigned long debounceMs)
        : pin(buttonPin),
          lastState(HIGH),
          currentState(HIGH),
          lastDebounceTime(0),
          debounceDelay(debounceMs),
          pressStartTime(0),
          lastPressDuration(0),
          isPressed(false),
          justPressed(false),
          justReleased(false) {
        pinMode(pin, INPUT_PULLUP);
    }

    void ButtonDebouncer::update() {
        justPressed = false;
        justReleased = false;

        int reading = digitalRead(pin);

        if (reading != lastState) {
            lastDebounceTime = millis();
        }

        if ((millis() - lastDebounceTime) > debounceDelay) {
            if (reading != currentState) {
                currentState = reading;

                if (currentState == LOW && !isPressed) {
                    isPressed = true;
                    justPressed = true;
                    pressStartTime = millis();
                } else if (currentState == HIGH && isPressed) {
                    lastPressDuration = millis() - pressStartTime;
                    isPressed = false;
                    justReleased = true;
                }
            }
        }

        lastState = reading;
    }

    bool ButtonDebouncer::wasPressed() {
        return justPressed;
    }

    bool ButtonDebouncer::wasReleased() {
        return justReleased;
    }

    unsigned long ButtonDebouncer::getPressedDuration() const {
        if (isPressed) {
            return millis() - pressStartTime;
        }
        return lastPressDuration;
    }

}
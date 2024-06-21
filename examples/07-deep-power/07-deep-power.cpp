#include "AB1805_RK.h"

SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(SEMI_AUTOMATIC);

Serial1LogHandler logHandler(115200, LOG_LEVEL_TRACE);

AB1805 ab1805(Wire);

bool buttonPressed = false;

void buttonHandler(system_event_t event, int data);

void setup() {
    System.on(button_click, buttonHandler);
    
    ab1805.withFOUT(WKP).setup();

    AB1805::WakeReason wakeReason = ab1805.getWakeReason();
    if (wakeReason == AB1805::WakeReason::DEEP_POWER_DOWN) {
        Log.info("woke from DEEP_POWER_DOWN");
    }

    ab1805.resetConfig();
    
    //ab1805.setWDT(AB1805::WATCHDOG_MAX_SECONDS);
    Particle.connect();
}


void loop() {
    ab1805.loop();

    if (buttonPressed) {
        buttonPressed = false;

        Log.info("deep power down - disconecting");
        
        Particle.disconnect();
        waitFor(Particle.disconnected, 10000);

        Cellular.off();
        waitFor(Cellular.isOff, 10000);

        Log.info("going into deep power down");

        ab1805.deepPowerDown(60);
    }

}

void buttonHandler(system_event_t event, int data) {
    buttonPressed = true;
}


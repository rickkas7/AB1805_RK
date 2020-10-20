
#include "AB1805_RK.h"

SYSTEM_THREAD(ENABLED);
// SYSTEM_MODE(MANUAL);

SerialLogHandler logHandler;

AB1805 ab1805(Wire);

enum {
	TEST_IDLE = 0,              // 0 Don't do anything
    TEST_DEEP_POWER_DOWN_30,    // 1 Deep power down (EN pin) for 30 seconds  
    TEST_WDT,                   // 2 Test watchdog reset 
    TEST_HIBERNATE_30,          // 3 Hibernate for 30 seconds 
    TEST_STOP_30,               // 4 Stop sleep for 30 seconds 
    TEST_ALARM_30,              // 5 Hibernate using alarm wake
    TEST_REPEAT_60,             // 6 Repeating stop sleep interrupt 60 secs arg is num repeats (default = 2)
    TEST_REPEAT_60_RUN,         // Used after setting the setting once to make sure it repeats
    TEST_RAM,                   // 8 Test RAM functions
    TEST_LAST
};
const size_t MAX_PARAM = 4;
int testNum = 0;
int intParam[MAX_PARAM];
String stringParam[MAX_PARAM];
size_t numParam;
int repeatsLeft = 0;

int testHandler(String cmd);
void testRam();

void setup() {
	Particle.function("test", testHandler);

    // Optional: Enable to make it easier to see debug USB serial messages at startup
    waitFor(Serial.isConnected, 15000);
    delay(1000);

    ab1805.withFOUT(D8).setup();

    // AB1805::PRESERVE_REPEATING_TIMER
    ab1805.resetConfig();

    Log.info("using %s oscillator", ab1805.usingRCOscillator() ? "RC" : "crystal");

    ab1805.setWDT(AB1805::WATCHDOG_MAX_SECONDS);

}

void loop() {
    ab1805.loop();

    switch(testNum) {
    case TEST_DEEP_POWER_DOWN_30: { // 1
        Log.info("deepPowerDown(30)");
        ab1805.deepPowerDown(30);

        // Powerdown reset should occur here
        Log.error("deepPortDown failed");
        testNum = 0;
        break;
    }

    case TEST_WDT: { // 2
        Log.info("test WDT, this may take a few minutes");

        // Maximum watchdog period is 124 seconds
        delay(130s);

        Log.error("watchdog failed to trigger");
        testNum = 0;
        break;
    }

    case TEST_HIBERNATE_30: { // 3
        // Set an interrupt in 30 seconds
        ab1805.interruptCountdownTimer(30, false);

        SystemSleepConfiguration config;
        config.mode(SystemSleepMode::HIBERNATE)
            .gpio(D8, FALLING);
        System.sleep(config);

        // System should reset here
        Log.error("TEST_HIBERNATE_30 failed");
        testNum = 0;
        break;
    }

    case TEST_STOP_30: { // 4
        // Set an interrupt in 30 seconds
        ab1805.interruptCountdownTimer(30, false);

        SystemSleepConfiguration config;
        config.mode(SystemSleepMode::STOP)
            .gpio(D8, FALLING);
        System.sleep(config);

        waitFor(Serial.isConnected, 15000);
        delay(1000);

        // Execution continues after sleep, update the reason for wake here

        ab1805.updateWakeReason();
        testNum = 0;
        break;
    }

    case TEST_ALARM_30: {   // 5
        time_t time;
        ab1805.getRtcAsTime(time);
        ab1805.interruptAtTime(time + 30);

        SystemSleepConfiguration config;
        config.mode(SystemSleepMode::HIBERNATE)
            .gpio(D8, FALLING);
        System.sleep(config);

        // System should reset here
        Log.error("TEST_ALARM_30 failed");
        testNum = 0;
        break;
    }

    case TEST_REPEAT_60:    // 6 Repeating stop sleep interrupt 60 secs
    case TEST_REPEAT_60_RUN: { 
        if (!Particle.connected()) {
            break;
        }
        if (testNum == TEST_REPEAT_60_RUN) {
            if (--repeatsLeft <= 0) {
                testNum = 0;
                Log.info("test complete");
                ab1805.clearRepeatingInterrupt();
                break;
            }
        }

        delay(2000);
        Log.info("Sleeping now. repeatsLeft=%d", repeatsLeft);

        if (testNum == TEST_REPEAT_60) {
            testNum = TEST_REPEAT_60_RUN;
            struct tm timestruct;
            timestruct.tm_sec = 30;
            ab1805.repeatingInterrupt(&timestruct, AB1805::REG_TIMER_CTRL_RPT_SEC);

            if (numParam > 0) {
                repeatsLeft = intParam[0];
            }
            else {
                repeatsLeft = 2;
            }
        }

        SystemSleepConfiguration config;
        config.mode(SystemSleepMode::STOP)
            .gpio(D8, FALLING);
        System.sleep(config);

        waitFor(Serial.isConnected, 15000);
        delay(1000);

        // Execution continues after sleep, update the reason for wake here

        ab1805.updateWakeReason();
        break;
    }


    case TEST_RAM: { // 8
        testRam();
        testNum = 0;
        break;
    }

    case TEST_IDLE: 
        break;
    }
}



int testHandler(String cmd) {
	char *mutableCopy = strdup(cmd.c_str());

	char *cp = strtok(mutableCopy, ",");

	int tempTestNum = atoi(cp);

	switch(tempTestNum) {
	default:
		testNum = tempTestNum;

		for(numParam = 0; numParam < MAX_PARAM; numParam++) {
			cp = strtok(NULL, ",");
			if (!cp) {
				break;
			}
			intParam[numParam] = atoi(cp);
			stringParam[numParam] = cp;
		}
		for(size_t ii = numParam; ii < MAX_PARAM; ii++) {
			intParam[ii] = 0;
			stringParam[ii] = "";
		}
		break;
	}

	free(mutableCopy);
	return 0;
}

void testRam() {
    Log.info("testRam started");

    // Test 8
    ab1805.eraseRam();

    for(size_t ii = 0; ii < 256; ii++) {
        uint8_t value;
        ab1805.get(ii, value);
        if (value != 0) {
            Log.error("testRam failed ii=%u value=0x%02x line=%u", ii, value, __LINE__);
            break;
        }
    }

    uint8_t buf[256], buf2[256];

    for(size_t ii = 0; ii < 256; ii++) {
        buf[ii] = (uint8_t)rand();
        ab1805.put(ii, buf[ii]);
    }

    for(size_t ii = 0; ii < 256; ii++) {
        uint8_t value2;
        ab1805.get(ii, value2);

        if (buf[ii] != value2) {
            Log.error("testRam failed ii=%u value1=0x%02x value2=0x%02x line=%u", ii, buf[ii], value2, __LINE__);
            break;
        }
    }

    for(size_t ii = 0; ii < 64; ii++) {
        uint32_t value1 = (uint32_t)rand();
        ab1805.put(ii * 4, value1);

        uint32_t value2;
        ab1805.get(ii * 4, value2);

        if (value1 != value2) {
            Log.error("testRam failed ii=%u value1=0x%lx value2=0x%lx line=%u", ii, value1, value2, __LINE__);
            break;
        }
    }

    for(size_t ii = 0; ii < 256; ii++) {
        buf[ii] = (uint8_t)rand();
    }
    ab1805.writeRam(0, buf, sizeof(buf));

    for(size_t ii = 0; ii < 256; ii++) {
        uint8_t value2;
        ab1805.get(ii, value2);

        if (buf[ii] != value2) {
            Log.error("testRam failed ii=%u value1=0x%02x value2=0x%02x line=%u", ii, buf[ii], value2, __LINE__);
            break;
        }
    }
    
    ab1805.readRam(0, buf2, sizeof(buf2));
    for(size_t ii = 0; ii < 256; ii++) {
        if (buf[ii] != buf2[ii]) {
            Log.error("testRam failed ii=%u value1=0x%02x value2=0x%02x line=%u", ii, buf[ii], buf2[ii], __LINE__);
            break;
        }
    }

    {
        uint32_t value1 = (uint32_t)rand();
        ab1805.put(125, value1);

        uint32_t value2;
        ab1805.get(125, value2);

        if (value1 != value2) {
            Log.error("testRam failed value1=0x%lx value2=0x%lx line=%u", value1, value2, __LINE__);
        }
    }

    ab1805.eraseRam();

    Log.info("testRam complete");
}
#include "Particle.h"

SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(MANUAL);

SerialLogHandler logHandler;

// For Tracker SoM, use Wire1 instead of Wire.
TwoWire &wire = Wire;

const uint8_t AB1805_I2C_ADDR = 0x69;
const unsigned long SCAN_PERIOD_MS = 15000;
unsigned long lastScan = 0;

bool i2cscan();
void chipTest();

void setup() { 
    wire.begin();
}

void loop() {

	if (millis() - lastScan >= SCAN_PERIOD_MS) {
		lastScan = millis();
		if (i2cscan()) {
            chipTest();
        }
	}

}

bool i2cscan() {
    bool bFound = false;

	Log.info("Scanning I2C bus...");

	int numDevices = 0;

	// Address 0x79 to 0x7f are reserved, don't scan them
	for(byte address = 1; address < 0x78; address++) {
		wire.beginTransmission(address);
		byte error = wire.endTransmission();

		if (error == 0) {
			const char *deviceName = NULL;

			switch(address) {
			case AB1805_I2C_ADDR: // 0x69
				deviceName = "AB1805";
				break;
			}
			if (deviceName != NULL) {
				Log.info("%s found at address 0x%02x", deviceName, address);
                bFound = true;
			}
			else {
				Log.info("Unknown I2C device found at address 0x%02x", address);
			}

			numDevices++;
		}
		else if (error == 4) {
			Log.info("Unknown error at address 0x%2x", address);
		}
	}

	Log.info("%d devices found", numDevices);

    return bFound;
}

uint8_t readRegister(uint8_t regAddr) {
    wire.beginTransmission(AB1805_I2C_ADDR);
    wire.write(regAddr);
    wire.endTransmission(false);
    size_t count = wire.requestFrom(AB1805_I2C_ADDR, 1, true);
    if (count != 1) {
        Log.error("failed to read regAddr=%02x", regAddr);
        return 0;
    }
    
    uint8_t value = wire.read();

    Log.info("regAddr=%02x value=%02x", regAddr, value);

    return value;
}

void chipTest() {
    Log.info("running chipTest");

    // These are the ID0 - ID6 registers
    for(uint8_t regAddr = 0x28; regAddr <= 0x2E; regAddr++) {
        readRegister(regAddr);
    }
}

/*
0000030000 [app] INFO: Scanning I2C bus...
0000035408 [app] INFO: AB1805 found at address 0x69
0000036136 [app] INFO: 1 devices found
0000036136 [app] INFO: regAddr=28 value=18
0000036138 [app] INFO: regAddr=29 value=05
0000036139 [app] INFO: regAddr=2a value=13
0000036139 [app] INFO: regAddr=2b value=72
0000036140 [app] INFO: regAddr=2c value=dc
0000036142 [app] INFO: regAddr=2d value=b5
0000036143 [app] INFO: regAddr=2e value=b0
*/

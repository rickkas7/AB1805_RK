# AB1805_RK

*Library for AB1805/AM1805 RTC/Watchdog for Particle devices*

You can [browse the generated API documentation](https://rickkas7.github.io/AB1805_RK/index.html).

The examples and AB1805_RK.h header file should be mostly self-explanatory. There will be a Particle application note that shows the hardware design used with this library shortly.

## Examples

### 01-minimal

This is just the minimal implement of using the RTC and Watchdog Timer (WDT). Here's the complete code:

```cpp
#include "AB1805_RK.h"

SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(SEMI_AUTOMATIC);

SerialLogHandler logHandler;

AB1805 ab1805(Wire);

void setup() {
    // The sample board has D8 (Argon/Boron) or D10 (Photon 2) connected to FOUT for wake interrupts
    // though this example does not use this feature.
    ab1805.withFOUT(WKP).setup();

    // Reset the AB1805 configuration to default values
    ab1805.resetConfig();

    // Enable watchdog
    ab1805.setWDT(AB1805::WATCHDOG_MAX_SECONDS);

    // Connect to the Particle cloud
    Particle.connect();
}


void loop() {
    // Be sure to call ab1805.loop() on every call to loop()
    ab1805.loop();
}

```

Things to note in this code:

Declare an `AB1805` object in your code as a global variable. Only do this once in your main source file. The parameter is the I2C interface the AB1805 is connected to, typically `Wire` (D0/D1).

```cpp
AB1805 ab1805(Wire);
```

In setup(), call the `ab1805.setup()` method.

```cpp
    ab1805.setup();
```

Reset the settings on the AB1805. This isn't strictly necessary since it resets the chip to power-on defaults, but it's not a bad idea to be safe:

```cpp
    ab1805.resetConfig();
```

If you want to use the hardware watchdog, enable it:

```cpp
    ab1805.setWDT(AB1805::WATCHDOG_MAX_SECONDS);
```

And from loop(), make sure you call the loop method:

```cpp
    ab1805.loop();
```

The `ab1805.loop()` method takes care of:

- Serving the watchdog timer.
- Synchronizing the hardware RTC with the cloud time.
- Turning off the watchdog before System.reset() in case an OTA firmware update is in progress.


### 02-typical

The "typical" example adds in a few helpful features:

- An out of memory handler, which will reset the device if a RAM allocation fails.
- A failure to connect detector, so if it takes longer than 11 minutes to connect to the cloud, a deep power down for 30 seconds is done to hopefully reset things complete.

Full firmware:

```cpp
#include "AB1805_RK.h"

SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(SEMI_AUTOMATIC);

SerialLogHandler logHandler;

// This is the maximum amount of time to allow for connecting to cloud. If this time is
// exceeded, do a deep power down. This should not be less than 10 minutes. 11 minutes
// is a reasonable value to use.
const std::chrono::milliseconds connectMaxTime = 11min;

AB1805 ab1805(Wire);
int outOfMemory = -1;
bool cloudConnected = false;
uint64_t cloudConnectStarted = 0;

void outOfMemoryHandler(system_event_t event, int param);

void setup() {
    // Enabling an out of memory handler is a good safety tip. If we run out of
    // memory a System.reset() is done.
    System.on(out_of_memory, outOfMemoryHandler);
    
    // Optional: Enable to make it easier to see debug USB serial messages at startup
    waitFor(Serial.isConnected, 15000);
    delay(1000);

    // Make sure you set up the AB1805 library from setup()!
    ab1805.setup();

    // This is how to check if we did a deep power down (optional)
    AB1805::WakeReason wakeReason = ab1805.getWakeReason();
    if (wakeReason == AB1805::WakeReason::DEEP_POWER_DOWN) {
        Log.info("woke from DEEP_POWER_DOWN");
    }

    // Reset the AB1805 configuration to default values
    ab1805.resetConfig();
    
    // If using the supercap, enable trickle charging here. 
    // Do not enable this for the AB1805-Li example!
    // ab1805.setTrickle(AB1805::REG_TRICKLE_DIODE_0_3 | AB1805::REG_TRICKLE_ROUT_3K);
    
    // Enable watchdog
    ab1805.setWDT(AB1805::WATCHDOG_MAX_SECONDS);

    // Connect to the Particle cloud
    Particle.connect();
}


void loop() {
    // Be sure to call ab1805.loop() on every call to loop()
    ab1805.loop();

    if (outOfMemory >= 0) {
        // An out of memory condition occurred - reset device.
        Log.info("out of memory occurred size=%d", outOfMemory);
        delay(100);

        System.reset();
    }

    // Monitor the cloud connection state and do a deep power down if a 
    // failure to connect exceeds connectMaxTime (typically 11 minutes).
    if (Particle.connected()) {
        if (!cloudConnected) {
            cloudConnected = true;
            uint32_t elapsed = (uint32_t)(System.millis() - cloudConnectStarted);
            Log.info("cloud connected in %lu ms", elapsed);
        }
    }
    else {
        if (cloudConnected) {
            cloudConnected = false;
            cloudConnectStarted = System.millis();
            Log.info("lost cloud connection");
        }
        uint32_t elapsed = (uint32_t)(System.millis() - cloudConnectStarted);
        if (elapsed > connectMaxTime.count()) {
            Log.info("failed to connect to cloud, doing deep reset");
            delay(100);
            ab1805.deepPowerDown();
        }
    }
}

void outOfMemoryHandler(system_event_t event, int param) {
    outOfMemory = param;
}
```

#### Out of Memory Handler

The out of memory handler is a good feature to have in your code. This is different than a check of `System.freeMemory()`. The out of memory handler is called when an allocation fails and returns NULL. Note that this is not in itself fatal, as your code might then do something to free up some memory and try again. However, in practice, if you are running that low on RAM, a reset is often a reasonable alternative. C/C++ do not have garbage collection and thus it's possible for memory to be fragmented into unusably small chunks. A reset is the only way to clean this up in most cases. Note that on Gen 2 devices and Gen 3 devices with Device OS 2.0.0 and later, a System.reset() should be fast because it stays connected to the cellular modem.


You need a global variable, because it's not a good idea to reset directly from the system event handler.

```cpp
int outOfMemory = -1;
```

You need to register the out of memory handler from setup():

```cpp
System.on(out_of_memory, outOfMemoryHandler);
```

The handler function just sets the global variable:

```cpp
void outOfMemoryHandler(system_event_t event, int param) {
    outOfMemory = param;
}
```

And finally, from loop(), we take action if outOfMemory is >= 0:

```cpp
    if (outOfMemory >= 0) {
        // An out of memory condition occurred - reset device.
        Log.info("out of memory occurred size=%d", outOfMemory);
        delay(100);

        System.reset();
    }
```

#### Connection Failure Deep Power Off

You can configure the amount of time to fail to connect to the cloud before doing a deep power off for 30 seconds. The default is 11 minutes, and you should not set it less than 10. You can set it higher if you want.

```cpp
const std::chrono::milliseconds connectMaxTime = 11min;
```

These global variables are used:

```cpp
bool cloudConnected = false;
uint64_t cloudConnectStarted = 0;
```

And this code is added to loop to do the detection:

```cpp
    if (Particle.connected()) {
        if (!cloudConnected) {
            cloudConnected = true;
            uint32_t elapsed = (uint32_t)(System.millis() - cloudConnectStarted);
            Log.info("cloud connected in %lu ms", elapsed);
        }
    }
    else {
        if (cloudConnected) {
            cloudConnected = false;
            cloudConnectStarted = System.millis();
            Log.info("lost cloud connection");
        }
        uint32_t elapsed = (uint32_t)(System.millis() - cloudConnectStarted);
        if (elapsed > connectMaxTime.count()) {
            Log.info("failed to connect to cloud, doing deep reset");
            delay(100);
            ab1805.deepPowerDown();
        }
    }
```

This code just writes a message to debug serial both before resetting and after. However you may want to add some code to publish an event so you can keep track of how often this is happening. The wake reason is set during setup() and will remain valid so you can perform this check as necessary. Note that the existing block in setup() is a bad place to put a publish as you're not yet cloud connected.

```
    AB1805::WakeReason wakeReason = ab1805.getWakeReason();
    if (wakeReason == AB1805::WakeReason::DEEP_POWER_DOWN) {
        Log.info("woke from DEEP_POWER_DOWN");
    }
```


### 03-periodic-wake

This example has the device wake up once per hour and publish a value. It illustrates:

- Using the RTC to wake up the device periodically.
- Using the 256-byte non-volatile RAM in the RTC.
- An out of memory handler, which will reset the device if a RAM allocation fails.
- A failure to connect detector, so if it takes longer than 11 minutes to connect to the cloud, a deep power down for 30 seconds is done to hopefully reset things complete.
 
### 04-selftest

The self-test code is used to do a quick check of the hardware to make sure it's visible by I2C. It's helpful when you've soldered up a new board to make sure the AB1805 at least has working I2C communications.

### 05-hwtest

This is the hardware test suite that allows various modes to be tested via commands from the cloud.

### 06-supercap

This is an example of using a board with a super capacitor.

- It enables the supercap trickle charger
- When the MODE button is tapped, the device goes into 30 second deep power down (with the RTC powered by supercap)

### 07-deep-power

This is the deep power down example that uses a LiPo powered RTC to a deep power down, not using a supercap.

- When the MODE button is tapped, the device goes into 30 second deep power down (with the RTC powered by the LiPo)

## Version history

### 0.0.4 (2024-08-28)

- Added a check to prevent the loop() function from blocking while connecting to the cloud if the time is set.

### 0.0.3 (2024-06-14)

- Switch hardcoding of pin D8 to WKP. On the Photon 2, the WKP pin is D10 but is in the same position as D8.

### 0.0.2 (2024-05-29)

- Disable SET_D8_LOW by default; it was accidentally left on in the previous version.
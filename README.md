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
    // The sample board has D8 connected to FOUT for wake interrupts, though this
    // example does not use this feature.
    ab1805.withFOUT(D8).setup();

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

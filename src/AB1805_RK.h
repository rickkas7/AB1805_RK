#ifndef __AB1805RK_H
#define __AB1805RK_H

#include "Particle.h"

#include <time.h> // struct tm

/**
 * @brief Class for using the AB1805/AM1805 RTC/watchdog chip
 * 
 * You typically allocate one of these objects as a global variable as 
 * a singleton. You can only have one of these objects per device.
 * 
 * Be sure you call the setup() and loop() methods of the AB1805 object!
 */
class AB1805 {
public:
    /**
     * @brief Return codes for getWakeReason()
     */
    enum class WakeReason {
        UNKNOWN,            //!< Wake reason is not know (may be from something other than RTC)
        WATCHDOG,           //!< Watchdog trigged reset
        DEEP_POWER_DOWN,    //!< The deepPowerDown() function was used (RTC was in sleep mode)
        COUNTDOWN_TIMER,    //!< The interruptCountdownTimer() was used
        ALARM               //!< RTC clock alarm (periodic or single) trigged wake
    };

    /**
     * @brief Construct the AB1805 driver object
     *
     * @param wire The I2C (TwoWire) inteface to use. Usually `Wire`. 
     * 
     * @param i2cAddr The I2C address. This is always 0x69 on the AB1805 as the
     * address is not configurable.  
     *  
     * You typically allocate one of these objects as a global variable as 
     * a singleton. You can only have one of these objects per device.
     */
    AB1805(TwoWire &wire = Wire, uint8_t i2cAddr = 0x69);

    /**
     * @brief Destructor. Not normally used as this object is typically a global object.
     */
    virtual ~AB1805();

    /**
     * @brief Call this from main setup() to initialize the library.
     * 
     * @param callBegin Whether to call wire.begin(). Default is true.
     */
    void setup(bool callBegin = true);

    /**
     * @brief Call this from main loop(). Should be called on every call to loop().
     */
    void loop();

    /**
     * @brief Call this before AB1805::setup() to specify the pin connected to FOUT/nIRQ.
     * 
     * @param pin The pin (like `D8`) that is connected to FOUT.
     * 
     * @return An AB1805& so you can chain the withXXX() calls, fluent-style-
     * then call the AB1805::setup() method.
     * 
     * This must be called before setup(). Default is `PIN_INVALID` which signifies
     * that FOUT is not connected.
     * 
     * This is used during chip detection as FOUT goes high after the AB1805
     * is initialized. While FOUT is low, the I2C interface is not yet ready.
     * 
     * The FOUT/nIRQ pin is also used for one-time and periodic interrupts.
     */
    AB1805 &withFOUT(pin_t pin) { foutPin = pin; return *this; };


    /**
     * @brief Checks the I2C bus to make sure there is an AB1805 present
     * 
     * This is called during AB1805::setup().
     */
    bool detectChip();

    /**
     * @brief Check oscillator
     * 
     * @return true if RC oscillator is being used, false if XT (crystal)
     */
    bool usingRCOscillator();

    /**
     * @brief Returns true if the RTC has been set
     * 
     * On cold power-up before cloud connecting, this will be false. Note that
     */
    bool isRTCSet() { return isBitClear(REG_CTRL_1, REG_CTRL_1_WRTC); };

    /**
     * @brief Gets the reason the device was reset or woken. For example, TIMER, ALARM, WATCHDOG, etc.
     * 
     * This is set during AB1805::setup() automatically (for HIBERNATE, ULP, and SLEEP_MODE_DEEP) as well as
     * normal reset, however if you are using STOP mode sleep, you must call `updateWakeReason()` after
     * System.sleep() returns to update the wake reason.
     */
    WakeReason getWakeReason() const { return wakeReason; };

    /**
     * @brief Update the wake reason. This is needed after STOP mode System.sleep()
     * 
     * The wake reason is set during AB1805::setup() automatically (for HIBERNATE, ULP, and SLEEP_MODE_DEEP) as well as
     * normal reset, however if you are using STOP mode sleep, you must call `updateWakeReason()` after
     * System.sleep() returns to update the wake reason.
     */
    bool updateWakeReason();

    /**
     * @brief Set or reset the watchdog timer. 
     * 
     * @param seconds Duration of watchdog timer or -1 to tickle/pet/service the watchdog. 
     * 
     * Minimum is 4 and maximum is 124 seconds. 0 disables the watchdog timer.
     * The constant `WATCHDOG_MAX_SECONDS` is 124 and is a good choice. 
     * -1 resets the timer to the previous setting and is used to tickle/pet/service the 
     * watchdog timer. This is done from AB1805::loop().
     * 
     * Periodically servicing the watchdog (-1) is handled automatically in AB1805::loop()
     * so you normally don't need to worry about it. Since it requires an I2C transaction
     * you probably don't want to call it on every loop. 
     */
    bool setWDT(int seconds = -1);

    /**
     * @brief Stops the watchdog timer. Useful before entering sleep mode.
     * 
     * This is done automatically right before reset (using the reset system event)
     * so the watchdog won't trigger during a firmware update. 
     */
    bool stopWDT() { return setWDT(0); };

    /**
     * @brief Resumes watchdog with same settings as before
     * 
     * This is useful after returning from `System.sleep()` in STOP or ULTRA_LOW_POWER
     * sleep modes where executinon continues. You may also want to call `updateWakeReason()`.
     * 
     * It's safe to call resumeWDT() even if the WDT has never been set, it does nothing
     * in this case, leaving the watchdog off.
     */
    bool resumeWDT() { return setWDT(-1); };

    /**
     * @brief Get the time from the RTC as a time_t
     * 
     * @param time Filled in with the number of second since January 1, 1970 UTC.
     * 
     * @return true on success or false if an error occurs.
     * 
     * The time value is basically the same as what would be returned from `Time.now()`
     * except it's retrieved from the AB1805 RTC instead of the system. However both
     * should approximately equal to each other.
     */
    bool getRtcAsTime(time_t &time);

    /**
     * @brief Get the time from the RTC as a struct tm
     * 
     * @param timeptr pointer to struct tm. Filled in with the current time, UTC.
     * 
     * @return true on success or false if an error occurs.
     * 
     * Note: This uses mktime, which technically is local time, not UTC. However,
     * the standard library is set at UTC as the local time. Using Time.zone() changes
     * the timezone in the `Time` class but does not modifying the underling 
     * standard C library, so mktime should always be UTC.
     * 
     * The fields of the timeptr are:
     * - tm_sec	  seconds after the minute	0-61 (usually 0-59)
     * - tm_min	  minutes after the hour 0-59
     * - tm_hour  hours since midnight 0-23
     * - tm_mday  day of the month 1-31
     * - tm_mon	  months since January 0-11 (not 1-12!)
     * - tm_year  years since 1900 (note: 2020 = 120)
     * - tm_wday  days since Sunday	0-6
     */
    bool getRtcAsTm(struct tm *timeptr);

    /**
     * @brief Resets the configuration of the AB1805 to default settings
     * 
     * @param flags flags to customize reset behavior (default: 0)
     * 
     * @return true on success or false if an error occurs.
     * 
     * The only exception currently defined is `AB1805::RESET_PRESERVE_REPEATING_TIMER` that
     * keeps repeating timers programmed when resetting configuration.
     */
    bool resetConfig(uint32_t flags = 0);

    /**
     * @brief Set an interrupt at a time in the future using a time_t
     * 
     * @param time The number of second after January 1, 1970 UTC. 
     * 
     * @return true on success or false if an error occurs.
     * 
     * This causes an interrupt on FOUT/nIRQ in the future. It will execute
     * once. This can only be done if the RTC has been programmed with
     * the current time, which it normally gets from the cloud at startup.
     * 
     * There can only be one interrupt set. Setting at one-time or repeating interrupt
     * removes any previously set interrupt time.
     */
    bool interruptAtTime(time_t time);

    /**
     * @brief Set an interrupt at a time in the future using a struct tm *.
     * 
     * @param timeptr pointer to struct tm. This specifies the time (UTC).
     * 
     * @return true on success or false if an error occurs.
     * 
     * This causes an interrupt on FOUT/nIRQ in the future. It will execute
     * once. This can only be done if the RTC has been programmed with
     * the current time, which it normally gets from the cloud at startup.
     * 
     * Only the tm_sec, tm_min, tm_hour, tm_mday, tm_mon, and tm_year are 
     * used. The tm_wday field is ignored. Note that tm_mon is 0-11, not 1-12.
     * 
     * There can only be one interrupt set. Setting at one-time or repeating interrupt
     * removes any previously set interrupt time.
     * 
     * The fields of the timeptr are:
     * - tm_sec	  seconds after the minute	0-61 (usually 0-59)
     * - tm_min	  minutes after the hour 0-59
     * - tm_hour  hours since midnight 0-23
     * - tm_mday  day of the month 1-31
     * - tm_mon	  months since January 0-11 (not 1-12!)
     * - tm_year  years since 1900 (note: 2020 = 120)
     * - tm_wday  days since Sunday	0-6
     */
    bool interruptAtTm(struct tm *timeptr);

    /**
     * @brief Set a repeating interrupt
     * 
     * @param timeptr pointer to struct tm. This specifies the time (UTC).
     * 
     * @param rptValue a constant for which fields of timeptr are used.
     * 
     * @return true on success or false if an error occurs.
     * 
     * This causes an interrupt on FOUT/nIRQ in the future, repeating.
     * This can only be done if the RTC has been programmed with
     * the current time, which it normally gets from the cloud at startup.
     * 
     * - `REG_TIMER_CTRL_RPT_SEC` tm_sec matches (once per minute)
     * - `REG_TIMER_CTRL_RPT_MIN` tm_sec, tm_min match (once per hour)
     * - `REG_TIMER_CTRL_RPT_HOUR` tm_sec, tm_min, tm_hour match (once per day)
     * - `REG_TIMER_CTRL_RPT_WKDY` tm_sec, tm_min, tm_hour, tm_wday match (once per week)
     * - `REG_TIMER_CTRL_RPT_DATE` tm_sec, tm_min, tm_hour, tm_mday match (once per month)
     * - `REG_TIMER_CTRL_RPT_MON` tm_sec, tm_min, tm_hour, tm_mday, tm_mon match (once per year)
     * 
     * Note that tm_mon (month) is 0 - 11, not 1 - 12! 
     * 
     * There can only be one interrupt set. Setting at one-time or repeating interrupt
     * removes any previously set interrupt time.
     * 
     * If you reset the AB1805 configuration using `resetConfig()`, the repeating timer
     * will be cleared, unless you add the `AB1805::RESET_PRESERVE_REPEATING_TIMER` parameter 
     * to preserve it.
     * 
     * The fields of the timeptr are:
     * - tm_sec	  seconds after the minute	0-61 (usually 0-59)
     * - tm_min	  minutes after the hour 0-59
     * - tm_hour  hours since midnight 0-23
     * - tm_mday  day of the month 1-31
     * - tm_mon	  months since January 0-11 (not 1-12!)
     * - tm_year  years since 1900 (note: 2020 = 120)
     * - tm_wday  days since Sunday	0-6
     */
    bool repeatingInterrupt(struct tm *timeptr, uint8_t rptValue);

    /**
     * @brief Clear repeating interrupt set with `repeatingInterrupt()`.

     * @return true on success or false if an error occurs.
     */
    bool clearRepeatingInterrupt();

    /**
     * @brief Interrupt at a time in the future, either in minutes or seconds
     * 
     * @param value Value in seconds or minutes. Must be 0 < value <= 255! 
     * 
     * @param minutes True if minutes, false if seconds
     * 
     * @return true on success or false if an error occurs.

     * The countdown timer works even if the RTC has not been set yet, but is more
     * limited in range (maximum: 255 minutes).
     */
    bool interruptCountdownTimer(int value, bool minutes);

    /**
     * @brief Enters deep power down reset mode, using the EN pin
     * 
     * @param seconds number of seconds to power down. Must be 0 < seconds <= 255.
     * The default is 30 seconds. If time-sensitive, 10 seconds is probably sufficient.
     * 
     * @return true on success or false if an error occurs.
     * 
     * This method powers down the MCU and cellular modem by using a combination
     * of the EN and RST pins. This super-reset is similar to what would happen if
     * you disconnected the battery.
     * 
     * It assumes that EN is connected to /nIRQ2 (PSW) on the AB1805 using an 
     * N-channel MOSFET and RST is connected to /RESET on the AB1805.
     * 
     * After the deep reset finishes, the device will reboot and go back through
     * setup() again. Calling getWakeReset() will return the reason `DEEP_POWER_DOWN`.
     * 
     * This works even if the RTC has not been set yet.
     */
    bool deepPowerDown(int seconds = 30);

    /**
     * @brief Used internally by interruptCountdownTimer and deepPowerDown.
     * 
     * @param value Value in seconds or minutes. Must be 0 < value <= 255! 
     * 
     * @param minutes True if minutes, false if seconds
     * 
     * @return true on success or false if an error occurs.
     * 
     * The countdown timer works even if the RTC has not been set yet, but is more
     * limited in range (maximum: 255 minutes).
     */
    bool setCountdownTimer(int value, bool minutes);

    /**
     * @brief Enable trickle charging mode
     * 
     * @param diodeAndRout Pass 0 to disable the trickle charger or one diode constant and one rout constant below.
     * 
     * @return true on success or false if an error occurs.
     * 
     * Diode settings determine the voltage applied to the supercap or battery in trickle charge mode.
     * This is from VCC, which is generally 3V3, so this will be a voltage of 2.7V or 3.0V. This makes
     * the trickle charger compatible with a wider variety of supercaps.
     * 
     * - `REG_TRICKLE_DIODE_0_6` diode 0.6V drop
     * - `REG_TRICKLE_DIODE_0_3` diode 0.3V drop
     * 
     * The ROUT setting determines the series resistor to the supercap. This determines charging speed.
     * 
     * - `REG_TRICKLE_ROUT_11K` rout 11K
     * - `REG_TRICKLE_ROUT_6K` rout 6K
     * - `REG_TRICKLE_ROUT_3K` rout 3K
     * 
     * The sample design includes an additional 1.5K series resistance using an external resistor, so
     * applying 3K results in an actual series resistance of 4.5K.
     * 
     */
    bool setTrickle(uint8_t diodeAndRout);

    /**
     * @brief Returns true if VBAT input is above minimum operating voltage (1.2V)
     * 
     * This function will check if trickle charging is enabled first. If enabled, it will be turned off,
     * the value checked, then turned back on again.
     */
    bool isVBATAboveMin() { bool bResult = false; checkVBAT(REG_ASTAT_BMIN, bResult); return bResult; };

    /**
     * @brief Returns true if VBAT input is above BREF voltage
     * 
     * The power-up default for BREF is 1.4V (`REG_BREF_CTRL_14_16`), so this provides
     * some additional margin over the minimum voltage (1.2V). BREF can be changed
     * to check for higher voltages (fully charged, for example) but it takes several
     * seconds for the values to settle after making a change, so you don't want to
     * change it too often.
     * 
     * This function will check if trickle charging is enabled first. If enabled, it will be turned off,
     * the value checked, then turned back on again.
     */
    bool isVBATAboveBREF() { bool bResult = false; checkVBAT(REG_ASTAT_BBOD, bResult); return bResult; };

    /**
     * @brief Checks VBAT status
     * 
     * @param mask Either REG_ASTAT_BBOD (compare againt BREF) or REG_ASTAT_BMIN (compare against minimum, 1.2V)
     * 
     * @param isAbove True if VBAT is above the specified voltage, or false if not
     * 
     * This function will check if trickle charging is enabled first. If enabled, it will be turned off,
     * the value checked, then turned back on again.
     */
    bool checkVBAT(uint8_t mask, bool &isAbove);

    /**
     * @brief Set the RTC from the system clock
     * 
     * This is called automatically from AB1805::loop() when the time is updated from the cloud.
     * You normally don't need to call this yourself.
     */
    bool setRtcFromSystem();

    /**
     * @brief Sets the RTC from a time_t
     * 
     * @param time The time (in seconds since January 1, 1970, UNIX epoch), UTC.
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * This is called automatically from AB1805::loop() when the time is updated from the cloud.
     * You normally don't need to call this yourself. You might call this if you are also getting
     * time from an external source like a GPS.
     */
    bool setRtcFromTime(time_t time, bool lock = true);

    /**
     * @brief Sets the RTC from a time_t
     * 
     * @param timeptr A struct tm specifying the time. 
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * The following fields are required:
     * - tm_sec	  seconds after the minute	0-61 (usually 0-59)
     * - tm_min	  minutes after the hour 0-59
     * - tm_hour  hours since midnight 0-23
     * - tm_mday  day of the month 1-31
     * - tm_mon	  months since January 0-11 (not 1-12!)
     * - tm_year  years since 1900 (note: 2020 = 120)
     * - tm_wday  days since Sunday	0-6
     * 
     * Note that you must include tm_wday and 0 = Sunday, 1 = Monday, ...
     * Month of year is 0 - 11, NOT 1 - 12!
     * Year is years since 1900, so 2020 has 120 in the tm_year field!
     * 
     * This is called automatically from AB1805::loop() when the time is updated from the cloud.
     * You normally don't need to call this yourself. You might call this if you are also getting
     * time from an external source like a GPS.
     */
    bool setRtcFromTm(const struct tm *timeptr, bool lock = true);

    
    /**
     * @brief Reads a AB1805 register (single byte)
     * 
     * @param regAddr Register address to read from (0x00 - 0xff)
     * 
     * @param value Filled in with the value from the register
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * @return true on success or false on error
     * 
     * There is also an overload that returns value instead of passing it by reference.
     */
    bool readRegister(uint8_t regAddr, uint8_t &value, bool lock = true);

    /**
     * @brief Reads a AB1805 register (single byte) and returns it
     * 
     * @param regAddr Register address to read from (0x00 - 0xff)
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * @return The value of the register or 0x00 if it could not be read.
     * 
     * There is an overload that takes a uint8_t &value parameter which is generally
     * better because you can tell the difference between a value of 0 and a failure
     * (false is returned).
     */
    uint8_t readRegister(uint8_t regAddr, bool lock = true);

    /**
     * @brief Reads sequential registers
     * 
     * @param regAddr Register address to start reading from (0x00 - 0xff) 
     * 
     * @param array Array of uint8_t values, filled in by this call
     * 
     * @param num Number of registers to read
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * Reads a number of registers at once. This is done when reading the RTC value so
     * it's atomic (counters will not be incremented in the middle of a read). Also used
     * for reading the device RAM.
     * 
     * Do not read past address 0xff. 
     */
    bool readRegisters(uint8_t regAddr, uint8_t *array, size_t num, bool lock = true);


    /**
     * @brief Writes a AB1805 register (single byte)
     * 
     * @param regAddr Register address to write to (0x00 - 0xff)
     * 
     * @param value This value is written to the register
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * @return true on success or false on error
     */
    bool writeRegister(uint8_t regAddr, uint8_t value, bool lock = true);

    /**
     * @brief Writes sequential AB1805 registers
     * 
     * @param regAddr Register address to start writing to (0x00 - 0xff)
     * 
     * @param array Array of uint8_t values to write
     * 
     * @param num Number of registers to write
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * @return true on success or false on error
     * 
     * Do not write past address 0xff. 
     */
    bool writeRegisters(uint8_t regAddr, const uint8_t *array, size_t num, bool lock = true);

    /**
     * @brief Writes a AB1805 register (single byte) with masking of existing value
     * 
     * @param regAddr Register address to read from and write to (0x00 - 0xff)
     * 
     * @param andValue The existing register values is logically ANDed with this value
     * 
     * @param orValue This value is logically ORed with this value before storing
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * @return true on success or false on error
     * 
     * If lock is true then the lock surrounds both the read and write, so the operation is
     * atomic.
     * 
     * If the value is unchanged after the andValue and orValue is applied, the write is skipped.
     * The read is always done.
     */
    bool maskRegister(uint8_t regAddr, uint8_t andValue, uint8_t orValue, bool lock = true);

    /**
     * @brief Returns true if a bit in a register is 0
     * 
     * @param regAddr Register address to read from (0x00 - 0xff)
     * 
     * @param bitMask Mask to check. Note that the bitMask should have a 1 bit where you are checking
     * for a 0 bit! Normally there is only one bit set in bitMask.
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * @return true if the register could be read and the bit is 0, otherwise false.
     */
    bool isBitClear(uint8_t regAddr, uint8_t bitMask, bool lock = true);

    /**
     * @brief Returns true if a bit in a register is 1
     * 
     * @param regAddr Register address to read from (0x00 - 0xff)
     * 
     * @param bitMask Mask to check. The bitMask should have a 1 bit where you are checking
     * for a 1 bit. Normally there is only one bit set in bitMask.
     * 
     * @param lock Lock the I2C bus. Default = true. Pass false if surrounding a block of
     * related calls with a wire.lock() and wire.unlock() so the block cannot be interrupted
     * with other I2C operations.
     * 
     * @return true if the register could be read and the bit is 1, otherwise false.
     */
    bool isBitSet(uint8_t regAddr, uint8_t bitMask, bool lock = true);

    /**
     * @brief Clear a bit in a register
     * 
     * @param regAddr The address of the register to read/write
     * 
     * @param bitMask The bit mask to clear. This has a 1 bit in the bit you want to clear, and will typically
     * only have one bit set, though you can clear multiple bits at the same time with this function.
     * 
     * @param lock Whether to lock the I2C bus, the default is true. You pass false if you are grouping
     * together functions in a single lock, for example doing a read/modify/write cycle.
     * 
     * The bit is cleared only if set. If the bit(s) are already cleared, then only the read is done,
     * and the write is skipped. A read is always done. 
     * 
     * If lock is true, then the lock surround both the read and write so the entire operation is atomic.
     */
    bool clearRegisterBit(uint8_t regAddr, uint8_t bitMask, bool lock = true);

    /**
     * @brief Sets a bit in a register
     * 
     * @param regAddr The address of the register to read/write
     * 
     * @param bitMask The bit mask to set. This has a 1 bit in the bit you want to set, and will typically
     * only have one bit set, though you can set multiple bits at the same time with this function.
     * 
     * @param lock Whether to lock the I2C bus, the default is true. You pass false if you are grouping
     * together functions in a single lock, for example doing a read/modify/write cycle.
     * 
     * The bit is set only if cleared (0). If the bit(s) are already set, then only the read is done,
     * and the write is skipped. A read is always done.
     * 
     * If lock is true, then the lock surround both the read and write so the entire operation is atomic.
     */
    bool setRegisterBit(uint8_t regAddr, uint8_t bitMask, bool lock = true);

	/**
	 * @brief Returns the length of the RTC RAM in bytes (always 256)
	 */
	inline size_t length() { return 256; }

	/**
	 * @brief Erases the RTC RAM to 0x00 values
     * 
     * @param lock Whether to lock the I2C bus, the default is true. You pass false if you are grouping
     * together functions in a single lock, for example doing a read/modify/write cycle.
	 */
	bool eraseRam(bool lock = true);

	/**
	 * @brief Read from RTC RAM using EEPROM-style API
	 *
	 * @param ramAddr The address in the RTC RAM to read from
     * 
	 * @param t The variable to read to. This must be a simple type (bool, int, float, etc.)
     * or struct. It cannot save a c-string (const char *), String, or other class. You
     * typically cannot get any pointers or structs containing pointers.
	 */
    template <typename T> T &get(size_t ramAddr, T &t) {
        readRam(ramAddr, (uint8_t *)&t, sizeof(T));
        return t;
    }

	/**
	 * @brief Write from RTC RAM using EEPROM-style API
	 *
	 * @param ramAddr The address in the RTC RAM to write to
     * 
	 * @param t The variable to write from. t is not modified. This must be a simple type (bool, int, float, etc.)
     * or struct. It cannot save a c-string (const char *), String, or other class. You
     * typically cannot save any pointers or structs containing pointers.
	 */
    template <typename T> const T &put(size_t ramAddr, const T &t) {
        writeRam(ramAddr, (const uint8_t *)&t, sizeof(T));
        return t;
    }

    /**
     * @brief Low-level read call
     *
	 * @param ramAddr The address in the RTC RAM to read from
	 *
	 * @param data The buffer to read into
	 *
	 * @param dataLen The number of bytes to read
	 *
     * @param lock Whether to lock the I2C bus, the default is true. You pass false if you are grouping
     * together functions in a single lock, for example doing a read/modify/write cycle.
     * 
	 * The dataLen can be larger than the maximum I2C read. Multiple reads will be done if necessary.
     * However do not read past the end of RAM (address 255).
     */
	virtual bool readRam(size_t ramAddr, uint8_t *data, size_t dataLen, bool lock = true);

    /**
     * @brief Low-level write call
     *
	 * @param ramAddr The address in the RTC RAM to write to
	 *
	 * @param data The buffer containing the data to write
	 *
	 * @param dataLen The number of bytes to write
	 *
     * @param lock Whether to lock the I2C bus, the default is true. You pass false if you are grouping
     * together functions in a single lock, for example doing a read/modify/write cycle.
     * 
	 * The dataLen can be larger than the maximum I2C write. Multiple writes will be done if necessary.
     * However do not read past the end of RAM (address 255).
     */
	virtual bool writeRam(size_t ramAddr, const uint8_t *data, size_t dataLen, bool lock = true);

    /**
     * @brief Utility function to convert a struct tm * to a readable string
     * 
     * @return String in the format of "yyyy-mm-dd hh:mm:ss". 
     */
    static String tmToString(const struct tm *timeptr);

    /**
     * @brief Convert a struct tm to register values for the AB1805
     * 
     * @param timeptr Pointer to a struct tm with the values to convert from
     * 
     * @param array Array of uint8_t to store the values to. This must be at least 6 bytes
     * if includeYear is false or 7 if true. This points to the seconds field, not the
     * hundredths field!
     * 
     * @param includeYear True if this is the the year should be included (time setting),
     * or false if the year should not be included (alarm setting).
     * 
     * Note: Does not include the hundredths are struct tm doesn't include fractional seconds.
     */
    static void tmToRegisters(const struct tm *timeptr, uint8_t *array, bool includeYear);

    /**
     * @brief Convert register values to a struct tm
     * 
     * @param array Pointer to an array of values from the AB1805. Point to the seconds
     * (not hundredths).
     * 
     * @param timeptr Pointer to a struct tm to hold the converted time
     * 
     * @param includeYear True if this is the the year should be included (time setting),
     * or false if the year should not be included (alarm setting).
     */
    static void registersToTm(const uint8_t *array, struct tm *timeptr, bool includeYear);

    /**
     * @brief Convert a bcd value (0x00-0x99) into an integer (0-99)
     */
    static int bcdToValue(uint8_t bcd);

    /**
     * @brief Convert an integer value (0-99) into a bcd value (0x00 - 0x99)
     */
    static uint8_t valueToBcd(int value);

    static const uint32_t RESET_PRESERVE_REPEATING_TIMER    = 0x00000001;   //!< When resetting registers, leave repeating timer settings intact
    static const uint32_t RESET_DISABLE_XT                  = 0x00000002;   //!< When resetting registers, disable XT oscillator
    
    static const int WATCHDOG_MAX_SECONDS = 124;    //!< Maximum value that can be passed to setWDT().


    static const uint8_t REG_HUNDREDTH              = 0x00;      //!< Hundredths of a second, 2 BCD digits
    static const uint8_t REG_SECOND                 = 0x01;      //!< Seconds, 2 BCD digits, MSB is GP0
    static const uint8_t REG_MINUTE                 = 0x02;      //!< Minutes, 2 BCD digits, MSB is GP1
    static const uint8_t REG_HOUR                   = 0x03;      //!< Hours, GP2, GP3
    static const uint8_t REG_DATE                   = 0x04;      //!< Day of month (1-31), 2 BCD digits, GP4, GP5
    static const uint8_t REG_MONTH                  = 0x05;      //!< Month (1-12), 2 BCD digits, GP6 - GP8
    static const uint8_t REG_YEAR                   = 0x06;      //!< Year (0-99), 2 BCD digits
    static const uint8_t REG_WEEKDAY                = 0x07;      //!< Weekday (0-6), GP9 - GP13
    static const uint8_t REG_HUNDREDTH_ALARM        = 0x08;      //!< Alarm on hundredths of a second (0-99), 2 BCD digits
    static const uint8_t REG_SECOND_ALARM           = 0x09;      //!< Alarm on seconds (0-59), 2 BCD digits, GP14
    static const uint8_t REG_MINUTE_ALARM           = 0x0a;      //!< Alarm on minutes (0-59), 2 BCD digits, GP15
    static const uint8_t REG_HOUR_ALARM             = 0x0b;      //!< Alarm on hour, GP16, GP17
    static const uint8_t REG_DATE_ALARM             = 0x0c;      //!< Alarm on date (1-31), 2 BCD digits, GP18-GP19
    static const uint8_t REG_MONTH_ALARM            = 0x0d;      //!< Alarm on month (1-12). 2 BCD digits, GP20-GP22
    static const uint8_t REG_WEEKDAY_ALARM          = 0x0e;      //!< Alarm on day of week (0-6). GP23-GP27
    static const uint8_t REG_STATUS                 = 0x0f;      //!< Status register
    static const uint8_t   REG_STATUS_CB            = 0x80;      //!< Status register century bit mask
    static const uint8_t   REG_STATUS_BAT           = 0x40;      //!< Status register switched to VBAT bit mask
    static const uint8_t   REG_STATUS_WDT           = 0x20;      //!< Status register watchdog timer enabled and triggered bit mask
    static const uint8_t   REG_STATUS_BL            = 0x10;      //!< Status register battery voltage crossing bit mask
    static const uint8_t   REG_STATUS_TIM           = 0x08;      //!< Status register countdown timer reaches 0 bit mask
    static const uint8_t   REG_STATUS_ALM           = 0x04;      //!< Status register alarm register match bit mask
    static const uint8_t   REG_STATUS_EX2           = 0x02;      //!< Status register WDI interrupt bit mask
    static const uint8_t   REG_STATUS_EX1           = 0x01;      //!< Status register EXTI interrupt bit mask
    static const uint8_t   REG_STATUS_DEFAULT       = 0x00;      //!< Status register, default
    static const uint8_t REG_CTRL_1                 = 0x10;      //!< Control register 1
    static const uint8_t   REG_CTRL_1_STOP          = 0x80;      //!< Control register 1, stop clocking system
    static const uint8_t   REG_CTRL_1_12_24         = 0x40;      //!< Control register 1, 12/24 hour mode select (0 = 24 hour)
    static const uint8_t   REG_CTRL_1_OUTB          = 0x20;      //!< Control register 1, value for nIRQ2
    static const uint8_t   REG_CTRL_1_OUT           = 0x10;      //!< Control register 1, value for FOUT/nIRQ
    static const uint8_t   REG_CTRL_1_RSP           = 0x08;      //!< Control register 1, Reset polarity
    static const uint8_t   REG_CTRL_1_ARST          = 0x04;      //!< Control register 1, Auto reset enable
    static const uint8_t   REG_CTRL_1_PWR2          = 0x02;      //!< Control register 1, PWW/nIRQ pull-down enable
    static const uint8_t   REG_CTRL_1_WRTC          = 0x01;      //!< Control register 1, write RTC mode
    static const uint8_t   REG_CTRL_1_DEFAULT       = 0x13;      //!< Control register 1, 0b00010011 (OUT | RSO | PWR2 | WRTC)
    static const uint8_t REG_CTRL_2                 = 0x11;      //!< Control register 2
    static const uint8_t   REG_CTRL_2_RS1E          = 0x20;      //!< Control register 2, nIRQ2 output mode
    static const uint8_t   REG_CTRL_2_OUT2S_MASK    = 0x1c;      //!< Control register 2, nIRQ2 output mode
    static const uint8_t   REG_CTRL_2_OUT2S_nIRQ    = 0x00;      //!< Control register 2, nIRQ2 output mode, nIRQ or OUTB
    static const uint8_t   REG_CTRL_2_OUT2S_SQW     = 0x04;      //!< Control register 2, nIRQ2 output mode, SQW or OUTB
    static const uint8_t   REG_CTRL_2_OUT2S_nAIRQ   = 0x0c;      //!< Control register 2, nIRQ2 output mode, nAIRQ or OUTB
    static const uint8_t   REG_CTRL_2_OUT2S_TIRQ    = 0x10;      //!< Control register 2, nIRQ2 output mode, TIRQ or OUTB
    static const uint8_t   REG_CTRL_2_OUT2S_nTIRQ   = 0x14;      //!< Control register 2, nIRQ2 output mode, nTIRQ or OUTB
    static const uint8_t   REG_CTRL_2_OUT2S_SLEEP   = 0x18;      //!< Control register 2, nIRQ2 output mode, sleep mode
    static const uint8_t   REG_CTRL_2_OUT2S_OUTB    = 0x1c;      //!< Control register 2, nIRQ2 output mode, OUTB
    static const uint8_t   REG_CTRL_2_OUT1S_MASK    = 0x03;      //!< Control register 2, FOUT/nIRQ output mode
    static const uint8_t   REG_CTRL_2_OUT1S_nIRQ    = 0x00;      //!< Control register 2, FOUT/nIRQ output mode, nIRQ, or OUT
    static const uint8_t   REG_CTRL_2_OUT1S_SQW     = 0x01;      //!< Control register 2, FOUT/nIRQ output mode, SQW or OUT
    static const uint8_t   REG_CTRL_2_OUT1S_SQW_nIRQ= 0x02;      //!< Control register 2, FOUT/nIRQ output mode, SQW, nIRQ, or OUT
    static const uint8_t   REG_CTRL_2_OUT1S_nAIRQ   = 0x03;      //!< Control register 2, FOUT/nIRQ output mode, nIRQ or OUT
    static const uint8_t   REG_CTRL_2_DEFAULT       = 0x3c;      //!< Control register 2, 0b00111100 (OUT2S = OUTB)
    static const uint8_t REG_INT_MASK               = 0x12;      //!< Interrupt mask
    static const uint8_t   REG_INT_MASK_CEB         = 0x80;      //!< Interrupt mask, century enable
    static const uint8_t   REG_INT_MASK_IM          = 0x60;      //!< Interrupt mask, interrupt mode bits (2 bits)
    static const uint8_t   REG_INT_MASK_BLIE        = 0x10;      //!< Interrupt mask, battery low interrupt enable
    static const uint8_t   REG_INT_MASK_TIE         = 0x08;      //!< Interrupt mask, timer interrupt enable
    static const uint8_t   REG_INT_MASK_AIE         = 0x04;      //!< Interrupt mask, alarm interrupt enable
    static const uint8_t   REG_INT_MASK_EX2E        = 0x02;      //!< Interrupt mask, XT2 interrupt enable
    static const uint8_t   REG_INT_MASK_EX1E        = 0x01;      //!< Interrupt mask, XT1 interrupt enable
    static const uint8_t   REG_INT_MASK_DEFAULT     = 0xe0;      //!< Interrupt mask, default 0b11100000 (CEB | IM=1/4 seconds)
    static const uint8_t REG_SQW                    = 0x13;      //!< Square wave output control
    static const uint8_t   REG_SQW_SQWE             = 0x80;      //!< Square wave output control, enable
    static const uint8_t   REG_SQW_DEFAULT          = 0x26;      //!< Square wave output control, default 0b00100110
    static const uint8_t REG_CAL_XT                 = 0x14;      //!< Calibration for the XT oscillator
    static const uint8_t REG_CAL_RC_HIGH            = 0x15;      //!< Calibration for the RC oscillator, upper 8 bits
    static const uint8_t REG_CAL_RC_LOW             = 0x16;      //!< Calibration for the RC oscillator, lower 8 bits
    static const uint8_t REG_SLEEP_CTRL             = 0x17;      //!< Power control system sleep function
    static const uint8_t   REG_SLEEP_CTRL_SLP       = 0x80;      //!< Sleep control, enter sleep mode
    static const uint8_t   REG_SLEEP_CTRL_SLRES     = 0x40;      //!< Sleep control, nRST low on sleep
    static const uint8_t   REG_SLEEP_CTRL_EX2P      = 0x20;      //!< Sleep control, XT2 on rising WDI
    static const uint8_t   REG_SLEEP_CTRL_EX1P      = 0x10;      //!< Sleep control, XT1 on rising EXTI
    static const uint8_t   REG_SLEEP_CTRL_SLST      = 0x08;      //!< Sleep control, set when sleep has occurred
    static const uint8_t   REG_SLEEP_CTRL_SLTO_MASK = 0x07;      //!< Sleep control, number of 7.8ms periods before sleep
    static const uint8_t   REG_SLEEP_CTRL_DEFAULT   = 0x00;      //!< Sleep control default (0b00000000)
    static const uint8_t REG_TIMER_CTRL             = 0x18;      //!< Countdown timer control 
    static const uint8_t   REG_TIMER_CTRL_TE        = 0x80;      //!< Countdown timer control, timer enable 
    static const uint8_t   REG_TIMER_CTRL_TM        = 0x40;      //!< Countdown timer control, timer interrupt mode
    static const uint8_t   REG_TIMER_CTRL_TRPT      = 0x20;      //!< Countdown timer control, timer repeat function
    static const uint8_t   REG_TIMER_CTRL_RPT_MASK  = 0x1c;      //!< Countdown timer control, repeat function
    static const uint8_t   REG_TIMER_CTRL_RPT_HUN   = 0x1c;      //!< Countdown timer control, repeat hundredths match (7)
    static const uint8_t   REG_TIMER_CTRL_RPT_SEC   = 0x18;      //!< Countdown timer control, repeat hundredths, seconds match (once per minute) (6)
    static const uint8_t   REG_TIMER_CTRL_RPT_MIN   = 0x14;      //!< Countdown timer control, repeat hundredths, seconds, minutes match (once per hour) (5)
    static const uint8_t   REG_TIMER_CTRL_RPT_HOUR  = 0x10;      //!< Countdown timer control, repeat hundredths, seconds, minutes, hours match (once per day) (4)
    static const uint8_t   REG_TIMER_CTRL_RPT_WKDY  = 0x0c;      //!< Countdown timer control, repeat hundredths, seconds, minutes, hours, weekday match (once per week) (3)
    static const uint8_t   REG_TIMER_CTRL_RPT_DATE  = 0x08;      //!< Countdown timer control, repeat hundredths, seconds, minutes, hours, date match (once per month) (2)
    static const uint8_t   REG_TIMER_CTRL_RPT_MON   = 0x04;      //!< Countdown timer control, repeat hundredths, seconds, minutes, hours, date, month match (once per year) (1)
    static const uint8_t   REG_TIMER_CTRL_RPT_DIS   = 0x00;      //!< Countdown timer control, alarm disabled (0)
    static const uint8_t   REG_TIMER_CTRL_TFS_MASK  = 0x03;      //!< Countdown timer control, clock frequency
    static const uint8_t   REG_TIMER_CTRL_TFS_FAST  = 0x00;      //!< Countdown timer control, clock frequency 4.096 kHz or 128 Hz
    static const uint8_t   REG_TIMER_CTRL_TFS_64    = 0x01;      //!< Countdown timer control, clock frequency 64 Hz
    static const uint8_t   REG_TIMER_CTRL_TFS_1     = 0x02;      //!< Countdown timer control, clock frequency 1 Hz
    static const uint8_t   REG_TIMER_CTRL_TFS_1_60  = 0x03;      //!< Countdown timer control, clock frequency 1/60 Hz (1 minute)
    static const uint8_t   REG_TIMER_CTRL_DEFAULT   = 0x23;      //!< Countdown timer control, 0b00100011 (TFPT + TFS = 1/60 Hz0)
    static const uint8_t REG_TIMER                  = 0x19;      //!< Countdown timer current value register
    static const uint8_t   REG_TIMER_DEFAULT        = 0x00;      //!< Countdown timer current value register default value (0x00)
    static const uint8_t REG_TIMER_INITIAL          = 0x1a;      //!< Countdown timer inital (reload) value register
    static const uint8_t   REG_TIMER_INITIAL_DEFAULT= 0x00;      //!< Countdown timer inital value register default value 
    static const uint8_t REG_WDT                    = 0x1b;      //!< Watchdog timer control register
    static const uint8_t   REG_WDT_RESET            = 0x80;      //!< Watchdog timer control, enable reset (1) or WIRQ (0)
    static const uint8_t   REG_WDT_WRB_16_HZ        = 0x00;      //!< Watchdog timer control, WRB watchdog clock = 16 Hz
    static const uint8_t   REG_WDT_WRB_4_HZ         = 0x01;      //!< Watchdog timer control, WRB watchdog clock = 4 Hz
    static const uint8_t   REG_WDT_WRB_1_HZ         = 0x02;      //!< Watchdog timer control, WRB watchdog clock = 1 Hz
    static const uint8_t   REG_WDT_WRB_1_4_HZ       = 0x03;      //!< Watchdog timer control, WRB watchdog clock = 1/4 Hz
    static const uint8_t   REG_WDT_DEFAULT          = 0x00;      //!< Watchdog timer control, default value
    static const uint8_t REG_OSC_CTRL               = 0x1c;      //!< Oscillator control register
    static const uint8_t   REG_OSC_CTRL_OSEL        = 0x80;      //!< Oscillator control, clock select 32.768 kHz (0) or 128 Hz (1)
    static const uint8_t   REG_OSC_CTRL_ACAL        = 0x60;      //!< Oscillator control, auto-calibration
    static const uint8_t   REG_OSC_CTRL_AOS         = 0x10;      //!< Oscillator control, automatic switch to RC oscillator on battery
    static const uint8_t   REG_OSC_CTRL_FOS         = 0x08;      //!< Oscillator control, automatic switch to RC oscillator on failure
    static const uint8_t   REG_OSC_CTRL_PWGT        = 0x04;      //!< Oscillator control, IO interface disable
    static const uint8_t   REG_OSC_CTRL_OFIE        = 0x02;      //!< Oscillator control, oscillator fail interrupt enable
    static const uint8_t   REG_OSC_CTRL_ACIE        = 0x01;      //!< Oscillator control, auto-calibration fail interrupt enable
    static const uint8_t   REG_OSC_CTRL_DEFAULT     = 0x00;      //!< Oscillator control, default value
    static const uint8_t REG_OSC_STATUS             = 0x1d;      //!< Oscillator status register
    static const uint8_t   REG_OSC_STATUS_XTCAL     = 0x0c;      //!< Oscillator status register, extended crystal calibration
    static const uint8_t   REG_OSC_STATUS_LKO2      = 0x04;      //!< Oscillator status register, lock OUT2
    static const uint8_t   REG_OSC_STATUS_OMODE     = 0x01;      //!< Oscillator status register, oscillator mode (read-only)
    static const uint8_t   REG_OSC_STATUS_OF        = 0x02;      //!< Oscillator status register, oscillator failure
    static const uint8_t   REG_OSC_STATUS_ACF       = 0x01;      //!< Oscillator status register, auto-calibration failure
    static const uint8_t REG_CONFIG_KEY             = 0x1f;      //!< Register to set to modify certain other keys
    static const uint8_t   REG_CONFIG_KEY_OSC_CTRL  = 0xa1;      //!< Configuration key, enable setting REG_OSC_CTRL
    static const uint8_t   REG_CONFIG_KEY_SW_RESET  = 0x3c;      //!< Configuration key, software reset
    static const uint8_t   REG_CONFIG_KEY_OTHER     = 0x9d;      //!< Configuration key, REG_TRICKLE, REG_BREF_CTRL, REG_AFCTRL, REG_BATMODE_IO, REG_OCTRL
    static const uint8_t REG_TRICKLE                = 0x20;      //!< Trickle charger control register
    static const uint8_t   REG_TRICKLE_DEFAULT      = 0x00;      //!< Trickle charger control register, default value
    static const uint8_t   REG_TRICKLE_TCS_MASK     = 0xf0;      //!< Trickle charger control register, enable mask
    static const uint8_t   REG_TRICKLE_TCS_ENABLE   = 0xa0;      //!< Trickle charger control register, enable value (0b10100000)
    static const uint8_t   REG_TRICKLE_DIODE_MASK   = 0x0c;      //!< Trickle charger control register, diode mask
    static const uint8_t   REG_TRICKLE_DIODE_0_6    = 0x08;      //!< Trickle charger control register, diode 0.6V drop
    static const uint8_t   REG_TRICKLE_DIODE_0_3    = 0x04;      //!< Trickle charger control register, diode 0.3V drop
    static const uint8_t   REG_TRICKLE_ROUT_MASK    = 0x03;      //!< Trickle charger control register, rout mask
    static const uint8_t   REG_TRICKLE_ROUT_11K     = 0x03;      //!< Trickle charger control register, rout 11K
    static const uint8_t   REG_TRICKLE_ROUT_6K      = 0x02;      //!< Trickle charger control register, rout 6K
    static const uint8_t   REG_TRICKLE_ROUT_3K      = 0x01;      //!< Trickle charger control register, rout 3K
    static const uint8_t   REG_TRICKLE_ROUT_DISABLE = 0x00;      //!< Trickle charger control register, rout disable
    static const uint8_t REG_BREF_CTRL              = 0x21;      //!< Wakeup control system reference voltages
    static const uint8_t   REG_BREF_CTRL_DEFAULT    = 0xf0;      //!< Wakeup control system default 0b11110000
    static const uint8_t   REG_BREF_CTRL_25_30      = 0x70;      //!< Wakeup control falling 2.5V rising 3.0V
    static const uint8_t   REG_BREF_CTRL_21_25      = 0xb0;      //!< Wakeup control falling 2.1V rising 2.5V
    static const uint8_t   REG_BREF_CTRL_18_22      = 0xd0;      //!< Wakeup control falling 1.8V rising 2.2V
    static const uint8_t   REG_BREF_CTRL_14_16      = 0xf0;      //!< Wakeup control falling 1.4V rising 1.6V, default value
    static const uint8_t REG_AFCTRL                 = 0x26;      //!< Auto-calibration filter capacitor enable register
    static const uint8_t   REG_AFCTRL_ENABLE        = 0xa0;      //!< Auto-calibration filter capacitor enable
    static const uint8_t   REG_AFCTRL_DISABLE       = 0x00;      //!< Auto-calibration filter capacitor disable
    static const uint8_t   REG_AFCTRL_DEFAULT       = 0x00;      //!< Auto-calibration filter, default
    static const uint8_t REG_BATMODE_IO             = 0x27;      //!< Brownout control for IO interface
    static const uint8_t   REG_BATMODE_IO_DEFAULT   = 0x80;      //!< Brownout control for IO interface, default value
    static const uint8_t   REG_BATMODE_IO_IOBM      = 0x80;      //!< Brownout control for IO interface, enable IO when on VBAT
    static const uint8_t REG_ID0                    = 0x28;      //!< Part number, upper (read-only)
    static const uint8_t   REG_ID0_AB08XX           = 0x18;      //!< Part number, upper, AB08xx
    static const uint8_t   REG_ID0_AB18XX           = 0x18;      //!< Part number, upper, AB18xx
    static const uint8_t REG_ID1                    = 0x29;      //!< Part number, lower (read-only)
    static const uint8_t   REG_ID1_ABXX05           = 0x05;      //!< Part number, lower, AB1805 or AB0805 (I2C)
    static const uint8_t   REG_ID1_ABXX15           = 0x05;      //!< Part number, lower, AB1815 or AB0815 (SPI)
    static const uint8_t REG_ID2                    = 0x2a;      //!< Part revision (read-only)
    static const uint8_t REG_ID3                    = 0x2b;      //!< Lot number, lower (read-only)
    static const uint8_t REG_ID4                    = 0x2c;      //!< Manufacturing unique ID upper (read-only)
    static const uint8_t REG_ID5                    = 0x2d;      //!< Manufacturing unique ID lower (read-only)
    static const uint8_t REG_ID6                    = 0x2e;      //!< Lot and wafer information (read-only)
    static const uint8_t REG_ASTAT                  = 0x2f;      //!< Analog status register (read-only)
    static const uint8_t   REG_ASTAT_BBOD           = 0x80;      //!< Analog status register. VBAT is above BREF (read-only)
    static const uint8_t   REG_ASTAT_BMIN           = 0x40;      //!< Analog status register. VBAT is above minimum operating voltage 1.2V (read-only)
    static const uint8_t   REG_ASTAT_VINIT          = 0x02;      //!< Analog status register. VCC is about minimum 1.6V (read-only)
    static const uint8_t REG_OCTRL                  = 0x30;      //!< Output control register at power-down
    static const uint8_t   REG_OCTRL_WDBM           = 0x80;      //!< Output control register, WDI enabled when powered from VBAT
    static const uint8_t   REG_OCTRL_EXBM           = 0x40;      //!< Output control register, EXTI enabled when powered from VBAT
    static const uint8_t   REG_OCTRL_WDDS           = 0x20;      //!< Output control register, WDI disabled in sleep 
    static const uint8_t   REG_OCTRL_EXDS           = 0x10;      //!< Output control register, EXTI disabled in sleep
    static const uint8_t   REG_OCTRL_RSEN           = 0x08;      //!< Output control register, nRST output enabled in sleep
    static const uint8_t   REG_OCTRL_O4EN           = 0x04;      //!< Output control register, CLKOUT/nIRQ3 enabled in sleep
    static const uint8_t   REG_OCTRL_O3EN           = 0x02;      //!< Output control register, nTIRQ enabled in sleep
    static const uint8_t   REG_OCTRL_O1EN           = 0x01;      //!< Output control register, FOUT/nIRQ enabled in sleep 
    static const uint8_t   REG_OCTRL_DEFAULT        = 0x00;      //!< Output control register, default 
    static const uint8_t REG_EXT_ADDR               = 0x3f;      //!< Extension RAM address
    static const uint8_t   REG_EXT_ADDR_O4MB        = 0x80;      //!< Extension RAM address, CLKOUT/nIRQ3 enabled when powered from VBAT
    static const uint8_t   REG_EXT_ADDR_BPOL        = 0x40;      //!< Extension RAM address, BL polarity
    static const uint8_t   REG_EXT_ADDR_WDIN        = 0x20;      //!< Extension RAM address, level of WDI pin (read-only)
    static const uint8_t   REG_EXT_ADDR_EXIN        = 0x10;      //!< Extension RAM address, level of EXTI pin (read-only)
    static const uint8_t   REG_EXT_ADDR_XADA        = 0x04;      //!< Extension RAM address, Upper bit of alternate RAM address
    static const uint8_t   REG_EXT_ADDR_XADS        = 0x03;      //!< Extension RAM address, Upper 2 bits of standard RAM address
    static const uint8_t REG_RAM                    = 0x40;      //!< Standard RAM
    static const uint8_t REG_ALT_RAM                = 0x80;      //!< Alternate RAM address


protected:
    /**
     * @brief Internal function used to handle system events
     * 
     * We currently only handle the reset event to disable the WDT before reset so it
     * won't trigger during a OTA firmware update.
     */
    void systemEvent(system_event_t event, int param);

    /**
     * @brief Static function passed to System.on
     * 
     * AB1805 is a singleton so AB1805::instance is used to find the instance pointer.
     */
    static void systemEventStatic(system_event_t event, int param);

    /**
     * @brief Which I2C (TwoWire) interface to use. Usually Wire, is Wire1 on Tracker SoM
     */
    TwoWire &wire = Wire; 

    /**
     * @brief I2C address, always 0x69 as that is the address hardwired in the AB1805
     */
    uint8_t i2cAddr = 0x69;

    /**
     * @brief Which GPIO is connected to FOUT/nIRQ
     * 
     * Set to PIN_INVALID if not connected. 
     * 
     * This is used for interrupts, and also to detect if the AB1805 is alive, during
     * detectChip(). If not connected, then only I2C is used to detect the chip.
     */
    pin_t foutPin = PIN_INVALID;

    /**
     * @brief Watchdog period in seconds (1 <= watchdogSecs <= 124) or 0 for disabled.
     * 
     * This is used so setWDT(-1) can restore the previous value.
     */
    int watchdogSecs = 0;

    /**
     * @brief The last millis() value where we called setWDT(-1)
     */
    unsigned long lastWatchdogMillis = 0;

    /**
     * @brief How often to call updateWDT(-1) in milliseconds
     */
    unsigned long watchdogUpdatePeriod = 0;

    /**
     * @brief True if we've set the RTC from the cloud time
     */
    bool timeSet = false;

    /**
     * @brief The reason for wake. Set during setup()
     */
    WakeReason wakeReason = WakeReason::UNKNOWN;

    /**
     * @brief Singleton for AB1805. Set in constructor
     */
    static AB1805 *instance;

};

#endif /* __AB1805RK_H */

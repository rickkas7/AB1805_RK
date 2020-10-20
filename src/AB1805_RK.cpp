#include "AB1805_RK.h"



static Logger _log("app.ab1805");

// Define SET_D8_LOW on FeatherAB1805v1 boards as the pull-up is wired to 3V3R instead
// of 3V3 which can cause current leakages when powering down using EN.
#define SET_D8_LOW

AB1805 *AB1805::instance = 0;


AB1805::AB1805(TwoWire &wire, uint8_t i2cAddr) : wire(wire), i2cAddr(i2cAddr) {
    instance = this;
}

AB1805::~AB1805() {

}


void AB1805::setup(bool callBegin) {
    if (callBegin) {
        wire.begin();
    }
    
    if (detectChip()) {
        updateWakeReason();

        // If we've set the time in the RTC, then the WRTC bit will be 0.
        // On power-up from cold, it's 1
        if (isBitClear(REG_CTRL_1, REG_CTRL_1_WRTC) && !Time.isValid()) {
            // Set system clock from RTC
            time_t time;

            getRtcAsTime(time);
            Time.setTime(time);

            _log.info("set system clock from RTC %s", Time.format(time, TIME_FORMAT_DEFAULT).c_str());
        }
    }
    else {
        _log.error("failed to detect AB1805");
    }

    System.on(reset, systemEventStatic);
}

void AB1805::loop() {
    if (!timeSet && Time.isValid() && Particle.timeSyncedLast() != 0) {
        timeSet = true;

        time_t time = Time.now();
        setRtcFromTime(time);

        time = 0;
        getRtcAsTime(time);
        _log.info("set RTC from cloud %s", Time.format(time, TIME_FORMAT_DEFAULT).c_str());

    }

    if (watchdogUpdatePeriod) {
        if (millis() - lastWatchdogMillis >= watchdogUpdatePeriod) {
            lastWatchdogMillis = millis();

            // Reset the watchdog timer
            setWDT();
        }
    }
}


bool AB1805::detectChip() {
    bool bResult, finalResult = false;
    uint8_t value;

    // FOUT/nIRQ (D8) will go HIGH when the chip is ready to respond
    if (foutPin != PIN_INVALID) {
        unsigned long start = 0;
        bool ready = false;
        while(millis() - start < 1000) {
            if (digitalRead(foutPin) == HIGH) {
                ready = true;
                break;
            }
            if (!ready) {
                _log.info("FOUT did not go HIGH");

                // May just want to return false here
            }
        }
    }

    bResult = readRegister(REG_ID0, value);
    if (bResult && value == REG_ID0_AB18XX) {
        bResult = readRegister(REG_ID1, value);
        if (bResult && value == REG_ID1_ABXX05) {
            // Is AB1805 (advanced features, I2C)
            finalResult = true;
        }
    }
    if (!finalResult) {
        _log.info("not detected");
    }

    return finalResult;
}


bool AB1805::usingRCOscillator() {
    uint8_t value;

    bool bResult = readRegister(REG_OSC_STATUS, value);
    if (bResult) {
        return (value & REG_OSC_STATUS_OMODE) != 0;
    }
    else {
        return false;
    }
}

bool AB1805::resetConfig(uint32_t flags) {
    _log.trace("resetConfig(0x%08lx)", flags);

    wire.lock();

    // Reset configuration registers to default values
    writeRegister(REG_STATUS, REG_STATUS_DEFAULT, false);
    writeRegister(REG_CTRL_1, REG_CTRL_1_DEFAULT, false);
    writeRegister(REG_CTRL_2, REG_CTRL_2_DEFAULT, false);
    writeRegister(REG_INT_MASK, REG_INT_MASK_DEFAULT, false);
    writeRegister(REG_SQW, REG_SQW_DEFAULT, false);
    writeRegister(REG_SLEEP_CTRL, REG_SLEEP_CTRL_DEFAULT, false);

    if ((flags & RESET_PRESERVE_REPEATING_TIMER) != 0) {
        maskRegister(REG_TIMER_CTRL, ~REG_TIMER_CTRL_RPT_MASK, REG_TIMER_CTRL_DEFAULT & ~REG_TIMER_CTRL_RPT_MASK, false);
    }  
    else {
        writeRegister(REG_TIMER_CTRL, REG_TIMER_CTRL_DEFAULT, false);
    }

    writeRegister(REG_TIMER, REG_TIMER_DEFAULT, false);
    writeRegister(REG_TIMER_INITIAL, REG_TIMER_INITIAL_DEFAULT, false);
    writeRegister(REG_WDT, REG_WDT_DEFAULT, false);

    uint8_t oscCtrl = REG_OSC_CTRL_DEFAULT;
    if ((flags & RESET_DISABLE_XT) != 0) {
        // If disabling XT oscillator, set OSEL to 1 (RC oscillator)
        // Also enable FOS so if the XT oscillator fails, it will switch to RC (just in case)
        // and ACAL to 0 (however REG_OSC_CTRL_DEFAULT already sets ACAL to 0)
        oscCtrl |= REG_OSC_CTRL_OSEL | REG_OSC_CTRL_FOS;
    }
    writeRegister(REG_OSC_CTRL, oscCtrl, false);
    writeRegister(REG_TRICKLE, REG_TRICKLE_DEFAULT, false);
    writeRegister(REG_BREF_CTRL, REG_BREF_CTRL_DEFAULT, false);
    writeRegister(REG_AFCTRL, REG_AFCTRL_DEFAULT, false);
    writeRegister(REG_BATMODE_IO, REG_BATMODE_IO_DEFAULT, false);
    writeRegister(REG_OCTRL, REG_OCTRL_DEFAULT, false);

    wire.unlock();

    return true;
}


bool AB1805::updateWakeReason() {
    static const char *errorMsg = "failure in updateWakeReason %d";

    uint8_t status;
    bool bResult = readRegister(REG_STATUS, status);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    const char *reason = 0;

    if ((status & REG_STATUS_WDT) != 0) {
        reason = "WATCHDOG";
        wakeReason = WakeReason::WATCHDOG;
        clearRegisterBit(REG_STATUS, REG_STATUS_WDT);
    }
    else if (isBitSet(REG_SLEEP_CTRL, REG_SLEEP_CTRL_SLST)) {
        reason = "DEEP_POWER_DOWN";
        wakeReason = WakeReason::DEEP_POWER_DOWN;
    }    
    else if ((status & REG_STATUS_TIM) != 0) {
        reason = "COUNTDOWN_TIMER";
        wakeReason = WakeReason::COUNTDOWN_TIMER;
        clearRegisterBit(REG_STATUS, REG_STATUS_TIM);            
    }
    else if ((status & REG_STATUS_ALM) != 0) {
        reason = "ALARM";
        wakeReason = WakeReason::ALARM;
        clearRegisterBit(REG_STATUS, REG_STATUS_ALM);            
    }

    if (reason) {
        _log.info("wake reason = %s", reason);
    }

    return true;
}

bool AB1805::setWDT(int seconds) {
    bool bResult = false;
    _log.info("setWDT %d", seconds);

    if (seconds < 0) {
        seconds = watchdogSecs;
    }

    if (seconds == 0) {
        // Disable WDT
        bResult = writeRegister(REG_WDT, 0x00);

        _log.trace("watchdog cleared bResult=%d", bResult);

        watchdogSecs = 0;
        watchdogUpdatePeriod = 0;
    } 
    else {
        // Use 1/4 Hz clock
        int fourSecs = seconds / 4;
        if (fourSecs < 1) {
            fourSecs = 1;
        }
        if (fourSecs > 31) {
            fourSecs = 31;
        }
        bResult = writeRegister(REG_WDT, REG_WDT_RESET | (fourSecs << 2) | REG_WDT_WRB_1_4_HZ);

        _log.trace("watchdog set fourSecs=%d bResult=%d", fourSecs, bResult);

        watchdogSecs = seconds;

        // Update watchdog half way through period
        watchdogUpdatePeriod = (fourSecs * 2000);
    }

    return bResult;      
}

bool AB1805::setRtcFromSystem() {
    if (Time.isValid()) {
        return setRtcFromTime(Time.now());
    }
    else {
        return false;
    }
}

bool AB1805::setRtcFromTime(time_t time, bool lock) {
    struct tm *tm = gmtime(&time);
    return setRtcFromTm(tm, lock);
}

bool AB1805::setRtcFromTm(const struct tm *timeptr, bool lock) {
    static const char *errorMsg = "failure in setRtcFromTm %d";
    uint8_t array[8];

    _log.info("setRtcAsTm %s", tmToString(timeptr).c_str());

    if (lock) {
        wire.lock();
    }

    array[0] = 0x00; // hundredths
    tmToRegisters(timeptr, &array[1], true);

    // Can only write RTC registers when WRTC is 1
    bool bResult = setRegisterBit(REG_CTRL_1, REG_CTRL_1_WRTC);
    if (bResult) {
        bResult = writeRegisters(REG_HUNDREDTH, array, sizeof(array), false);
        if (bResult) {
            // Clear the REG_CTRL_1_WRTC after setting the RTC.
            // Aside from being a good thing to do, that's how we know we've set it.
            clearRegisterBit(REG_CTRL_1, REG_CTRL_1_WRTC);
        }
        else {
            _log.error(errorMsg, __LINE__);
        }
    }
    else {
        _log.error(errorMsg, __LINE__);
    }

    if (lock) {
        wire.unlock();
    }

    return bResult;
}

bool AB1805::getRtcAsTime(time_t &time) {
    struct tm tmstruct;

    bool bResult = getRtcAsTm(&tmstruct);
    if (bResult) {
        // Technically mktime is local time, not UTC. However, the standard library
        // is set at +0000 so the local time happens to also be UTC. This is the
        // case even if Time.zone() is called, which only affects the Wiring
        // API and does not affect the standard time library.
        time = mktime(&tmstruct);
    }

    return bResult;   
}

bool AB1805::getRtcAsTm(struct tm *timeptr) {
    uint8_t array[8];
    bool bResult = false;

    // If we've set the time in the RTC, then the WTC bit will be 0.
    // On power-up from cold, it's 1
    if (isBitClear(REG_CTRL_1, REG_CTRL_1_WRTC)) {
        bResult = readRegisters(REG_HUNDREDTH, array, sizeof(array));
        if (bResult) {
            registersToTm(&array[1], timeptr, true);

            _log.info("getRtcAsTm %s", tmToString(timeptr).c_str());
        }
    }
    if (!bResult) {
        memset(timeptr, 0, sizeof(*timeptr));
    }

    return bResult;
}


#if 0
bool AB1805::testEN() {
    _log.info("testEN()");

    // Test function to drive EN low (device power off) by setting PSW/nIRQ2 high
    // Note: Only way to recover from this is to depower the AB1805!

    // Control PWR/nIRQ2 using OUTB
    maskRegister(REG_CTRL_2, ~REG_CTRL_2_OUT2S_MASK, REG_CTRL_2_OUT2S_OUTB);

    // In order to change OUTB, you need to clear LKO2. This is done by setting
    // Oscillator Status to 0. This will also clear the OF (oscillator failure)
    // but, but this is OK.
    writeRegister(REG_OSC_STATUS, 0x00);

    // Set PWR2 to 0 (default 1), this allows PWR/nIRQ2 to be normal open-drain 
    // Set OUTB to 1 (default 0), this should set EN low via the N-channel MOSFET
    maskRegister(REG_CTRL_1, ~REG_CTRL_1_PWR2, REG_CTRL_1_OUTB);

    return true;
}  
#endif

bool AB1805::interruptAtTime(time_t time) {
    struct tm *tm = gmtime(&time);
    return interruptAtTm(tm);
}

bool AB1805::interruptAtTm(struct tm *timeptr) {
    return repeatingInterrupt(timeptr, REG_TIMER_CTRL_RPT_DATE);
}

bool AB1805::repeatingInterrupt(struct tm *timeptr, uint8_t rptValue) {
    static const char *errorMsg = "failure in repeatingInterrupt %d";
    bool bResult;

    // Disable watchdog
    bResult = setWDT(0);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Clear any existing alarm (ALM) interrupt in status register
    bResult = clearRegisterBit(REG_STATUS, REG_STATUS_ALM);            
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Set alarm registers
    uint8_t array[7];

    array[0] = 0x00; // hundredths
    tmToRegisters(timeptr, &array[1], false);

    bResult = writeRegisters(REG_HUNDREDTH_ALARM, array, sizeof(array));
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

#if 1
    {
        // TESTING
        uint8_t array2[7];

        bResult = readRegisters(REG_HUNDREDTH_ALARM, array2, sizeof(array2));
        _log.info("alarm (first) current (second)");
        _log.dump(array2, sizeof(array2));
        _log.print("\n");

        uint8_t array3[8];
        bResult = readRegisters(REG_HUNDREDTH, array3, sizeof(array3));
        _log.dump(array3, sizeof(array3));
        _log.print("\n");
    }
#endif

    // Set FOUT/nIRQ control in OUT1S in Control2 for 
    // "nAIRQ if AIE is set, else OUT"
    bResult = maskRegister(REG_CTRL_2, ~REG_CTRL_2_OUT1S_MASK, REG_CTRL_2_OUT1S_nAIRQ);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Enable alarm interrupt (AIE) in interrupt mask register
    bResult = setRegisterBit(REG_INT_MASK, REG_INT_MASK_AIE);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Enable alarm
    bResult = maskRegister(REG_TIMER_CTRL, ~REG_TIMER_CTRL_RPT_MASK, rptValue & REG_TIMER_CTRL_RPT_MASK);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }
    
    return true;
}

bool AB1805::clearRepeatingInterrupt() {
    static const char *errorMsg = "failure in clearRepeatingInterrupt %d";
    bool bResult;

    // Set FOUT/nIRQ control in Control2 to the default value
    bResult = maskRegister(REG_CTRL_2, ~REG_CTRL_2_OUT1S_MASK, REG_CTRL_2_OUT1S_nIRQ);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Disable alarm interrupt (AIE) in interrupt mask register
    bResult = clearRegisterBit(REG_INT_MASK, REG_INT_MASK_AIE);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Disable alarm
    bResult = maskRegister(REG_TIMER_CTRL, ~REG_TIMER_CTRL_RPT_MASK, REG_TIMER_CTRL_RPT_DIS);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    return true;
}


bool AB1805::interruptCountdownTimer(int value, bool minutes) {
    static const char *errorMsg = "failure in interruptCountdownTimer %d";
    bool bResult;

    // Disable watchdog
    bResult = setWDT(0);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Set FOUT/nIRQ control in OUT1S in Control2 for 
    // "nIRQ if at least one interrupt is enabled, else OUT"
    bResult = maskRegister(REG_CTRL_2, ~REG_CTRL_2_OUT1S_MASK, REG_CTRL_2_OUT1S_nIRQ);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    bResult = setCountdownTimer(value, minutes);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    return true;
}

bool AB1805::deepPowerDown(int seconds) {
    static const char *errorMsg = "failure in deepPowerDown %d";
    bool bResult;

    _log.info("deepPowerDown %d", seconds);

    // Disable watchdog
    bResult = setWDT(0);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

#ifdef SET_D8_LOW
    // With FeatherAB1905v1 board, setting D8 low prior to sleep is necessary
    // to prevent current leakage. In V1, D8 is pulled up to 3V3R. In V2 and
    // later, it's pulled up to 3V3, so it won't be pulled in sleep and this
    // code is not necessary.

    // Set Output Control Register 1 (0x30)
    // O1EN to 1 to enable FOUT/nIRQ in sleep mode.
    bResult = setRegisterBit(REG_OCTRL, REG_OCTRL_O1EN);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Set OUT in Control1 to 0 so the FOUT/nIRQ pin goes low
    bResult = clearRegisterBit(REG_CTRL_1, REG_CTRL_1_OUT);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Make sure SQW is disabled
    bResult = writeRegister(REG_SQW, REG_SQW_DEFAULT);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Set OUT1S in Control2 to 01 so FOUT/nIRQ is set from SQW or OUT. Since SQW is off, this means OUT only.
    // Use this mode so FOUT/nIRQ (D8) won't be affected by the countdown timer nIRQ.
    bResult = maskRegister(REG_CTRL_2, ~REG_CTRL_2_OUT1S_MASK, REG_CTRL_2_OUT1S_SQW);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }
#endif

    bResult = setCountdownTimer(seconds, false);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Make sure STOP (stop clocking system is 0, otherwise sleep mode cannot be entered)
    // PWR2 = 1 (low resistance power switch)
    // (also would probably work with PWR2 = 0, as nIRQ2 should be high-true for sleep mode)
    bResult = maskRegister(REG_CTRL_1, (uint8_t)~(REG_CTRL_1_STOP | REG_CTRL_1_RSP), REG_CTRL_1_PWR2);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Disable the I/O interface in sleep
    bResult = setRegisterBit(REG_OSC_CTRL, REG_OSC_CTRL_PWGT);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // OUT2S = 6 to enable sleep mode
    bResult = maskRegister(REG_CTRL_2, (uint8_t)~REG_CTRL_2_OUT2S_MASK, REG_CTRL_2_OUT2S_SLEEP);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Enter sleep mode and set nRST low
    bResult = writeRegister(REG_SLEEP_CTRL, REG_SLEEP_CTRL_SLP | REG_SLEEP_CTRL_SLRES);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // _log.trace("delay in case we didn't power down");   
    unsigned long start = millis();
    while(millis() - start < (unsigned long) (seconds * 1000)) {
        _log.info("REG_SLEEP_CTRL=0x%2x", readRegister(REG_SLEEP_CTRL));
        delay(1000);
    }

    _log.error("didn't power down");
    System.reset();

    return true;
}

bool AB1805::setTrickle(uint8_t diodeAndRout) {
    static const char *errorMsg = "failure in setTrickle %d";
    bool bResult;

    // Set the key register to enable writes to the trickle regster.
    // Automatically resets to 0 so no need to clear it afterwards
    bResult = writeRegister(REG_CONFIG_KEY, REG_CONFIG_KEY_OTHER);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    uint8_t regValue = ((diodeAndRout != 0) ? REG_TRICKLE_TCS_ENABLE | diodeAndRout : 0x00);

    bResult = writeRegister(REG_TRICKLE, regValue);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    return true;
}

bool AB1805::checkVBAT(uint8_t mask, bool &isAbove) {
    static const char *errorMsg = "failure in checkVBAT %d";
    bool bResult;

    isAbove = false;

    // mask Either REG_ASTAT_BBOD (compare againt BREF) or REG_ASTAT_BMIN (compare against minimum, 1.2V)
    uint8_t trickleValue;
    bResult = readRegister(REG_TRICKLE, trickleValue);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    if (trickleValue != 0) {
        // Disable trickle before checking voltage
        setTrickle(0);

        // Do we need a delay here?   
    }

    uint8_t aStatus;
    bResult = readRegister(REG_ASTAT, aStatus);
    if (bResult) {
        isAbove = (aStatus & mask) != 0;
    }

    if (trickleValue != 0) {
        // Reenable trickle charging since it was enabled before
        setTrickle(trickleValue);
    }

    return true;
}



bool AB1805::setCountdownTimer(int value, bool minutes) {
    static const char *errorMsg = "failure in setCountdownTimer %d";
    bool bResult;

    // Clear any pending interrupts
    bResult = writeRegister(REG_STATUS, REG_STATUS_DEFAULT);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Stop countdown timer if already running since it can't be set while running
    bResult = writeRegister(REG_TIMER_CTRL, REG_TIMER_CTRL_DEFAULT);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Set countdown timer duration
    if (value < 1) {
        value = 1;
    }
    if (value > 255) {
        value = 255;
    }
    bResult = writeRegister(REG_TIMER, (uint8_t)value);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Enable countdown timer interrupt (TIE = 1) in IntMask
    bResult = setRegisterBit(REG_INT_MASK, REG_INT_MASK_TIE);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    // Set the TFS frequency to 1/60 Hz for minutes or 1 Hz for seconds 
    uint8_t tfs = (minutes ? REG_TIMER_CTRL_TFS_1_60 : REG_TIMER_CTRL_TFS_1);

    // Enable countdown timer (TE = 1) in countdown timer control register
    bResult = writeRegister(REG_TIMER_CTRL, REG_TIMER_CTRL_TE | tfs);
    if (!bResult) {
        _log.error(errorMsg, __LINE__);
        return false;
    }

    return true;
}


bool AB1805::readRegister(uint8_t regAddr, uint8_t &value, bool lock) {
    return readRegisters(regAddr, &value, 1, lock);
}

bool AB1805::readRegisters(uint8_t regAddr, uint8_t *array, size_t num, bool lock) {
    bool bResult = false;

    if (lock) {
        wire.lock();
    }

    wire.beginTransmission(i2cAddr);
    wire.write(regAddr);
    int stat = wire.endTransmission(false);
    if (stat == 0) {
        size_t count = wire.requestFrom(i2cAddr, num, true);
        if (count == num) {
            for(size_t ii = 0; ii < num; ii++) {
                array[ii] = wire.read();
            }
            // _log.trace("readRegisters regAddr=%02x num=%u", regAddr, num);
            // _log.dump(array, num);
            // _log.print("\n");

            bResult = true;
        }
        else {
            _log.error("failed to read regAddr=%02x count=%u", regAddr, count);
            bResult = false;
        }
    }
    else {
        _log.error("failed to read regAddr=%02x stat=%d", regAddr, stat);
    }

    if (lock) {
        wire.unlock();
    }
    return bResult;    
}


uint8_t AB1805::readRegister(uint8_t regAddr, bool lock) {
    uint8_t value = 0;

    (void) readRegister(regAddr, value, lock);
    
    return value;
}

bool AB1805::writeRegister(uint8_t regAddr, uint8_t value, bool lock) {
    return writeRegisters(regAddr, &value, 1, lock);
}


bool AB1805::writeRegisters(uint8_t regAddr, const uint8_t *array, size_t num, bool lock) {
    bool bResult = false;

    if (lock) {
        wire.lock();
    }

    wire.beginTransmission(i2cAddr);
    wire.write(regAddr);
    for(size_t ii = 0; ii < num; ii++) {
        wire.write(array[ii]);
    }
    int stat = wire.endTransmission(true);
    if (stat == 0) {
        // _log.trace("writeRegisters regAddr=%02x num=%u", regAddr, num);
        // _log.dump(array, num);
        // _log.print("\n");
        bResult = true;
    }
    else {
        _log.error("failed to write regAddr=%02x stat=%d", regAddr, stat);
    }

    if (lock) {
        wire.unlock();
    }
    return bResult;
}

bool AB1805::maskRegister(uint8_t regAddr, uint8_t andValue, uint8_t orValue, bool lock) {
    bool bResult = false;

    if (lock) {
        wire.lock();
    }

    uint8_t value;

    bResult = readRegister(regAddr, value, false);
    if (bResult) {
        uint8_t newValue = (value & andValue) | orValue;
        
        if (newValue != value) {
            bResult = writeRegister(regAddr, newValue, false);
        }
    }

    if (lock) {
        wire.unlock();
    }
    return bResult;
}

bool AB1805::isBitClear(uint8_t regAddr, uint8_t bitMask, bool lock) {
    bool bResult;
    uint8_t value;

    bResult = readRegister(regAddr, value, lock);
    
    return bResult && ((value & bitMask) == 0);
}

bool AB1805::isBitSet(uint8_t regAddr, uint8_t bitMask, bool lock) {
    bool bResult;
    uint8_t value;

    bResult = readRegister(regAddr, value, lock);
    
    return bResult && ((value & bitMask) != 0);
}


bool AB1805::clearRegisterBit(uint8_t regAddr, uint8_t bitMask, bool lock) {
    return maskRegister(regAddr, ~bitMask, 0x00, lock);
}

bool AB1805::setRegisterBit(uint8_t regAddr, uint8_t bitMask, bool lock) {
    return maskRegister(regAddr, 0xff, bitMask, lock);
}

/**
 * @brief Erases the RTC RAM to 0x00
 */
bool AB1805::eraseRam(bool lock) {
    bool bResult = true;
    uint8_t array[16];

    if (lock) {
        wire.lock();
    }

    memset(array, 0, sizeof(array));
    for(size_t ii = 0; ii < 16; ii++) {
        bResult = writeRam(ii * sizeof(array), array, sizeof(array), false);
        if (!bResult) {
            _log.error("erase failed addr=%u", ii * sizeof(array));
            break;
        }
    }

    if (lock) {
        wire.unlock();
    }

    return bResult;
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
 * The dataLen can be larger than the maximum I2C read. Multiple reads will be done if necessary.
 */
bool AB1805::readRam(size_t ramAddr, uint8_t *data, size_t dataLen, bool lock) {
    bool bResult = true;

    if (lock) {
        wire.lock();
    }

    while(dataLen > 0) {
        size_t count = dataLen;
        if (count > 32) {
            // Too large for a single I2C operation
            count = 32;
        }
        if ((ramAddr < 128) && ((ramAddr + count) > 128)) {
            // Crossing a page boundary
            count = 128 - ramAddr;
        }
        if (ramAddr < 128) {
            clearRegisterBit(REG_EXT_ADDR, REG_EXT_ADDR_XADA);
        }
        else {
            setRegisterBit(REG_EXT_ADDR, REG_EXT_ADDR_XADA);
        }

        bResult = readRegisters(REG_ALT_RAM + (ramAddr & 0x7f), data, count, false);
        if (!bResult) {
            break;
        }
        ramAddr += count;
        dataLen -= count;
        data += count;
    }

    if (lock) {
        wire.unlock();
    }

    return bResult;
}

/**
 * @brief Low-level write call
 *
 * @param ramAddr The address in the RTC RAM to write to
 *
 * @param data The buffer containing the data to write
 *
 * @param dataLen The number of bytes to write
 *
 * The dataLen can be larger than the maximum I2C write. Multiple writes will be done if necessary.
 */
bool AB1805::writeRam(size_t ramAddr, const uint8_t *data, size_t dataLen, bool lock) {
    bool bResult = true;

    if (lock) {
        wire.lock();
    }


    while(dataLen > 0) {
        size_t count = dataLen;
        if (count > 31) {
            // Too large for a single I2C operation
            count = 31;
        }
        if ((ramAddr < 128) && ((ramAddr + count) > 128)) {
            // Crossing a page boundary
            count = 128 - ramAddr;
        }
        if (ramAddr < 128) {
            clearRegisterBit(REG_EXT_ADDR, REG_EXT_ADDR_XADA);
        }
        else {
            setRegisterBit(REG_EXT_ADDR, REG_EXT_ADDR_XADA);
        }

        bResult = writeRegisters(REG_ALT_RAM + (ramAddr & 0x7f), data, count, false);
        if (!bResult) {
            break;
        }
        ramAddr += count;
        dataLen -= count;
        data += count;
    }
    if (lock) {
        wire.unlock();
    }

    return bResult;
}

// [static]
String AB1805::tmToString(const struct tm *timeptr) {
    return String::format("%04d-%02d-%02d %02d:%02d:%02d", 
        timeptr->tm_year + 1900, timeptr->tm_mon + 1, timeptr->tm_mday,
        timeptr->tm_hour, timeptr->tm_min, timeptr->tm_sec);
}

// [static] 
void AB1805::tmToRegisters(const struct tm *timeptr, uint8_t *array, bool includeYear) {
    uint8_t *p = array;
    *p++ = valueToBcd(timeptr->tm_sec);
    *p++ = valueToBcd(timeptr->tm_min);
    *p++ = valueToBcd(timeptr->tm_hour);
    *p++ = valueToBcd(timeptr->tm_mday);
    *p++ = valueToBcd(timeptr->tm_mon + 1); // struct tm is 0-11, not 1-12!
    if (includeYear) {
        *p++ = valueToBcd(timeptr->tm_year % 100);
    }
    *p++ = valueToBcd(timeptr->tm_wday);
}


// [static] 
void AB1805::registersToTm(const uint8_t *array, struct tm *timeptr, bool includeYear) {
    const uint8_t *p = array;
    timeptr->tm_sec = bcdToValue(*p++);
    timeptr->tm_min = bcdToValue(*p++);
    timeptr->tm_hour = bcdToValue(*p++);
    timeptr->tm_mday = bcdToValue(*p++);
    timeptr->tm_mon = bcdToValue(*p++) - 1; // struct tm is 0-11, not 1-12!
    if (includeYear) {
        timeptr->tm_year = bcdToValue(*p++) + 100;
    }
    timeptr->tm_wday = bcdToValue(*p++);
}

// [static] 
int AB1805::bcdToValue(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0f);
}

// [static] 
uint8_t AB1805::valueToBcd(int value) {
    int tens = (value / 10) % 10;
    int ones = value % 10;

    return (uint8_t) ((tens << 4) | ones);
}


void AB1805::systemEvent(system_event_t event, int param) {
    if (event == reset) {
        if (watchdogSecs != 0) {
            setWDT(0);
        }
    }
}

// [static] 
void AB1805::systemEventStatic(system_event_t event, int param) {
    if (instance) {
        instance->systemEvent(event, param);
    }
}



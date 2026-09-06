static Result LedControl_OpenMcu(Handle *handle)
{
    *handle = 0;
    if (!isServiceUsable("mcu::HWC"))
        return -1;

    /* Do not steal the single mcu::HWC session from another process/thread.
     * Rosalina configures srv GetServiceHandle to be non-blocking when a port
     * is full, so a busy service simply gets retried on the next update. */
    return srvGetServiceHandle(handle, "mcu::HWC");
}

static Result LedControl_McuReadRegister(Handle handle, u8 reg, void *data, u32 size)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x1, 2, 2);
    cmdbuf[1] = reg;
    cmdbuf[2] = size;
    cmdbuf[3] = IPC_Desc_Buffer(size, IPC_BUFFER_W);
    cmdbuf[4] = (u32)(uintptr_t)data;

    Result res = svcSendSyncRequest(handle);
    return R_FAILED(res) ? res : (Result)cmdbuf[1];
}

static Result LedControl_McuWriteRegister(Handle handle, u8 reg, const void *data, u32 size)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x2, 2, 2);
    cmdbuf[1] = reg;
    cmdbuf[2] = size;
    cmdbuf[3] = IPC_Desc_Buffer(size, IPC_BUFFER_R);
    cmdbuf[4] = (u32)(uintptr_t)data;

    Result res = svcSendSyncRequest(handle);
    return R_FAILED(res) ? res : (Result)cmdbuf[1];
}

static Result LedControl_McuSetPowerLedState(Handle handle, powerLedState state)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    /* Match libctru MCUHWC_SetPowerLedState exactly. */
    cmdbuf[0] = IPC_MakeHeader(0x6, 2, 0);
    cmdbuf[1] = state;

    Result res = svcSendSyncRequest(handle);
    return R_FAILED(res) ? res : (Result)cmdbuf[1];
}

static bool LedControl_ReadBatteryWithHandle(Handle handle, u16 *socQ8, bool *charging)
{
    u8 data[5]; /* 0x0B..0x0F: SOC int, SOC frac, voltage, unknown, flags */
    Result res = LedControl_McuReadRegister(handle, 0x0B, data, sizeof(data));
    if (R_FAILED(res) || data[0] > 100)
        return false;

    *socQ8 = ((u16)data[0] << 8) | data[1];
    *charging = (data[4] & BIT(4)) != 0;
    return true;
}

static Result LedControl_SetHardwareWithHandle(Handle handle, u8 brightnessPercent, powerLedState state)
{
    u8 brightness = LedControl_BrightnessToRaw(brightnessPercent);
    Result res = LedControl_McuWriteRegister(handle, 0x28, &brightness, 1);
    if (R_SUCCEEDED(res))
        res = LedControl_McuSetPowerLedState(handle, state);
    return res;
}

static LedBatteryWarningState LedControl_GetInstantWarning(u16 socQ8, bool charging)
{
    if (charging || config.lowBatteryMode != LED_LOW_BATTERY_CUSTOM)
        return LED_BATTERY_WARNING_NONE;

    const u16 redEnter = LedControl_TenthsToQ8(config.redThresholdTenths);
    const u16 blinkEnter = LedControl_TenthsToQ8(config.blinkThresholdTenths);
    const u16 redExit = LedControl_TenthsToQ8((u16)config.redThresholdTenths + LOW_BATTERY_HYSTERESIS_TENTHS);
    const u16 blinkExit = LedControl_TenthsToQ8((u16)config.blinkThresholdTenths + LOW_BATTERY_HYSTERESIS_TENTHS);

    switch (warningState) {
        case LED_BATTERY_WARNING_BLINK:
            if (socQ8 <= blinkExit) return LED_BATTERY_WARNING_BLINK;
            if (socQ8 <= redExit) return LED_BATTERY_WARNING_RED;
            return LED_BATTERY_WARNING_NONE;
        case LED_BATTERY_WARNING_RED:
            if (socQ8 <= blinkEnter) return LED_BATTERY_WARNING_BLINK;
            if (socQ8 <= redExit) return LED_BATTERY_WARNING_RED;
            return LED_BATTERY_WARNING_NONE;
        default:
            if (socQ8 <= blinkEnter) return LED_BATTERY_WARNING_BLINK;
            if (socQ8 <= redEnter) return LED_BATTERY_WARNING_RED;
            return LED_BATTERY_WARNING_NONE;
    }
}

static bool LedControl_UpdateWarning(u16 socQ8, bool charging, u64 now)
{
    LedBatteryWarningState target = LedControl_GetInstantWarning(socQ8, charging);

    if (charging || config.lowBatteryMode != LED_LOW_BATTERY_CUSTOM) {
        bool changed = warningState != LED_BATTERY_WARNING_NONE;
        warningState = LED_BATTERY_WARNING_NONE;
        candidateWarningState = warningState;
        candidateWarningSinceMs = now;
        return changed;
    }

    if (target == warningState) {
        candidateWarningState = warningState;
        candidateWarningSinceMs = now;
        return false;
    }

    if (candidateWarningState != target) {
        candidateWarningState = target;
        candidateWarningSinceMs = now;
        return false;
    }

    if (now - candidateWarningSinceMs < WARNING_DEBOUNCE_MS)
        return false;

    warningState = target;
    candidateWarningState = target;
    candidateWarningSinceMs = now;
    return true;
}

static powerLedState LedControl_GetDesiredPowerState(bool isSleeping)
{
    if (warningState == LED_BATTERY_WARNING_BLINK)
        return LED_BLINK_RED;
    if (warningState == LED_BATTERY_WARNING_RED)
        return LED_RED;

    if (isSleeping) {
        switch ((LedSleepMode)config.sleepMode) {
            case LED_SLEEP_DIM_STATIC: return LED_NORMAL;
            case LED_SLEEP_OFF: return LED_OFF;
            default: return LED_SLEEP_MODE;
        }
    }

    return config.lowBatteryMode == LED_LOW_BATTERY_SYSTEM ? LED_POWER_AUTO : LED_NORMAL;
}

static Result LedControl_ApplyHardwareWithHandle(Handle handle, bool isSleeping)
{
    u8 brightness = isSleeping ? config.sleepBrightness : config.awakeBrightness;
    return LedControl_SetHardwareWithHandle(handle, brightness, LedControl_GetDesiredPowerState(isSleeping));
}

static bool LedControl_CrossedStockWarningThreshold(u16 socQ8)
{
    if (!lastSocValid)
        return false;

    const u16 tenPercent = LedControl_TenthsToQ8(100);
    const u16 fivePercent = LedControl_TenthsToQ8(50);
    return (lastSocQ8 > tenPercent && socQ8 <= tenPercent) ||
           (lastSocQ8 > fivePercent && socQ8 <= fivePercent);
}

static void LedControl_ScheduleNext(u16 socQ8, bool charging, u64 now)
{
    u16 tenths = LedControl_Q8ToTenths(socQ8);
    u64 interval;

    if (charging)
        interval = UPDATE_INTERVAL_NORMAL_MS;
    else if (tenths <= BATTERY_CRITICAL_POLL_TENTHS)
        interval = UPDATE_INTERVAL_CRITICAL_MS;
    else if (tenths <= BATTERY_LOW_POLL_TENTHS)
        interval = UPDATE_INTERVAL_LOW_MS;
    else if (tenths <= BATTERY_MID_POLL_TENTHS)
        interval = UPDATE_INTERVAL_MID_MS;
    else
        interval = UPDATE_INTERVAL_NORMAL_MS;

    nextUpdateMs = now + interval;
}


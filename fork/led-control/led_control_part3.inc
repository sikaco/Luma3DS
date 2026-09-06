void LedControl_Init(void)
{
    LightLock_Init(&ledLock);
    LedControl_LoadConfig();
    LedControl_UpdateMenuTitles();
    warningState = LED_BATTERY_WARNING_NONE;
    candidateWarningState = LED_BATTERY_WARNING_NONE;
    candidateWarningSinceMs = 0;
    initialized = true;
    configDirty = false;
    lastSocValid = false;
    nextUpdateMs = 0;
    sleeping = false;
    forceApply = true;
}

void LedControl_SetSleeping(bool value)
{
    if (!initialized)
        return;

    LightLock_Lock(&ledLock);
    sleeping = value;
    forceApply = true;
    nextUpdateMs = 0;
    LightLock_Unlock(&ledLock);
}

void LedControl_RequestUpdate(void)
{
    if (!initialized)
        return;

    LightLock_Lock(&ledLock);
    forceApply = true;
    nextUpdateMs = 0;
    LightLock_Unlock(&ledLock);
}

void LedControl_OnGoingToSleep(void)
{
    if (!initialized)
        return;

    LightLock_Lock(&ledLock);
    sleeping = true;
    /* LED_SLEEP_MODE is a fixed MCU animation and ignores register 0x28 for
     * the blue Power LED. sleepBrightness still affects Wi-Fi; choose
     * "Dim static" for an actually dim Power LED while asleep. */
    Handle mcuHandle;
    if (R_SUCCEEDED(LedControl_OpenMcu(&mcuHandle))) {
        LedControl_ApplyHardwareWithHandle(mcuHandle, true);
        svcCloseHandle(mcuHandle);
        forceApply = false;
    } else {
        forceApply = true;
    }
    nextUpdateMs = 0;
    LightLock_Unlock(&ledLock);
}

void LedControl_Update(void)
{
    if (!initialized)
        return;

    LightLock_Lock(&ledLock);
    if (sleeping && !forceApply) {
        LightLock_Unlock(&ledLock);
        return;
    }

    u64 now = LedControl_GetTickMs();
    bool explicitApply = forceApply;
    if (!explicitApply && now < nextUpdateMs) {
        LightLock_Unlock(&ledLock);
        return;
    }
    forceApply = false;

    Handle mcuHandle;
    if (R_FAILED(LedControl_OpenMcu(&mcuHandle))) {
        forceApply = true;
        nextUpdateMs = now + UPDATE_INTERVAL_RETRY_MS;
        LightLock_Unlock(&ledLock);
        return;
    }

    u16 socQ8;
    bool charging;
    if (!LedControl_ReadBatteryWithHandle(mcuHandle, &socQ8, &charging)) {
        svcCloseHandle(mcuHandle);
        forceApply = true;
        nextUpdateMs = now + UPDATE_INTERVAL_RETRY_MS;
        LightLock_Unlock(&ledLock);
        return;
    }

    bool warningChanged = LedControl_UpdateWarning(socQ8, charging, now);
    bool stockThresholdCrossed = LedControl_CrossedStockWarningThreshold(socQ8);
    bool mustApply = explicitApply || !lastSocValid || warningChanged;
    if (config.lowBatteryMode != LED_LOW_BATTERY_SYSTEM && stockThresholdCrossed)
        mustApply = true;

    if (mustApply)
        LedControl_ApplyHardwareWithHandle(mcuHandle, sleeping);

    svcCloseHandle(mcuHandle);
    lastSocQ8 = socQ8;
    lastSocValid = true;
    LedControl_ScheduleNext(socQ8, charging, now);
    LightLock_Unlock(&ledLock);
}

static void LedControl_MarkChanged(void)
{
    LightLock_Lock(&ledLock);
    configDirty = true;
    LedControl_UpdateMenuTitles();
    forceApply = true;
    nextUpdateMs = 0;
    LightLock_Unlock(&ledLock);
    LedControl_Update();
}

static void LedControl_SaveIfDirty(void)
{
    LightLock_Lock(&ledLock);
    if (configDirty)
        LedControl_SaveConfig();
    LightLock_Unlock(&ledLock);
}


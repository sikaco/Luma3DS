static void LedControl_AdjustPercentage(const char *title, u8 *value)
{
    u8 original = *value;

    do {
        Draw_Lock();
        Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, title);
        Draw_DrawFormattedString(10, 30, COLOR_WHITE, "Current value: %u%%\n\n", (unsigned)*value);
        Draw_DrawString(10, 60, COLOR_WHITE, "Up/Down: +/-1    Right/Left: +/-10\n");
        Draw_DrawString(10, 80, COLOR_WHITE, "A: save    B: cancel");
        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);
        if (pressed & KEY_A) {
            if (*value != original) {
                LedControl_MarkChanged();
                LedControl_SaveIfDirty();
            }
            return;
        }
        if (pressed & KEY_B) {
            LightLock_Lock(&ledLock);
            *value = original;
            LedControl_UpdateMenuTitles();
            forceApply = true;
            nextUpdateMs = 0;
            LightLock_Unlock(&ledLock);
            LedControl_Update();
            return;
        }

        s32 updated = *value;
        if (pressed & KEY_UP) updated++;
        if (pressed & KEY_DOWN) updated--;
        if (pressed & KEY_RIGHT) updated += 10;
        if (pressed & KEY_LEFT) updated -= 10;
        if (updated < 0) updated = 0;
        if (updated > 100) updated = 100;

        if ((u8)updated != *value) {
            LightLock_Lock(&ledLock);
            *value = (u8)updated;
            LightLock_Unlock(&ledLock);
            LedControl_MarkChanged();
        }
    } while (!menuShouldExit);
}

static void LedControl_AdjustThreshold(const char *title, u8 *value, bool isBlink)
{
    u8 original = *value;

    do {
        Draw_Lock();
        Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, title);
        Draw_DrawFormattedString(10, 30, COLOR_WHITE, "Current value: %u.%u%%\n\n",
            (unsigned)(*value / 10), (unsigned)(*value % 10));
        Draw_DrawString(10, 60, COLOR_WHITE, "Up/Down: +/-0.1    Right/Left: +/-0.5\n");
        Draw_DrawString(10, 80, COLOR_WHITE, "A: save    B: cancel");
        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);
        if (pressed & KEY_A) {
            if (*value != original) {
                LedControl_MarkChanged();
                LedControl_SaveIfDirty();
            }
            return;
        }
        if (pressed & KEY_B) {
            LightLock_Lock(&ledLock);
            *value = original;
            LedControl_UpdateMenuTitles();
            forceApply = true;
            nextUpdateMs = 0;
            LightLock_Unlock(&ledLock);
            LedControl_Update();
            return;
        }

        s32 updated = *value;
        if (pressed & KEY_UP) updated++;
        if (pressed & KEY_DOWN) updated--;
        if (pressed & KEY_RIGHT) updated += 5;
        if (pressed & KEY_LEFT) updated -= 5;
        if (updated < 1) updated = 1;
        if (updated > 100) updated = 100;

        if (isBlink && updated > config.redThresholdTenths)
            updated = config.redThresholdTenths;
        if (!isBlink && updated < config.blinkThresholdTenths)
            updated = config.blinkThresholdTenths;

        if ((u8)updated != *value) {
            LightLock_Lock(&ledLock);
            *value = (u8)updated;
            LightLock_Unlock(&ledLock);
            LedControl_MarkChanged();
        }
    } while (!menuShouldExit);
}

static void LedControl_AdjustAwakeBrightness(void)
{
    LedControl_AdjustPercentage("Power/Wi-Fi brightness while awake", &config.awakeBrightness);
}

static void LedControl_AdjustSleepBrightness(void)
{
    LedControl_AdjustPercentage("Wi-Fi/static sleep brightness", &config.sleepBrightness);
}

static void LedControl_CycleSleepMode(void)
{
    LightLock_Lock(&ledLock);
    config.sleepMode = (config.sleepMode + 1) % 3;
    LightLock_Unlock(&ledLock);
    LedControl_MarkChanged();
    LedControl_SaveIfDirty();
}

static void LedControl_CycleLowBatteryMode(void)
{
    LightLock_Lock(&ledLock);
    config.lowBatteryMode = (config.lowBatteryMode + 1) % 3;
    warningState = LED_BATTERY_WARNING_NONE;
    candidateWarningState = LED_BATTERY_WARNING_NONE;
    candidateWarningSinceMs = LedControl_GetTickMs();
    LightLock_Unlock(&ledLock);
    LedControl_MarkChanged();
    LedControl_SaveIfDirty();
}

static void LedControl_AdjustRedThreshold(void)
{
    LedControl_AdjustThreshold("Low battery red threshold", &config.redThresholdTenths, false);
}

static void LedControl_AdjustBlinkThreshold(void)
{
    LedControl_AdjustThreshold("Low battery blink threshold", &config.blinkThresholdTenths, true);
}

static void LedControl_ShowHardwareNotes(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_DrawString(10, 10, COLOR_TITLE, "LED hardware limits");
    Draw_DrawString(10, 30, COLOR_WHITE,
        "Power and Wi-Fi LEDs share one brightness register.\n"
        "The stock blue sleep breathing animation is generated\n"
        "inside the MCU and ignores that brightness register.\n"
        "Use Dim static for a genuinely dim sleep Power LED.\n\n"
        "The orange charging LED has no known safe brightness\n"
        "control interface, so this fork leaves it unchanged.\n\n"
        "Press B to go back.");
    Draw_FlushFramebuffer();
    Draw_Unlock();

    while (!menuShouldExit) {
        if (waitInputWithTimeout(1000) & KEY_B)
            return;
    }
}

static void LedControl_ResetDefaults(void)
{
    LightLock_Lock(&ledLock);
    LedControl_SetDefaults();
    warningState = LED_BATTERY_WARNING_NONE;
    candidateWarningState = LED_BATTERY_WARNING_NONE;
    candidateWarningSinceMs = LedControl_GetTickMs();
    configDirty = true;
    LightLock_Unlock(&ledLock);
    LedControl_MarkChanged();
    LedControl_SaveIfDirty();
}

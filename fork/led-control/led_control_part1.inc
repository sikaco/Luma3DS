/*
*   This file is part of Luma3DS
*   Copyright (C) 2026 Luma3DS contributors
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*/

#include <3ds.h>
#include <string.h>
#include "led_control.h"
#include "draw.h"
#include "fmt.h"
#include "ifile.h"
#include "utils.h"

#define LED_CONFIG_MAGIC                 0x3144454Cu /* "LED1" */
#define LED_CONFIG_VERSION               1
#define LED_CONFIG_PATH                  "/luma/led_config.bin"

#define DEFAULT_AWAKE_BRIGHTNESS         100
#define DEFAULT_SLEEP_BRIGHTNESS         20
#define DEFAULT_RED_THRESHOLD_TENTHS     15  /* 1.5% */
#define DEFAULT_BLINK_THRESHOLD_TENTHS   12  /* 1.2% */

#define LOW_BATTERY_HYSTERESIS_TENTHS    3   /* 0.3% */
#define WARNING_DEBOUNCE_MS              2000ULL

#define UPDATE_INTERVAL_NORMAL_MS        30000ULL
#define UPDATE_INTERVAL_MID_MS           5000ULL
#define UPDATE_INTERVAL_LOW_MS           1000ULL
#define UPDATE_INTERVAL_CRITICAL_MS      250ULL
#define UPDATE_INTERVAL_RETRY_MS         500ULL

#define BATTERY_MID_POLL_TENTHS          200 /* 20.0% */
#define BATTERY_LOW_POLL_TENTHS          120 /* 12.0% */
#define BATTERY_CRITICAL_POLL_TENTHS     20  /* 2.0% */

/* mcu::HWC accepts state 0 for automatic system control, while libctru's
 * public powerLedState enum starts at LED_NORMAL (1). */
#define LED_POWER_AUTO ((powerLedState)0)

typedef enum LedSleepMode {
    LED_SLEEP_STOCK_BREATHING = 0,
    LED_SLEEP_DIM_STATIC,
    LED_SLEEP_OFF,
} LedSleepMode;

typedef enum LedLowBatteryMode {
    LED_LOW_BATTERY_CUSTOM = 0,
    LED_LOW_BATTERY_SYSTEM,
    LED_LOW_BATTERY_DISABLED,
} LedLowBatteryMode;

typedef enum LedBatteryWarningState {
    LED_BATTERY_WARNING_NONE = 0,
    LED_BATTERY_WARNING_RED,
    LED_BATTERY_WARNING_BLINK,
} LedBatteryWarningState;

typedef struct __attribute__((packed)) LedControlConfig {
    u32 magic;
    u16 version;
    u8 awakeBrightness;
    u8 sleepBrightness;
    u8 sleepMode;
    u8 lowBatteryMode;
    u8 redThresholdTenths;
    u8 blinkThresholdTenths;
    u8 reserved[4];
} LedControlConfig;

static LedControlConfig config;
static LightLock ledLock;
static LedBatteryWarningState warningState;
static LedBatteryWarningState candidateWarningState;
static u64 candidateWarningSinceMs;
static bool initialized;
static bool configDirty;
static bool lastSocValid;
static u16 lastSocQ8;
static u64 nextUpdateMs;
static bool sleeping;
static bool forceApply;

static char awakeBrightnessTitle[64];
static char sleepModeTitle[64];
static char sleepBrightnessTitle[64];
static char lowBatteryModeTitle[64];
static char redThresholdTitle[64];
static char blinkThresholdTitle[64];

static void LedControl_AdjustAwakeBrightness(void);
static void LedControl_CycleSleepMode(void);
static void LedControl_AdjustSleepBrightness(void);
static void LedControl_CycleLowBatteryMode(void);
static void LedControl_AdjustRedThreshold(void);
static void LedControl_AdjustBlinkThreshold(void);
static void LedControl_ShowHardwareNotes(void);
static void LedControl_ResetDefaults(void);

Menu ledControlMenu = {
    "LED control",
    {
        { awakeBrightnessTitle, METHOD, .method = &LedControl_AdjustAwakeBrightness },
        { sleepModeTitle, METHOD, .method = &LedControl_CycleSleepMode },
        { sleepBrightnessTitle, METHOD, .method = &LedControl_AdjustSleepBrightness },
        { lowBatteryModeTitle, METHOD, .method = &LedControl_CycleLowBatteryMode },
        { redThresholdTitle, METHOD, .method = &LedControl_AdjustRedThreshold },
        { blinkThresholdTitle, METHOD, .method = &LedControl_AdjustBlinkThreshold },
        { "LED hardware notes", METHOD, .method = &LedControl_ShowHardwareNotes },
        { "Reset LED settings to defaults", METHOD, .method = &LedControl_ResetDefaults },
        {},
    }
};

static inline u64 LedControl_GetTickMs(void)
{
    return (1000ULL * svcGetSystemTick()) / SYSCLOCK_ARM11;
}

static inline u16 LedControl_TenthsToQ8(u16 tenths)
{
    return (u16)(((u32)tenths * 256U + 5U) / 10U);
}

static inline u16 LedControl_Q8ToTenths(u16 q8)
{
    return (u16)(((u32)q8 * 10U + 128U) / 256U);
}

static u8 LedControl_BrightnessToRaw(u8 percent)
{
    return (u8)(((u32)percent * 255U + 50U) / 100U);
}

static FS_ArchiveID LedControl_GetConfigArchive(void)
{
    s64 out = 0;
    svcGetSystemInfo(&out, 0x10000, 0x203);
    return (bool)out ? ARCHIVE_SDMC : ARCHIVE_NAND_RW;
}

static void LedControl_SetDefaults(void)
{
    memset(&config, 0, sizeof(config));
    config.magic = LED_CONFIG_MAGIC;
    config.version = LED_CONFIG_VERSION;
    config.awakeBrightness = DEFAULT_AWAKE_BRIGHTNESS;
    config.sleepBrightness = DEFAULT_SLEEP_BRIGHTNESS;
    config.sleepMode = LED_SLEEP_STOCK_BREATHING;
    config.lowBatteryMode = LED_LOW_BATTERY_CUSTOM;
    config.redThresholdTenths = DEFAULT_RED_THRESHOLD_TENTHS;
    config.blinkThresholdTenths = DEFAULT_BLINK_THRESHOLD_TENTHS;
}

static bool LedControl_ValidateConfig(const LedControlConfig *cfg)
{
    return cfg->magic == LED_CONFIG_MAGIC &&
           cfg->version == LED_CONFIG_VERSION &&
           cfg->awakeBrightness <= 100 &&
           cfg->sleepBrightness <= 100 &&
           cfg->sleepMode <= LED_SLEEP_OFF &&
           cfg->lowBatteryMode <= LED_LOW_BATTERY_DISABLED &&
           cfg->redThresholdTenths >= 1 && cfg->redThresholdTenths <= 100 &&
           cfg->blinkThresholdTenths >= 1 &&
           cfg->blinkThresholdTenths <= cfg->redThresholdTenths;
}

static void LedControl_LoadConfig(void)
{
    IFile file;
    u64 total = 0;
    LedControlConfig diskConfig;

    LedControl_SetDefaults();
    Result res = IFile_Open(&file, LedControl_GetConfigArchive(),
        fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, LED_CONFIG_PATH), FS_OPEN_READ);
    if (R_FAILED(res))
        return;

    res = IFile_Read(&file, &total, &diskConfig, sizeof(diskConfig));
    IFile_Close(&file);
    if (R_SUCCEEDED(res) && total == sizeof(diskConfig) && LedControl_ValidateConfig(&diskConfig))
        config = diskConfig;
}

static Result LedControl_SaveConfig(void)
{
    IFile file;
    u64 total = 0;
    config.magic = LED_CONFIG_MAGIC;
    config.version = LED_CONFIG_VERSION;

    Result res = IFile_Open(&file, LedControl_GetConfigArchive(),
        fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, LED_CONFIG_PATH),
        FS_OPEN_CREATE | FS_OPEN_WRITE);
    if (R_FAILED(res))
        return res;

    res = IFile_SetSize(&file, sizeof(config));
    if (R_SUCCEEDED(res))
        res = IFile_Write(&file, &total, &config, sizeof(config), 0);
    IFile_Close(&file);

    if (R_SUCCEEDED(res) && total == sizeof(config))
        configDirty = false;
    return res;
}

static const char *LedControl_GetSleepModeName(void)
{
    switch ((LedSleepMode)config.sleepMode) {
        case LED_SLEEP_DIM_STATIC: return "Dim static";
        case LED_SLEEP_OFF: return "Off";
        default: return "Stock breathing";
    }
}

static const char *LedControl_GetLowBatteryModeName(void)
{
    switch ((LedLowBatteryMode)config.lowBatteryMode) {
        case LED_LOW_BATTERY_SYSTEM: return "System default";
        case LED_LOW_BATTERY_DISABLED: return "Disabled";
        default: return "Custom";
    }
}

static void LedControl_UpdateMenuTitles(void)
{
    sprintf(awakeBrightnessTitle, "Power/Wi-Fi brightness (awake): %u%%", (unsigned)config.awakeBrightness);
    sprintf(sleepModeTitle, "Power LED while asleep: %s", LedControl_GetSleepModeName());
    sprintf(sleepBrightnessTitle, "Wi-Fi/static sleep brightness: %u%%", (unsigned)config.sleepBrightness);
    sprintf(lowBatteryModeTitle, "Low battery warning: %s", LedControl_GetLowBatteryModeName());
    sprintf(redThresholdTitle, "Low battery red threshold: %u.%u%%",
        (unsigned)(config.redThresholdTenths / 10), (unsigned)(config.redThresholdTenths % 10));
    sprintf(blinkThresholdTitle, "Low battery blink threshold: %u.%u%%",
        (unsigned)(config.blinkThresholdTenths / 10), (unsigned)(config.blinkThresholdTenths % 10));
}


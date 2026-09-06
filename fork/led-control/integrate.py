#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one integration anchor, found {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


# Rosalina System configuration menu
replace_once(
    "sysmodules/rosalina/source/menus/sysconfig.c",
    '#include "luminance.h"\n',
    '#include "luminance.h"\n#include "led_control.h"\n',
)
replace_once(
    "sysmodules/rosalina/source/menus/sysconfig.c",
    '        { "Toggle LEDs", METHOD, .method = &SysConfigMenu_ToggleLEDs },\n',
    '        { "LED control", MENU, .menu = &ledControlMenu },\n',
)

# Rosalina periodic runtime hook
replace_once(
    "sysmodules/rosalina/source/menu.c",
    '#include "shell.h"\n',
    '#include "shell.h"\n#include "led_control.h"\n',
)
replace_once(
    "sysmodules/rosalina/source/menu.c",
    '        svcSleepThread(50 * 1000 * 1000LL);\n        if (menuShouldExit)\n',
    '        svcSleepThread(50 * 1000 * 1000LL);\n        LedControl_Update();\n        if (menuShouldExit)\n',
)
replace_once(
    "sysmodules/rosalina/source/menu.c",
    '        u32 pressed = waitInputWithTimeout(1000);\n        numItems = menuCountItems(currentMenu);\n',
    '        u32 pressed = waitInputWithTimeout(1000);\n        LedControl_Update();\n        numItems = menuCountItems(currentMenu);\n',
)

# Sleep/wake, low-battery and charger notifications
replace_once(
    "sysmodules/rosalina/source/main.c",
    '#include "shell.h"\n',
    '#include "shell.h"\n#include "led_control.h"\n',
)
replace_once(
    "sysmodules/rosalina/source/main.c",
    '''        case PTMNOTIFID_GOING_TO_SLEEP:\n        case PTMNOTIFID_SLEEP_ALLOWED:\n        case PTMNOTIFID_FULLY_WAKING_UP:\n        case PTMNOTIFID_HALF_AWAKE:\n            PTMSYSM_NotifySleepPreparationComplete(ackValue);\n            break;\n        case PTMNOTIFID_SLEEP_DENIED:\n        case PTMNOTIFID_FULLY_AWAKE:\n            menuShouldExit = false;\n            break;\n''',
    '''        case PTMNOTIFID_GOING_TO_SLEEP:\n            LedControl_OnGoingToSleep();\n            PTMSYSM_NotifySleepPreparationComplete(ackValue);\n            break;\n        case PTMNOTIFID_SLEEP_ALLOWED:\n        case PTMNOTIFID_FULLY_WAKING_UP:\n        case PTMNOTIFID_HALF_AWAKE:\n            PTMSYSM_NotifySleepPreparationComplete(ackValue);\n            break;\n        case PTMNOTIFID_SLEEP_DENIED:\n            LedControl_SetSleeping(false);\n            menuShouldExit = false;\n            break;\n        case PTMNOTIFID_FULLY_AWAKE:\n            LedControl_SetSleeping(false);\n            menuShouldExit = false;\n            break;\n''',
)
replace_once(
    "sysmodules/rosalina/source/main.c",
    '''        handleShellOpened();\n        menuShouldExit = false;\n    } else {\n        // Shell closed\n        menuShouldExit = true;\n    }\n\n}\n\nstatic void handlePreTermNotification''',
    '''        handleShellOpened();\n        LedControl_SetSleeping(false);\n        menuShouldExit = false;\n    } else {\n        // Shell closed\n        LedControl_SetSleeping(true);\n        menuShouldExit = true;\n    }\n\n}\n\nstatic void handleLedPowerNotification(u32 notificationId)\n{\n    (void)notificationId;\n    LedControl_RequestUpdate();\n}\n\nstatic void handlePreTermNotification''',
)
replace_once(
    "sysmodules/rosalina/source/main.c",
    '''    { PTMNOTIFID_HALF_AWAKE,        handleSleepNotification                 },\n    { 0x213,                        handleShellNotification                 },\n''',
    '''    { PTMNOTIFID_HALF_AWAKE,        handleSleepNotification                 },\n    { 0x20D,                        handleLedPowerNotification               }, // charger removed\n    { 0x20E,                        handleLedPowerNotification               }, // charger plugged in\n    { 0x211,                        handleLedPowerNotification               }, // battery very low (5%)\n    { 0x212,                        handleLedPowerNotification               }, // battery low (10%)\n    { 0x213,                        handleShellNotification                 },\n''',
)
replace_once(
    "sysmodules/rosalina/source/main.c",
    '''    ScreenFiltersMenu_LoadConfig();\n    SysConfigMenu_LoadConfig();\n\n    MyThread *menuThread = menuCreateThread();\n''',
    '''    ScreenFiltersMenu_LoadConfig();\n    SysConfigMenu_LoadConfig();\n    LedControl_Init();\n\n    MyThread *menuThread = menuCreateThread();\n''',
)

print("LED-control integration applied successfully")

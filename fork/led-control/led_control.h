/*
*   This file is part of Luma3DS
*   Copyright (C) 2026 Luma3DS contributors
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*/

#pragma once

#include <3ds/types.h>
#include "menu.h"

extern Menu ledControlMenu;

void LedControl_Init(void);
void LedControl_Update(void);
void LedControl_SetSleeping(bool sleeping);
void LedControl_RequestUpdate(void);
void LedControl_OnGoingToSleep(void);

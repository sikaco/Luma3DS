#!/usr/bin/env bash
set -euo pipefail
repo="${1:-.}"
cd "$repo"
cp fork/led-control/led_control.c sysmodules/rosalina/source/led_control.c
cp fork/led-control/led_control_part*.inc sysmodules/rosalina/source/
cp fork/led-control/led_control.h sysmodules/rosalina/include/led_control.h
git apply --check fork/led-control/integration.patch
git apply fork/led-control/integration.patch

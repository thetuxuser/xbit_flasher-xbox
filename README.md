# xbit_flasher

[![CI](https://github.com/thetuxuser/xbit_flasher-xbox/actions/workflows/ci.yml/badge.svg)](https://github.com/thetuxuser/xbit_flasher-xbox/actions/workflows/ci.yml)
[![CodeQL](https://github.com/thetuxuser/xbit_flasher-xbox/actions/workflows/codeql.yml/badge.svg)](https://github.com/thetuxuser/xbit_flasher-xbox/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)

Cross-platform command-line tool to flash the **X-BIT modchip** (manufactured by DMS3 Team)
for the Original Xbox.  Works on Linux, macOS, and Windows via [hidapi](https://github.com/libusb/hidapi).

> **Fork of** [original xbit_flasher-xbox](https://github.com/tuxuser/xbit_flasher-xbox) with modernised build system, CI, and platform improvements.

---

## Features

| Feature | Status |
|---|---|
| Read bank | ✅ |
| Write / flash bank | ✅ |
| Verify bank | ✅ |
| Format chip (set layout) | ✅ |
| All 6 bank layouts | ✅ |
| Linux, macOS, Windows | ✅ |
| udev rules (no-root on Linux) | ✅ |
| CMake + legacy Makefile | ✅ |
| GitHub Actions CI | ✅ |

---

## Hardware

The X-BIT is an ST Microelectronics STR750 (8051-core) based modchip exposed as a USB HID device:

| Field | Value |
|---|---|
| USB VID | `0x0483` (ST Microelectronics) |
| USB PID | `0x0000` |
| Product string | `DK3200 Evaluation Board` |

---

## Requirements

| Platform | Dependency |
|---|---|
| Linux | `libhidapi-dev` (`hidraw` backend recommended) |
| macOS | `hidapi` via Homebrew |
| Windows | `hidapi` via vcpkg |

---

## Building

### CMake (recommended)

```bash
# Linux
sudo apt-get install libhidapi-dev cmake ninja-build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# macOS
brew install hidapi cmake ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Windows (PowerShell, vcpkg)
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

### Legacy Makefile

```bash
make          # auto-detects hidapi backend
make install  # installs binary + udev rules
```

---

## Linux: USB access without root

Install the bundled udev rule so your user account can access the device:

```bash
sudo cp udev/99-xbit-flasher.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo usermod -aG plugdev $USER   # log out and back in
```

---

## Usage

```
xbit_flasher [options]

Options:
  -m <mode>     r=read, w=write, v=verify, f=format
  -b <bank>     Bank number (0-5)
  -l <layout>   Memory layout (1-6, see below)
  -f <file>     BIOS image file
  -h            Print this help

Memory layouts (sizes in KB):
  1:  512  512  256  256  256  256
  2: 1024  256  256  256  256
  3: 1024  512  256  256
  4: 1024  512  512
  5: 1024 1024
  6: 2048
```

### Examples

```bash
# Format chip with layout 5 (1 MB + 1 MB)
./xbit_flasher -m f -l 5

# Write a BIOS image to bank 0
./xbit_flasher -m w -b 0 -l 5 -f evox.bin

# Read bank 0 back to a file
./xbit_flasher -m r -b 0 -l 5 -f readback.bin

# Verify bank 0 against a file
./xbit_flasher -m v -b 0 -l 5 -f evox.bin
```

---

## hookDll (Windows reverse-engineering aid)

`hookDll/` contains an injectable DLL that hooks the original `XBIT_v1.0.exe` Windows
flashing tool (included in binary form) and logs all flash read/write/erase calls.
Useful for reverse engineering the protocol.

**Requires:** [MinHook](https://github.com/TsudaKageyu/minhook)

```bash
cmake -B build -DBUILD_HOOKDLL=ON
cmake --build build
# Inject xbit_hook.dll into XBIT_v1.0.exe using your DLL injector of choice
```

> Note: the original tool only works reliably on Windows 2000/XP.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Device not found | Replug USB; check `dmesg` for correct manufacturer string |
| `Permission denied` on Linux | Install udev rules (see above) |
| Layout mismatch error | Replug USB and run tool again; or re-format chip |
| Flashing fails intermittently | Known hardware issue with X-BIT's USB interface — retry |
| Works on some USB controllers, not others | Prefer NVIDIA nForce or VIA USB controllers; Intel xHCI can be problematic |

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

---

## Credits

- Original tool by the author of [xbit_flasher-xbox](https://github.com/tuxuser/xbit_flasher-xbox)
- Based on *WinApp DK3200 USB DEMO* by ST Microelectronics
- hidapi by [libusb](https://github.com/libusb/hidapi)

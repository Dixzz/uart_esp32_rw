# Safe OTA Firmware Update — ESP32 + STM32

Over-the-air firmware update system with automatic rollback. ESP32 downloads firmware via MQTT, verifies integrity with CRC32, and flashes an STM32F401RE using the AN3155 UART bootloader protocol. If verification fails after flashing, the previous firmware is automatically restored.

## Architecture
```
MQTT Broker (laptop/cloud)
│
▼
┌────────┐ UART (AN3155) ┌──────────────┐
│ ESP32 │ ◄─────────────────────────► │ STM32F401RE │
│ │ sync, erase, write, read, │ │
│ backup │ go commands │ user app │
└────────┘ └──────────────┘
```

## Features

- **MQTT-triggered OTA**: Firmware update initiated by MQTT message
- **CRC32 verification**: Incoming firmware validated before and after flashing
- **Version management**: Skips downgrades and duplicate versions
- **Automatic backup**: Current firmware read via `0x11` command before erasing
- **Rollback on failure**: Corrupted firmware detected post-flash → previous version restored automatically
- **Self-contained**: No PC, no ST-LINK, no external tools required at runtime

## How It Works

### Safe Update Flow
```
[MQTT: new firmware + CRC + version]
│
▼
[CRC check on received data] ── FAIL ──► [Abort: CRC_PRE_FAIL]
│
▼
[Version check] ── version ≤ current ──► [Abort: ALREADY_CURRENT]
│
▼
[Backup current firmware] ← read_memory(0x08000000)
│
▼
[Global erase + write new firmware]
│
▼
[Read back + CRC verify] ── FAIL ──► [Erase + write backup] ──► [Rollback complete]
│
▼
[Boot new firmware]
```

### Rollback

If the newly written firmware's CRC doesn't match the expected value, the backup is automatically re-flashed and the STM32 boots the previous version. The device never bricks.

## MQTT Topics

| Topic | Direction | Payload |
|-------|-----------|---------|
| `testtopic/222m` | Laptop → ESP32 | `{"msg": 2318574364}` |
| `testtopic/221m` | Laptop → ESP32 | Raw firmware binary (chunked) |

## Hardware

| Device | Role |
|--------|------|
| ESP32-WROVER | MQTT client, OTA manager, UART flasher |
| STM32F401RE (Nucleo) | Target device, runs user firmware |
| Logic analyzer | Debugging and verification (optional) |

## UART Bootloader (AN3155)

Implements ST's USART bootloader protocol:

| Command | Byte | Purpose |
|---------|------|---------|
| Sync | `0x7F` | Auto-baud detection |
| Get Version | `0x01` | Read bootloader protocol version |
| Read Memory | `0x11` | Backup current firmware |
| Write Memory | `0x31` | Flash new firmware (256-byte chunks) |
| Extended Erase | `0x44` | Global mass erase |
| Go | `0x21` | Boot firmware at `0x08000000` |

## Firmware Versioning

User firmware embeds a version byte at a fixed flash address (`0x08000200`). The ESP32 reads this before updating:

```c
// In STM32 firmware:
const uint8_t firmware_version __attribute__((section(".firmware_meta"))) = 0x02;

// In STM32 linker script:
.firmware_meta 0x08000200 :
{
    . = ALIGN(4);
    KEEP(*(.firmware_meta))
    . = ALIGN(4);
} >FLASH

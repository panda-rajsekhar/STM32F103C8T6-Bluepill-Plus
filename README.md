# STM32F103C8T6 "Bluepill Plus" — Bare-Metal Programming with ST-Link V2

A hands-on, register-level (no HAL, no CubeMX auto-generated peripheral code) exploration of the STM32F103C8T6 "Bluepill" board using an ST-Link V2 programmer/debugger. This repository documents the hardware, the ARM Cortex-M3 architecture underneath it, and a working bare-metal GPIO blink example, along with the practical gotchas encountered along the way.

Written for anyone learning embedded systems who wants to understand what's actually happening under the HAL/Arduino abstraction layers.

---

## Table of Contents

1. [Hardware Overview](#hardware-overview)
2. [The ARM Cortex-M3 Core](#the-arm-cortex-m3-core)
3. [STM32F103C8T6 Microcontroller](#stm32f103c8t6-microcontroller)
4. [Board Specifics — WeAct Bluepill Plus](#board-specifics--weact-bluepill-plus)
5. [ST-Link V2 Programmer](#st-link-v2-programmer)
6. [Wiring / SWD Connection](#wiring--swd-connection)
7. [Toolchain](#toolchain)
8. [Bare-Metal GPIO Example](#bare-metal-gpio-example)
9. [Gotchas & Lessons Learned](#gotchas--lessons-learned)
10. [License](#license)

---

## Hardware Overview

| Component | Role |
|---|---|
| STM32F103C8T6 | 32-bit ARM Cortex-M3 microcontroller ("Bluepill") |
| ST-Link V2 | SWD programmer/debugger, connects target MCU to host PC |
| Host PC | Runs STM32CubeIDE / STM32CubeProgrammer, compiles and flashes firmware |

The STM32F103C8T6 supports two independent programming paths that are easy to conflate:

- **USB DFU**, which uses the chip's built-in system bootloader (factory-programmed into a protected memory region) over the board's own USB connector — no external probe required, but it must be explicitly entered (typically via the BOOT0 pin/jumper) and does not support live debugging.
- **SWD**, which requires an external probe — an ST-Link V2 in this project — connected via a dedicated 4-pin header, and supports both flashing *and* full live debugging (breakpoints, register/memory inspection, single-stepping).

This project uses SWD exclusively. The ST-Link V2 translates USB commands from the host PC into the Serial Wire Debug protocol the Cortex-M3 core understands natively.

---

## The ARM Cortex-M3 Core

The STM32F103 is built around an **ARM Cortex-M3**, a 32-bit RISC processor core licensed from ARM Holdings and implemented by STMicroelectronics as part of the STM32F1 family. Understanding the core is essential to understanding why register-level programming works the way it does.

### Architecture Fundamentals

- **ISA**: ARMv7-M (Thumb-2 instruction set only — the classic 32-bit ARM instruction set is *not* available on Cortex-M; every instruction is Thumb or Thumb-2, giving good code density)
- **Pipeline**: 3-stage pipeline (fetch, decode, execute)
- **Registers**: 13 general-purpose 32-bit registers (R0–R12), plus:
  - **R13 (SP)** — Stack Pointer (banked: Main Stack Pointer / Process Stack Pointer)
  - **R14 (LR)** — Link Register, holds return address on function calls
  - **R15 (PC)** — Program Counter
  - Special registers: **PSR** (Program Status Register), **PRIMASK**, **FAULTMASK**, **BASEPRI**, **CONTROL**
- **Memory model**: Von Neumann — unified address space for code and data (unlike Harvard-architecture cores), which is why flash, RAM, and peripherals all sit in one flat 4 GB address map
- **Bus architecture**: Multiple AMBA AHB/APB buses (ICode, DCode, System bus) allowing simultaneous instruction fetch and data access — this is why the linker script separates `.text` (flash) from `.data`/`.bss` (RAM), even though addressing is unified

### Interrupts and the NVIC

The Cortex-M3 integrates a **Nested Vectored Interrupt Controller (NVIC)** directly into the core (not a separate peripheral, as on older architectures). Key properties:

- Supports up to 240 interrupt sources (STM32F103 implements a subset — see the interrupt vector table in the startup file)
- Automatic state save/restore on interrupt entry/exit (hardware pushes R0–R3, R12, LR, PC, PSR onto the stack automatically — this is why Cortex-M interrupt handlers can be plain C functions with no special calling convention)
- Configurable priority levels
- Tail-chaining and late-arrival optimizations to minimize interrupt latency

### Memory Map (relevant to this project)

The Cortex-M3's 4 GB address space is divided into fixed regions by ARM's architecture specification:

| Address Range | Region |
|---|---|
| `0x0000 0000` – `0x1FFF FFFF` | Code (Flash, or aliased boot memory) |
| `0x2000 0000` – `0x3FFF FFFF` | SRAM |
| `0x4000 0000` – `0x5FFF FFFF` | Peripherals (memory-mapped registers — GPIO, RCC, timers, etc.) |
| `0xE000 0000` – `0xE00F FFFF` | Cortex-M3 internal peripherals (NVIC, SysTick, debug) |

This is *the* key concept behind bare-metal programming: every peripheral register (`RCC->APB2ENR`, `GPIOC->CRH`, etc.) is nothing more than a specific 32-bit memory address in the `0x4000 0000` region. Reading/writing a peripheral register is literally a memory read/write instruction — there is no special "I/O instruction" the way there is on x86. This is why the example code in this repo can configure hardware using nothing but `#define`d pointers to fixed addresses.

### SysTick

A 24-bit down-counting timer built into every Cortex-M core (not STM32-specific), intended for RTOS tick generation but equally usable as a general-purpose delay/timing source. Located at a fixed address (`0xE000E010`) regardless of vendor, since it's part of the ARM core itself rather than ST's peripheral set.

---

## STM32F103C8T6 Microcontroller

| Spec | Value |
|---|---|
| Core | ARM Cortex-M3, 32-bit |
| Max clock | 72 MHz |
| Flash | 64 KB |
| SRAM | 20 KB |
| Operating voltage | 2.0 V – 3.6 V |
| Package | LQFP48 |
| GPIO pins | 37 (ports A, B, C, partial D) |
| Timers | 4× general-purpose 16-bit, 1× advanced-control, 2× watchdog |
| Communication peripherals | 2× SPI, 2× I²C, 3× USART, 1× USB 2.0 FS, 1× CAN |
| ADC | 2× 12-bit, up to 16 channels, 1 µs conversion |
| Family | STM32F1 "Performance line" (medium-density device — confirmed via Device ID `0x410` during ST-Link connection) |

The "Bluepill" nickname refers to the widely-cloned blue PCB development board built around this chip, popular for its low cost and STM32 access despite the barebones design.

### Clock Tree (Simplified)

- **HSE** (High-Speed External): typically an 8 MHz crystal on the board
- **PLL**: multiplies HSE up to the system clock (commonly ×9 → 72 MHz)
- **SYSCLK**: feeds AHB prescaler → **HCLK** (core, memory, DMA)
- **APB1** (max 36 MHz) and **APB2** (max 72 MHz) — peripheral buses, each gated independently via `RCC->APB1ENR` / `RCC->APB2ENR`

This is why **every** peripheral (including GPIO) requires an explicit clock-enable step before its registers respond — the peripheral's clock domain is disabled by default at reset to save power. With the clock gated off, the peripheral's internal logic isn't running, so register writes cannot be reliably guaranteed to take effect and reads do not reflect real peripheral state; behavior in this state should be treated as unreliable/undefined rather than assumed to be silently and safely dropped. In practice this means: always enable a peripheral's clock in `RCC` *before* touching any of its other registers.

---

## Board Specifics — WeAct Bluepill Plus

This repository specifically documents the **WeAct Studio Bluepill Plus**, an improved clone over the original/generic Bluepill design. Differences worth noting if you're following along with a different Bluepill variant:

- **USB-C connector** (instead of the original's Micro-USB) — used purely for power and USB device communication (CDC/DFU), completely separate from the SWD programming header
- **Correct USB pull-up resistor** on PA12 — the original cheap Bluepill clones had a well-documented hardware bug here (R10 pull-up wired to the wrong voltage rail), which broke proper USB enumeration. WeAct's board fixes this, so USB DFU flashing is also a viable option here, not just SWD
- **Onboard user LED wired to PB2** — **not PC13** as on the vast majority of Bluepill tutorials and the "classic" reference design. This tripped up the first blink attempt in this repo's development and is worth flagging prominently for anyone using this exact variant
- **Separate PWR (red) and user LED (blue, PB2)** — the red LED lights whenever the board has power, independent of firmware; the blue LED is fully user-controllable GPIO
- **BOOT0 / BOOT1 jumpers/buttons** near the USB-C port, used to select boot mode (flash vs. system bootloader) — not needed for SWD flashing, only relevant for USB DFU

---

## ST-Link V2 Programmer

| Spec | Value |
|---|---|
| Protocols supported | SWD (ARM Cortex-M targets), SWIM (STM8 targets) |
| Host interface | USB 2.0 Full Speed |
| SWD pins used | SWCLK, SWDIO, GND, (+ optional 3.3V/5V for target power) |
| SWIM pins used | SWIM, RESET, GND, (+ optional power) |

SWD (Serial Wire Debug) is ARM's 2-wire alternative to full JTAG, designed specifically for space-constrained microcontroller debugging. It carries both programming (flash write/erase) and live debug (breakpoints, register/memory inspection, single-stepping) over the same two data lines (SWCLK + SWDIO), which is why STM32CubeIDE can both flash *and* debug through the identical physical connection used in this project.

The unit used here reports as ST-Link firmware `V2J37S7` and connects via STM32CubeProgrammer's GDB server component bundled with STM32CubeIDE.

---

## Wiring / SWD Connection

| ST-Link V2 Pin | Bluepill Pin | Purpose |
|---|---|---|
| SWCLK | SWCLK | Serial Wire Clock |
| SWDIO (DIO) | SWDIO | Serial Wire Data I/O |
| GND | GND | Common ground reference |
| 3.3V | 3.3V | Target power (optional — omit if board is powered independently via USB-C) |

**Note:** Only 4 wires are required for SWD. This project ran with the ST-Link supplying power directly via its 3.3V pin, so no separate USB-C power connection was needed during programming/debugging sessions — a single 4-wire harness handled both power and communication.

Do not connect the ST-Link's 3.3V pin *and* an independent USB-C power source to the board simultaneously, to avoid two supplies fighting on the same rail.

---

## Toolchain

- **STM32CubeIDE** (v1.x, workspace format 2.2.0) — Eclipse-based IDE bundling `arm-none-eabi-gcc`, `arm-none-eabi-gdb`, and the ST-Link GDB server
- **STM32CubeProgrammer** (v2.23.0) — standalone flashing/verification tool, also invoked internally by CubeIDE's Run/Debug actions
- **Project type**: STM32CubeIDE *Empty Project* — deliberately chosen over the CubeMX-generated HAL project template to avoid auto-inserted peripheral initialization code, keeping the example genuinely bare-metal

Compiler invocation (as generated by CubeIDE's build system) targets:
```
-mcpu=cortex-m3 -mthumb -mfloat-abi=soft
```
confirming Thumb-2-only code generation with software floating point (the F103 has no FPU).

---

## Bare-Metal GPIO Example

`src/main.c` in this repository blinks the onboard user LED (PB2) using **direct memory-mapped register access only** — no CMSIS device headers, no HAL, no `stm32f1xx.h`. Every register is a hand-written `#define` pointing at the exact address from the STM32F103 reference manual (RM0008).

### Why This Approach

An Empty Project in STM32CubeIDE 2.2.0 does not ship CMSIS device headers by default, so rather than sourcing/adding them, this project defines registers directly:

```c
#define RCC_BASE    0x40021000UL
#define RCC_APB2ENR (*(volatile uint32_t *)(RCC_BASE + 0x18))

#define GPIOB_BASE  0x40010C00UL
#define GPIOB_CRL   (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_ODR   (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
```

This makes explicit what CMSIS/HAL normally hides: every peripheral register is just a `volatile uint32_t` at a fixed memory address, and configuring hardware is nothing more than reading and writing plain memory.

### Register Summary Used

| Register | Address | Purpose |
|---|---|---|
| `RCC_APB2ENR` | `0x4002 1018` | Peripheral clock enable (bit 3 = GPIOB clock) |
| `GPIOB_CRL` | `0x4001 0C00` | Pin mode/config for GPIOB pins 0–7 (4 bits per pin) |
| `GPIOB_ODR` | `0x4001 0C0C` | Output data register — one bit per pin, direct level control |

Bit-field layout for `CRL`, pin *n* (0 ≤ n ≤ 7), occupies bits `[4n+3 : 4n]`:

- `MODE[1:0]` — `00` = input, `01`/`10`/`11` = output at increasing max speed (2/10/50 MHz)
- `CNF[1:0]` — meaning depends on MODE; for output mode, `00` = push-pull, `01` = open-drain

See `src/main.c` for the full annotated implementation and `docs/setup.md` for the complete build/flash walkthrough and console output.

---

## Gotchas & Lessons Learned

A few real issues hit during development, documented here to save others the same debugging time:

1. **PB2, not PC13** — this specific WeAct Bluepill Plus variant routes the user LED to PB2. The overwhelming majority of Bluepill tutorials online assume PC13 (the classic/generic Bluepill layout). Always check your board's silkscreen before assuming pin placement.
2. **No CMSIS headers in Empty Project** — `stm32f103xb.h` and friends are not included by default when creating an STM32CubeIDE *Empty Project* (as opposed to a full HAL-generated project). Either add the CMSIS device pack's include path manually, or define registers by hand as done here.
3. **Stale builds** — CubeIDE's Incremental Build can silently skip recompilation if the source file wasn't saved before building, producing an identical binary with no compiler invocation shown in the console. If a build finishes suspiciously fast with no `arm-none-eabi-gcc "../Src/main.c"...` line, save the file and do **Project → Clean** before rebuilding.
4. **PC13 is typically active-low** on classic Bluepill boards (LED on when the pin is driven low) — worth checking polarity on any given board before assuming a "blink" that appears static is actually broken firmware.

---

## License

Code in `src/` is original and released under the MIT License (see `LICENSE`).

STM32CubeIDE, STM32CubeProgrammer, and any STMicroelectronics-provided tooling referenced or shown in screenshots are the property of STMicroelectronics and are used here strictly for educational/documentation purposes; no ST source files are redistributed in this repository.

# Bare-Metal GPIO Blink Using SysTick

This program demonstrates how to control an STM32 GPIO pin and implement a millisecond delay using the **SysTick timer**, without using STM32 HAL, LL libraries, or vendor abstraction layers.

The program:

1. Enables the clock for GPIOB.
2. Configures **PB2** as a 2 MHz push-pull output.
3. Configures the ARM Cortex-M SysTick timer to generate an interrupt every **1 ms**.
4. Increments a millisecond counter inside the SysTick interrupt handler.
5. Toggles PB2 every 500 ms.

The result is a square-wave signal on **PB2** with approximately a **1 Hz complete cycle** (500 ms HIGH + 500 ms LOW).

> **Important:** This code assumes an **8 MHz system clock** and a correctly configured startup/vector-table file that maps the SysTick exception to `SysTick_Handler()`.

---

## Connections 


```
ST-LINK          STM32F103C8T6
───────────────────────────────
3.3V      ──────  3.3V
GND       ──────  GND
SWDIO     ──────  PA13
SWCLK     ──────  PA14
NRST      ──────  NRST

```
> **Important:** Do not connect the ST-LINK's 5V output to the Blue Pill's 3.3V pin.





---

## 1. Including `<stdint.h>`

```c
#include <stdint.h>
```

This provides fixed-width integer types such as:

```c
uint32_t
```

`uint32_t` represents an **unsigned 32-bit integer**, which is useful when working directly with MCU registers because the STM32 peripheral registers are generally 32 bits wide.

---

## 2. RCC Register

```c
#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018UL)
```

The **RCC (Reset and Clock Control)** peripheral controls clocks supplied to different peripherals.

For the STM32F103, address:

```text
0x40021018
```

corresponds to the **APB2 Peripheral Clock Enable Register (`RCC_APB2ENR`)**.

### Why `volatile`?

The register represents actual hardware and can change independently of normal program execution.

Therefore:

```c
volatile uint32_t
```

tells the compiler:

> Every access represents an actual hardware register access. Do not optimize it away.

### Why the pointer cast?

```c
(uint32_t *)0x40021018UL
```

treats the numerical address as a pointer to a 32-bit value.

The `*` dereferences that address:

```c
*(volatile uint32_t *)0x40021018UL
```

This allows `RCC_APB2ENR` to be used like a normal C variable even though it actually represents a hardware register.

---

# 3. GPIOB Registers

The program accesses two GPIOB registers:

```c
#define GPIOB_CRL   (*(volatile uint32_t *)0x40010C00UL)
#define GPIOB_ODR   (*(volatile uint32_t *)0x40010C0CUL)
```

### GPIOB_CRL

`GPIOB_CRL` is the **GPIO Port B Configuration Register Low**.

It controls:

```text
PB0 → PB7
```

Each GPIO pin uses 4 configuration bits.

For PB2:

```text
PB2 → bits [11:8]
```

### GPIOB_ODR

`GPIOB_ODR` is the **Output Data Register**.

Each bit corresponds to one GPIO pin:

```text
Bit 0  → PB0
Bit 1  → PB1
Bit 2  → PB2
...
Bit 15 → PB15
```

Therefore:

```c
(1 << 2)
```

produces a value that selects PB2.

---

# 4. SysTick Registers

The ARM Cortex-M3 processor inside the STM32F103 contains a built-in timer called **SysTick**.

This program accesses three SysTick registers:

```c
#define SYST_CSR    (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR    (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR    (*(volatile uint32_t *)0xE000E018UL)
```

### SysTick Control and Status Register

```text
0xE000E010
```

```c
SYST_CSR
```

Controls the SysTick timer.

### SysTick Reload Value Register

```text
0xE000E014
```

```c
SYST_RVR
```

Contains the value from which SysTick counts down.

### SysTick Current Value Register

```text
0xE000E018
```

```c
SYST_CVR
```

Contains the current counter value.

---

# 5. System Clock

```c
#define SYSCLK_HZ 8000000UL
```

The program assumes the CPU is running at:

```text
8,000,000 Hz = 8 MHz
```

This is important because SysTick needs the processor clock frequency to generate an accurate time interval.

---

# 6. Millisecond Counter

```c
volatile uint32_t msTicks = 0;
```

`msTicks` stores the number of milliseconds that have elapsed since the program started.

It is declared `volatile` because it is modified inside an **interrupt handler** while the main program is also reading it.

Without `volatile`, the compiler could incorrectly assume that the value does not change unexpectedly.

---

# 7. SysTick Interrupt Handler

```c
void SysTick_Handler(void)
{
    msTicks++;
}
```

This function is automatically called every time the SysTick timer generates an interrupt.

Every interrupt represents approximately:

```text
1 ms
```

Therefore:

```text
1 interrupt    → 1 ms
1000 interrupts → 1 second
```

The statement:

```c
msTicks++;
```

keeps track of elapsed milliseconds.

For example:

```text
msTicks = 0      → startup
msTicks = 1      → 1 ms
msTicks = 100    → 100 ms
msTicks = 1000   → 1 second
```

---

# 8. Implementing `delay_ms()`

```c
void delay_ms(uint32_t ms)
{
    uint32_t start = msTicks;

    while ((msTicks - start) < ms);
}
```

This function implements a **busy-wait delay** using the millisecond counter maintained by SysTick.

Suppose:

```c
delay_ms(500);
```

is called when:

```text
msTicks = 1000
```

The function stores:

```c
start = 1000;
```

It then continuously checks:

```c
msTicks - start
```

As the SysTick interrupt continues to execute:

```text
1000 - 1000 = 0 ms
1001 - 1000 = 1 ms
1002 - 1000 = 2 ms
...
1500 - 1000 = 500 ms
```

When 500 ms have elapsed:

```c
(msTicks - start) < 500
```

becomes false and the function returns.

### Why calculate the difference?

Using:

```c
msTicks - start
```

also makes the delay robust against the eventual **32-bit counter overflow**, because unsigned integer subtraction wraps around predictably.

> **Note:** The CPU is still busy-waiting during this delay. SysTick is interrupt-driven, but the main thread is not doing useful work while waiting.

---

# 9. GPIO Initialization

```c
void GPIO_Init(void)
{
    /* Enable GPIOB clock */
    RCC_APB2ENR |= (1 << 3);

    /* PB2 = output push-pull, 2 MHz */
    GPIOB_CRL &= ~(0xF << 8);
    GPIOB_CRL |=  (0x2 << 8);
}
```

## Enabling the GPIOB clock

```c
RCC_APB2ENR |= (1 << 3);
```

Bit 3 of `RCC_APB2ENR` controls the clock for GPIOB.

Therefore:

```c
(1 << 3)
```

sets bit 3:

```text
0000 0000 0000 1000
```

The operation:

```c
RCC_APB2ENR |= (1 << 3);
```

changes the GPIOB clock from disabled to enabled.

The GPIO peripheral must have its clock enabled before its registers can be used reliably.

---

# 10. Configuring PB2

```c
GPIOB_CRL &= ~(0xF << 8);
GPIOB_CRL |=  (0x2 << 8);
```

The STM32F1 GPIO configuration system uses **4 bits per pin**.

For PB2:

```text
PB2 → bits [11:8]
```

## First operation — clear the configuration

```c
GPIOB_CRL &= ~(0xF << 8);
```

This clears:

```text
bits 11, 10, 9, 8
```

without modifying the configuration of the other GPIO pins.

## Second operation — set the desired configuration

```c
GPIOB_CRL |= (0x2 << 8);
```

For STM32F1 GPIOs, the 4-bit configuration is:

```text
MODE[1:0] = 10
CNF[1:0]  = 00
```

This means:

```text
MODE = 10 → Output mode, maximum speed 2 MHz
CNF  = 00 → General-purpose push-pull
```

Therefore PB2 becomes:

```text
PB2
 ↓
General-purpose output
 ↓
Push-pull
 ↓
2 MHz maximum output speed
```

---

# 11. SysTick Initialization

```c
void SysTick_Init(void)
{
    SYST_RVR = (SYSCLK_HZ / 1000) - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x07;
}
```

The goal is to generate one SysTick interrupt every **1 millisecond**.

The CPU clock is:

```text
8 MHz
```

which means:

```text
8,000,000 clock cycles / second
```

We want:

```text
1,000 interrupts / second
```

Therefore:

```text
8,000,000 / 1,000 = 8,000 clock cycles
```

The reload value is:

```c
SYST_RVR = (SYSCLK_HZ / 1000) - 1;
```

giving:

```text
8,000 - 1 = 7,999
```

So SysTick counts down from approximately:

```text
7999 → 0
```

and then reloads.

This produces approximately:

```text
1 interrupt / millisecond
```

---

# 12. Clearing the Current Counter

```c
SYST_CVR = 0;
```

Writing zero to the current-value register clears the current SysTick count.

This ensures that the timer begins from a known state after configuration.

---

# 13. Enabling SysTick

```c
SYST_CSR = 0x07;
```

`0x07` in binary is:

```text
0000 0111
```

The important SysTick control bits are:

| Bit | Name | Value | Purpose |
|---:|---|---:|---|
| 0 | ENABLE | 1 | Enable SysTick |
| 1 | TICKINT | 1 | Enable SysTick interrupt |
| 2 | CLKSOURCE | 1 | Use processor clock |

Therefore:

```c
SYST_CSR = 0x07;
```

means:

> Start SysTick, generate interrupts, and use the processor clock as the timer source.

---

# 14. `main()`

```c
int main(void)
{
    GPIO_Init();
    SysTick_Init();

    while (1)
    {
        GPIOB_ODR ^= (1 << 2);
        delay_ms(500);
    }
}
```

The program first initializes:

```text
GPIOB
 ↓
PB2 configured as output
```

and then:

```text
SysTick
 ↓
1 ms interrupt
```

After initialization, the program enters an infinite loop.

---

# 15. Toggling PB2

```c
GPIOB_ODR ^= (1 << 2);
```

The `^=` operator performs an XOR operation.

If PB2 is currently:

```text
0 → LOW
```

then:

```text
0 XOR 1 = 1
```

so PB2 becomes HIGH.

If PB2 is:

```text
1 → HIGH
```

then:

```text
1 XOR 1 = 0
```

so PB2 becomes LOW.

Therefore every execution of this line changes the state of PB2:

```text
LOW
 ↓
HIGH
 ↓
LOW
 ↓
HIGH
 ↓
...
```

---

# 16. 500 ms Delay

```c
delay_ms(500);
```

After toggling PB2, the processor waits until 500 milliseconds have elapsed.

Therefore the sequence becomes:

```text
PB2 LOW
   ↓
toggle
   ↓
PB2 HIGH
   ↓
wait 500 ms
   ↓
toggle
   ↓
PB2 LOW
   ↓
wait 500 ms
   ↓
repeat
```

This gives:

```text
HIGH = 500 ms
LOW  = 500 ms
```

Therefore the total period is:

```text
T = 500 ms + 500 ms
  = 1000 ms
  = 1 second
```

The output frequency is approximately:

```text
f = 1 / T
  = 1 / 1
  = 1 Hz
```

The duty cycle is approximately:

```text
50%
```

---

# Complete Execution Flow

```text
                MCU RESET
                    │
                    ▼
                 main()
                    │
          ┌─────────┴─────────┐
          ▼                   ▼
      GPIO_Init()        SysTick_Init()
          │                   │
          ▼                   ▼
   Enable GPIOB clock    Configure 1 ms tick
          │                   │
          ▼                   ▼
    Configure PB2       Enable SysTick IRQ
          │                   │
          └─────────┬─────────┘
                    ▼
               while(1)
                    │
                    ▼
             Toggle PB2
                    │
                    ▼
              delay_ms(500)
                    │
                    ▼
          SysTick interrupt
                    │
                    ▼
             msTicks++
                    │
                    ▼
          500 ms elapsed?
             │          │
            NO         YES
             │          │
             └──────────┘
                    │
                    ▼
             Toggle PB2
                    │
                    ▼
                  repeat
```

---

# Register-Level Summary

| Peripheral | Register | Address | Purpose |
|---|---|---:|---|
| RCC | `RCC_APB2ENR` | `0x40021018` | Enable GPIOB clock |
| GPIOB | `GPIOB_CRL` | `0x40010C00` | Configure PB2 |
| GPIOB | `GPIOB_ODR` | `0x40010C0C` | Set/toggle PB2 |
| SysTick | `SYST_CSR` | `0xE000E010` | Configure/enable SysTick |
| SysTick | `SYST_RVR` | `0xE000E014` | Set reload value |
| SysTick | `SYST_CVR` | `0xE000E018` | Current counter value |

---

# Hardware Requirements

- STM32F103 MCU, such as an STM32F103C8T6 Blue Pill
- LED + suitable current-limiting resistor, if using an LED
- Oscilloscope or logic analyzer, if measuring PB2 directly
- MCU clock configured to **8 MHz**
- Proper startup/vector-table code providing `SysTick_Handler()`

---

# What This Example Demonstrates

This small program demonstrates several fundamental bare-metal embedded concepts:

- Memory-mapped peripheral registers
- RCC peripheral clock control
- STM32F1 GPIO configuration
- GPIO output control
- Bitwise operations
- ARM Cortex-M SysTick
- Interrupt-driven timekeeping
- `volatile` variables
- Software delays without HAL
- Direct hardware manipulation

## The Core Idea

The most important concept is that there is no abstraction layer hiding the hardware.

For example:

```c
GPIOB_ODR ^= (1 << 2);
```

does not call a GPIO library function.

Instead, the program directly accesses the memory address assigned to the GPIOB Output Data Register. The STM32 hardware interprets that memory access as a command to change the physical state of PB2.

That is the essence of **bare-metal embedded programming**: the C code maps directly onto the MCU's hardware registers and peripherals.

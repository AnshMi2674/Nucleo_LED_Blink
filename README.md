# Nucleo_LED_Blink
Over-engineered LED Blinker
# STM32F103RB Bare-Metal LED Blinker with USART Logging

> A complete bare-metal embedded systems project for the STMicroelectronics NUCLEO-F103RB development board, written entirely from scratch without HAL or any abstraction libraries. Every register value was derived directly from the reference manual and datasheet.

---

## Project Overview

This project implements a button-controlled LED state machine on the STM32F103RBT6 microcontroller with real-time USART serial logging. The system cycles through three states on each button press:

```
State 0 → LED OFF
State 1 → LED ON  
State 2 → LED BLINK (500ms non-blocking toggle)
```

All state transitions are reported over USART2 at 115200 baud to a serial terminal, and the system accepts character input from the terminal which is echoed back.

---

## Hardware

| Component | Details |
|---|---|
| Board | STMicroelectronics NUCLEO-F103RB |
| MCU | STM32F103RBT6 (ARM Cortex-M3, 72MHz, 128KB Flash, 20KB RAM) |
| LED | LD2 (Green) on PA5 — active HIGH |
| Button | B1 (USER) on PC13 — active LOW, pulled HIGH internally |
| Serial | USART2 via ST-LINK Virtual COM Port (PA2=TX, PA3=RX) |

---

## Features

- **Bare-metal register programming** — no HAL, no CubeMX, no abstraction layers
- **PLL clock configuration** — HSE crystal → PLL × 9 → 72MHz SYSCLK
- **Non-blocking SysTick timer** — 1ms interrupt-driven tick counter
- **EXTI interrupt-driven button** — falling edge detection via AFIO → EXTI → NVIC chain
- **Non-blocking LED blink** — timer comparison, no `delay_ms()` blocking calls
- **Button debounce** — 20ms IDR verification after interrupt
- **USART2 with interrupt-driven RX** — RXNE interrupt, non-blocking receive
- **State machine architecture** — clean enum-based switch-case design
- **Race condition handling** — volatile shared variables with local copy pattern

---

## Documents Used

Every line of code was derived from these official documents:

| Document | Purpose |
|---|---|
| **RM0008** — STM32F103xx Reference Manual | GPIO, RCC, EXTI, USART, SysTick registers |
| **DS5319** — STM32F103RB Datasheet | Pin definitions, density category, electrical specs |
| **UM1724** — NUCLEO-F103RB User Manual | LED/button pin assignments, virtual COM port wiring |
| **PM0056** — Cortex-M3 Programming Manual | SysTick registers, NVIC, vector table |

---

## Architecture

### Clock Tree

```
HSE (8MHz crystal on Nucleo board)
        ↓
     [PLL] × 9
        ↓
  SYSCLK = 72MHz
        ↓
  AHB Prescaler (/1) → HCLK = 72MHz
       ↙              ↘
APB1 (/2)           APB2 (/1)
PCLK1 = 36MHz       PCLK2 = 72MHz
(USART2, TIM2-4)    (GPIO, USART1, TIM1)
```

### Interrupt Chain (Button)

```
PC13 voltage falls (button pressed)
        ↓
AFIO_EXTICR4 routes PC13 → EXTI line 13
        ↓
EXTI_FTSR detects falling edge
EXTI_IMR unmasks line 13
        ↓
NVIC_ISER1 bit 8 (IRQ40) passes to CPU
        ↓
EXTI15_10_IRQHandler() fires
btn_event = 1, EXTI_PR cleared
        ↓
Main loop detects btn_event
20ms debounce via GPIOC_IDR + get_tick()
State machine transitions
```

### Interrupt Chain (USART RX)

```
Byte arrives on PA3
        ↓
USART2 hardware validates frame (oversampling × 16)
Sets RXNE flag
        ↓
RXNEIE = 1 → interrupt request generated
NVIC_ISER1 bit 6 (IRQ38) passes to CPU
        ↓
USART2_IRQHandler() fires
rec_data = USART2_DR (reading DR clears RXNE)
        ↓
Main loop processes rec_data with local copy
```

---

## Problems Encountered and Solutions

This section documents every significant problem encountered during development, the reasoning process used to diagnose each one, and the solution derived.

---

### Problem 1 — Understanding the Reference Manual Structure

**What happened:**  
RM0008 covers STM32F101xx, STM32F102xx, STM32F103xx, STM32F105xx, and STM32F107xx in a single 1136-page document. Sections were qualified with terms like "high-density devices" and "connectivity line devices" which caused confusion about which sections applied to the STM32F103RBT6.

**Investigation:**  
The part number itself encodes the device category. The letter after the pin count letter indicates Flash size:

```
STM32F103 R B T6
              ↑
              B = 128KB Flash = Medium-density
```

Medium-density applies to 64KB and 128KB Flash variants. High-density applies to 256–512KB. The F103xx family spans all density tiers — the RM applies to all of them but individual sections are qualified.

**Solution:**  
Always cross-reference RM0008 with DS5319. The datasheet confirms the exact specifications for the specific part number (Flash, RAM, package, peripherals). The RM is filtered by density category throughout reading.

**Lesson:**  
The RM is not a single-chip document. It is a family document. The developer's job is to know which chip they have and filter accordingly.

---

### Problem 2 — RCC Clock Enable Requirement

**What happened:**  
Coming from Arduino, `pinMode()` secretly enables peripheral clocks internally. In bare-metal STM32, every peripheral clock is OFF by default to save power. Writing to GPIO registers before enabling the clock silently does nothing.

**Investigation:**  
This was not found in the GPIO chapter. It was found in the **system architecture section** at the beginning of RM0008, which states that all peripherals on APB1, APB2, and AHB buses require their clock to be enabled via RCC before use. This is a universal rule that applies to every peripheral in the entire chip.

**Solution:**  
Enable the relevant clock in `RCC_APB1ENR` or `RCC_APB2ENR` before configuring or using any peripheral.

```c
RCC_APB2ENR |= (1 << 2);   // IOPAEN — enable GPIOA clock
RCC_APB2ENR |= (1 << 4);   // IOPCEN — enable GPIOC clock
```

**Lesson:**  
Always read the system architecture section of any new MCU reference manual before touching individual peripheral chapters. It contains universal rules that apply to everything else in the document.

---

### Problem 3 — The HardFault Before main() — SystemInit

**What happened:**  
After flashing the first working code, the STM32CubeIDE debugger immediately terminated with:

```
<terminated> Nucleo_Blinker Debug [STM32 C/C++ Application]
  <terminated, exit value: 0> arm-none-eabi-gdb
  <terminated, exit value: 0> ST-LINK (ST-LINK GDB server)
```

The editor jumped to `startup_stm32f103rbtx.s` and execution was stuck in `Default_Handler` — an infinite loop. Nothing in main() had executed.

**Investigation:**  
Reading the startup assembly file revealed this sequence before main() is called:

```asm
Reset_Handler:
    ldr  r0, =_estack
    mov  sp, r0
    bl   SystemInit     ← called BEFORE main()
    ...
    bl   main
```

The startup file calls `SystemInit` before copying initialized data to RAM and before calling main(). At the bottom of the startup file:

```asm
.weak SystemInit
```

The `.weak` directive means: use a default implementation unless the developer provides one. Since no `SystemInit` was defined in the project, the linker used a default weak version from the C runtime library. That default version attempted to configure clocks using ST HAL assumptions — conflicting with the bare-metal register setup — causing a HardFault before a single line of user code executed.

**Solution:**  
Add an empty `SystemInit` to main.c. This overrides the weak default with a strong definition that does nothing, allowing the startup sequence to complete cleanly. Clock configuration is then handled explicitly in `pll_init()`.

```c
void SystemInit(void) {
    // intentionally empty
    // clock configuration done manually in pll_init()
}
```

**Lesson:**  
In every bare-metal STM32 project without HAL, `SystemInit` must be explicitly defined. The startup file calls it before main() and the weak default is not safe for bare-metal use. This is bare-metal project checklist item #1.

---

### Problem 4 — Flash Wait States for 72MHz

**What happened:**  
Configuring the PLL to 72MHz without touching Flash wait states would have caused the CPU to read garbage instructions from Flash and crash immediately after the clock switch.

**Investigation:**  
Flash memory has a physical read speed limit. At higher CPU frequencies the CPU requests instructions faster than Flash can deliver them. RM0008 Flash programming section specifies:

```
0–24 MHz  → 0 wait states (LATENCY = 000)
24–48 MHz → 1 wait state  (LATENCY = 001)
48–72 MHz → 2 wait states (LATENCY = 010)
```

Wait states are deliberate CPU stall cycles that give Flash time to respond. Skipping this step causes immediate silent corruption.

**Solution:**  
Set Flash wait states before switching to PLL, before the frequency actually increases:

```c
FLASH_ACR &= ~(0x7);    // clear LATENCY bits
FLASH_ACR |=  (0x2);    // set 2 wait states
```

**Lesson:**  
Flash wait states must be configured before increasing CPU frequency, not after. The window between switching the clock source and the CPU running at full speed is when corruption occurs if wait states are wrong.

---

### Problem 5 — ODR vs IDR for Button Debounce

**What happened:**  
The button appeared to never register presses in early testing. The EXTI interrupt was firing correctly (confirmed via SFR inspection in debugger) but the debounce logic in main() was not confirming presses.

**Investigation:**  
The debounce code was reading `GPIOC_ODR` instead of `GPIOC_IDR`:

```c
// WRONG — reads output data register
while(!((GPIOC_ODR >> BTN_PIN) & 1))

// CORRECT — reads input data register
while(!((GPIOC_IDR >> BTN_PIN) & 1))
```

`ODR` is what the CPU writes TO the pin. `IDR` is what the CPU reads FROM the pin — the actual voltage state. `GPIOC_ODR` bit 13 was permanently 1 (because it was set to activate the pull-up resistor), so the debounce condition was immediately false and no press was ever confirmed.

The compiler produced no error — both `GPIOC_ODR` and `GPIOC_IDR` are valid defined symbols. The bug was purely logical.

**Solution:**  
Change `GPIOC_ODR` to `GPIOC_IDR` in the debounce loop and add `GPIOC_IDR` to the register defines.

**Lesson:**  
The compiler checks syntax, not hardware logic. A wrong register address compiles perfectly and fails silently on hardware. Always verify register names against the RM register map, not just against the define list.

---

### Problem 6 — SysTick Handler Name and the Weak Symbol System

**What happened:**  
There was uncertainty about how the CPU knows to call `SysTick_Handler` — specifically where this name comes from and what happens if it is spelled incorrectly.

**Investigation:**  
Reading `startup_stm32f103rbtx.s` revealed the vector table (`g_pfnVectors`) — an array of function addresses stored at the start of Flash at `0x08000000`. Entry 15 (0-indexed) is the SysTick entry:

```asm
.word SysTick_Handler   // position 15 in vector table
```

When SysTick fires, the CPU reads address position 15 from the vector table and jumps to it. The name `SysTick_Handler` is standardized by the ARM Cortex-M3 specification in PM0056 — it is identical on every Cortex-M chip from every manufacturer.

The startup file also contains:

```asm
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler
```

If the developer does not define `SysTick_Handler` in C, the weak alias points to `Default_Handler` (infinite loop). If the developer defines it, the linker replaces the weak alias with the real function address. If the developer spells it wrong — `Systick_handler` for example — the linker silently uses the weak default and `tick_count` never increments. No error, no warning, silent failure.

**Solution:**  
Handler names must match the startup file exactly, character for character including capitals. Always verify against the vector table in the startup file before writing any interrupt handler.

**Lesson:**  
The startup file is the authoritative source for all interrupt handler names. Before writing any handler, check the startup file. A misspelled handler name is one of the most common and hardest-to-find bugs in bare-metal embedded.

---

### Problem 7 — AFIO → EXTI → NVIC Chain

**What happened:**  
The concept of needing three separate peripheral blocks (AFIO, EXTI, NVIC) to handle a single GPIO interrupt was initially unclear.

**Investigation:**  
Each block has one specific responsibility:

**AFIO** — Multiple GPIO ports share EXTI lines. Line 13 can come from PA13, PB13, PC13, or PD13. AFIO is a hardware multiplexer that selects which port's pin connects to each EXTI line. Without AFIO configuration, EXTI line 13 has no defined source.

```c
AFIO_EXTICR4 |= (0x2 << 4);   // bits 7:4 = 0010 = Port C
```

**EXTI** — Detects the edge condition (rising, falling, or both) and generates an interrupt request. Also maintains a pending register that must be manually cleared in the handler (write 1 to clear — not 0).

```c
EXTI_IMR  |= (1 << 13);   // unmask line 13
EXTI_FTSR |= (1 << 13);   // falling edge trigger
```

**NVIC** — The CPU's interrupt controller. Even with EXTI generating a request, the CPU will not respond unless NVIC has that IRQ enabled. IRQ numbers are found by counting peripheral entries in the vector table starting from WWDG = IRQ0.

```
IRQ number N → ISER register = N / 32, bit = N % 32
EXTI15_10 = IRQ40 → ISER1 bit 8
```

**Solution:**  
Configure all three in sequence. Missing any one of them results in silent failure — the interrupt simply never fires.

**Lesson:**  
GPIO interrupts on STM32 require three separate peripheral configurations. This pattern is consistent across all STM32 families. Once understood for one interrupt source, it applies to all others.

---

### Problem 8 — Button Active LOW Confusion

**What happened:**  
Initial code checked `if(btn == 1)` to detect a button press, which is the Arduino convention where buttons typically pull HIGH when pressed.

**Investigation:**  
UM1724 schematic shows B1 (USER button) is wired between PC13 and GND. No connection to VDD. The internal pull-up resistor (activated via `GPIOC_ODR` bit 13 = 1 with CNF = 10) holds the pin HIGH when the button is not pressed. Pressing the button physically connects PC13 to GND, pulling it LOW.

```
Button released → PC13 = HIGH (3.3V) → IDR bit 13 = 1
Button pressed  → PC13 = LOW  (0V)   → IDR bit 13 = 0
```

The button is active LOW — the opposite of common Arduino convention.

**Solution:**  
Detect falling edge (HIGH → LOW) as the press event. Use EXTI_FTSR (falling trigger selection register) and check for last_state = 1 AND current_state = 0.

**Lesson:**  
Always read the board user manual (UM1724) before assuming button polarity. Hardware wiring determines active level — it cannot be inferred from software alone.

---

### Problem 9 — Blocking Delay Missing Button Presses

**What happened:**  
An early version used `delay_ms(500)` inside the blink state to create the 500ms toggle period. During testing, button presses during the blink state were completely missed.

**Investigation:**  
`delay_ms(500)` using a busy-wait loop or blocking SysTick poll keeps the CPU busy for 500ms. The button check only happens once per loop iteration. With a 500ms blocking period per iteration, any button press shorter than 500ms that occurs during the delay is never seen.

This is the fundamental problem with blocking delays in event-driven systems — the CPU cannot respond to anything while blocked.

**Solution:**  
Replace blocking delay with non-blocking timer comparison using `get_tick()`:

```c
// Instead of:
delay_ms(500);

// Use:
if((get_tick() - blink_timer) >= 500) {
    blink_timer = get_tick();
    // toggle LED
}
```

The CPU checks elapsed time every loop iteration (microseconds) instead of waiting. Button events are never missed regardless of blink timing.

**Lesson:**  
Blocking delays and responsive event handling are fundamentally incompatible. In any system that needs to respond to external events, all timing must be non-blocking. `get_tick()` comparison is the standard pattern.

---

### Problem 10 — UART Receive Race Condition

**What happened:**  
When receiving characters over USART, the output was sometimes corrupted or showed wrong characters. For example, sending 'A' would correctly print "Received: A" but then immediately print "Received: " with nothing after it.

**Investigation:**  
Serial terminals send characters followed by `\r` (13) and `\n` (10) when Enter is pressed. At 115200 baud each byte arrives 0.087ms apart. The sequence is:

```
T+0.000ms  'A'  arrives → rec_data = 65
T+0.087ms  '\r' arrives → rec_data = 13  (overwrites during string TX)
T+0.174ms  '\n' arrives → rec_data = 10  (overwrites again)
```

The original code checked `rec_data != 0`, entered the if block, started transmitting "Received: " (which takes ~0.78ms at 115200 baud), and while transmitting, the `\r` interrupt fired and overwrote `rec_data`. By the time `usart2_transmit(rec_data)` executed, `rec_data` was 13 (carriage return) — invisible on terminal.

This is a **race condition** — two concurrent execution contexts (main loop and interrupt handler) accessing shared data without synchronization.

**Solution — Part 1:** Copy shared variable to local immediately:

```c
if(rec_data != 0) {
    uint8_t local_data = rec_data;  // copy atomically
    rec_data = 0;                    // clear before slow operations
    // use local_data — interrupt cannot touch it
}
```

**Solution — Part 2:** Filter non-printable characters:

```c
if(local_data >= 32 && local_data <= 126) {
    // printable ASCII only — ignores \r, \n, \t etc.
}
```

**Lesson:**  
`volatile` prevents compiler caching but does not prevent race conditions. Any shared variable modified in an interrupt and read in main code must be copied to a local variable before use. The copy should happen as early as possible, followed immediately by clearing the shared variable. For critical sections in production code, disable interrupts during the copy.

---

### Problem 11 — Register Naming Inconsistency (Compile Error)

**What happened:**  
A compile error occurred because registers were defined as `USART_SR`, `USART_DR` etc. (without "2") but used as `USART2_SR`, `USART2_DR` in the init and handler functions.

**Investigation:**  
The defines at the top of the file and the usage in functions used different naming conventions. The C preprocessor is case-sensitive and exact — `USART_SR` and `USART2_SR` are completely different identifiers.

**Solution:**  
Standardize all USART2 register defines with the "2" suffix to match usage and clearly distinguish from USART1 and USART3:

```c
#define USART2_SR   (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_DR   (*(volatile uint32_t *)(USART2_BASE + 0x04))
```

**Lesson:**  
When a project has multiple instances of the same peripheral (USART1, USART2, USART3), always include the instance number in the register define name. Ambiguous naming causes bugs that are invisible to the hardware but obvious to the compiler.

---

### Problem 12 — Baud Rate Calculation

**What happened:**  
The baud rate register (BRR) does not accept the baud rate directly. It requires a calculated value split into mantissa and fractional parts.

**Investigation:**  
RM0008 Section on USART baud rate gives the formula:

$$USARTDIV = \frac{f_{PCLK}}{16 \times BaudRate}$$

For USART2 at 115200 baud with PCLK1 = 36MHz:

$$USARTDIV = \frac{36{,}000{,}000}{16 \times 115{,}200} = \frac{36{,}000{,}000}{1{,}843{,}200} = 19.53125$$

The BRR register stores this as:
- **Mantissa** (integer part): 19 = `0x13`
- **Fraction** (decimal × 16, rounded): $0.53125 \times 16 = 8.5 \approx 9$ = `0x9`

$$BRR = (Mantissa \ll 4) \mid Fraction = (0x13 \ll 4) \mid 0x9 = 0x139$$

Verification by working backwards:

$$Actual\ baud = \frac{36{,}000{,}000}{16 \times (19 + \frac{9}{16})} = \frac{36{,}000{,}000}{16 \times 19.5625} = 115{,}014\ baud$$

$$Error = \frac{|115{,}200 - 115{,}014|}{115{,}200} = 0.16\%$$

UART tolerates up to ±2% error. 0.16% is well within tolerance.

**Solution:**
```c
USART2_BRR = 0x139;   // 115200 baud at PCLK1 = 36MHz
```

**Lesson:**  
Always verify baud rate accuracy by working backwards from the BRR value to the actual baud rate. The formula is in the USART chapter of RM0008. Different clock speeds require recalculation — the formula depends on PCLK1 for USART2/3 and PCLK2 for USART1.

---

## Register Map Summary

All registers used in this project with their addresses:

### RCC (Base: 0x40021000)

| Register | Offset | Purpose |
|---|---|---|
| RCC_CR | 0x00 | HSE/PLL enable and ready flags |
| RCC_CFGR | 0x04 | Clock source select, prescalers, PLL config |
| RCC_APB2ENR | 0x18 | Enable AFIO, GPIOA, GPIOC clocks |
| RCC_APB1ENR | 0x1C | Enable USART2 clock |

### GPIO (GPIOA Base: 0x40010800, GPIOC Base: 0x40011000)

| Register | Offset | Purpose |
|---|---|---|
| GPIOx_CRL | 0x00 | Configure pins 0–7 (MODE + CNF) |
| GPIOx_CRH | 0x04 | Configure pins 8–15 (MODE + CNF) |
| GPIOx_IDR | 0x08 | Read pin input states |
| GPIOx_ODR | 0x0C | Write pin output states / activate pull-up |
| GPIOx_BSRR | 0x10 | Atomic set (BS) / reset (BR) bits |

### AFIO (Base: 0x40010000)

| Register | Offset | Purpose |
|---|---|---|
| AFIO_EXTICR4 | 0x14 | Route GPIO pins to EXTI lines 12–15 |

### EXTI (Base: 0x40010400)

| Register | Offset | Purpose |
|---|---|---|
| EXTI_IMR | 0x00 | Interrupt mask (unmask lines) |
| EXTI_FTSR | 0x0C | Falling edge trigger select |
| EXTI_PR | 0x14 | Pending register (write 1 to clear) |

### SysTick (Base: 0xE000E010)

| Register | Address | Purpose |
|---|---|---|
| SYST_CSR | 0xE000E010 | Enable, clock source, interrupt enable |
| SYST_RVR | 0xE000E014 | Reload value (71999 for 1ms at 72MHz) |
| SYST_CVR | 0xE000E018 | Current value (write any to clear) |

### NVIC (Base: 0xE000E100)

| Register | Address | Purpose |
|---|---|---|
| NVIC_ISER1 | 0xE000E104 | Enable IRQ32–63 (EXTI15_10=IRQ40, USART2=IRQ38) |

### USART2 (Base: 0x40004400)

| Register | Offset | Purpose |
|---|---|---|
| USART2_SR | 0x00 | Status flags (TXE, TC, RXNE) |
| USART2_DR | 0x04 | Data register (read/write) |
| USART2_BRR | 0x08 | Baud rate (mantissa + fraction) |
| USART2_CR1 | 0x0C | UE, M, TE, RE, RXNEIE |
| USART2_CR2 | 0x10 | Stop bits |

---

## GPIO Configuration Reference

| Pin | Peripheral | MODE | CNF | 4-bit value | Register bits |
|---|---|---|---|---|---|
| PA2 | USART2_TX | 01 (10MHz out) | 10 (AF push-pull) | 1001 = 0x9 | CRL bits 11:8 |
| PA3 | USART2_RX | 00 (input) | 01 (floating) | 0100 = 0x4 | CRL bits 15:12 |
| PA5 | LED (LD2) | 01 (10MHz out) | 00 (GP push-pull) | 0001 = 0x1 | CRL bits 23:20 |
| PC13 | Button (B1) | 00 (input) | 10 (pull-up/down) | 1000 = 0x8 | CRH bits 23:20 |

---

## Boot Sequence

Understanding what happens before `main()` is essential for bare-metal development:

```
Power on / Reset
      ↓
CPU reads vector table at 0x08000000
Jumps to Reset_Handler (startup_stm32f103rbtx.s)
      ↓
Set stack pointer (SP = _estack from linker script)
      ↓
bl SystemInit  ← must exist, empty in bare-metal
      ↓
Copy initialized data: Flash → RAM (_sidata → _sdata)
      ↓
Zero BSS segment in RAM (_sbss to _ebss)
      ↓
bl __libc_init_array  (C++ static constructors)
      ↓
bl main()  ← your code starts here
      ↓
LoopForever (if main returns — should never happen)
```

---

## Project Structure

```
Nucleo_Blinker/
├── Src/
│   └── main.c              ← all application code
├── Startup/
│   └── startup_stm32f103rbtx.s  ← vector table, Reset_Handler
├── STM32F103RBTX_FLASH.ld  ← linker script (128KB Flash, 20KB RAM)
└── README.md
```

---

## Building and Flashing

**Requirements:**
- STM32CubeIDE 2.x
- NUCLEO-F103RB connected via USB (ST-LINK port)

**Build:**
```
Project → Build Project  (Ctrl+B)
```

**Flash and Debug:**
```
Run → Debug  (F11)
Press F8 to resume execution
```

**Serial Monitor:**
Open any serial terminal (PuTTY, Arduino IDE Serial Monitor) at:
```
Port   : STMicroelectronics Virtual COM Port (check Device Manager)
Baud   : 115200
Data   : 8 bits
Parity : None
Stop   : 1 bit
```

---

## Key Concepts Learned

| Concept | Applied In |
|---|---|
| Memory-mapped I/O with `volatile` | Every register access |
| Bit masking and manipulation | All register configuration |
| Bus architecture (AHB/APB) | RCC clock enable |
| PLL clock configuration | pll_init() |
| GPIO modes (push-pull, open-drain, input) | PA2, PA3, PA5, PC13 |
| Interrupt-driven architecture | Button (EXTI) + UART RX |
| Non-blocking timing with SysTick | LED blink, debounce |
| State machine design | LED OFF/ON/BLINK |
| Race conditions and volatile | rec_data handling |
| Startup file and weak symbols | SystemInit, handler names |
| UART frame format and baud rate | USART2 configuration |
| Datasheet reading methodology | All pin/register lookups |

---

## License

This project is provided for educational purposes. Free to use, modify, and learn from.

# RM Classic — Microcontroller Interface

MCU-side firmware library for use with [RM Classic](https://github.com/NaoNaoMe/RM-Classic).

For protocol details and host-side usage, refer to the RM Classic repository.

## Quick Start (Arduino Uno R3)

1. Clone this repository or download and extract the ZIP.
2. Open `RmSample/RmSample.ino` in the Arduino IDE.
3. Upload the sketch to your Arduino Uno.
4. In RM Classic, open `RMConfiguration--RmSample.rmxml` via **File > Open > View File**.
5. Configure RM Classic with baud rate **9600 bps**, password **`0x0000FFFF`**, and address width **2 bytes**.
6. Click **Comm Open** — the version string `RmSample` confirms a successful connection.

### Demo sketch

`RmSample.ino` declares several variables that RM Classic can observe and modify at runtime.
RM Classic resolves each variable's address and size from its symbol name via a map file,
and uses that information to keep the addresses in `*.rmxml` up to date.

| Variable | Type | Description |
|----------|------|-------------|
| `isCounting` | `boolean` | Start/stop the counter from RM Classic |
| `count` | `int` | Counter incremented every 5 ms while `isCounting` is true |
| `debounceCount` | `int` | Consecutive LOW-state count of the switch |
| `isLEDOn` | `boolean` | Indicates whether the LED is on |

The sketch also echoes back any text received over the SCE channel.
Click **Terminal** in RM Classic to open the terminal view, type any text, and confirm the echo response from the MCU.

See the [SCE Channel](#sce-channel) section for details on the underlying mechanism.

### Generating the map file (Arduino IDE)

Arduino IDE can generate the map file automatically after each build
via post-build hooks in `platform.txt`, located at:

```
%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\platform.txt
```

Add the following lines, adjusting the output path to suit your environment:

```
# User post actions
recipe.hooks.postbuild.1.pattern=cmd /c if not exist "C:\Users\XX_user_XX\arduino_ide_output" mkdir "C:\Users\XX_user_XX\arduino_ide_output"
recipe.hooks.postbuild.2.pattern=cmd /c copy "{build.path}\{build.project_name}.hex" "C:\Users\XX_user_XX\arduino_ide_output\{build.project_name}.hex"
recipe.hooks.postbuild.4.pattern=cmd /c "{compiler.path}avr-nm" --demangle --print-size --size-sort --radix=x "{build.path}/{build.project_name}.elf" > "C:\Users\XX_user_XX\arduino_ide_output\{build.project_name}.nm.map"
```

After building, import the generated `*.nm.map` file into RM Classic via **File > Open > Map File**.

> **Note:** For other toolchains or IDEs, an equivalent map file must be generated manually using the appropriate tool (e.g. `arm-none-eabi-nm` for ARM targets). The required format and import steps are the same, but the method of invoking the tool after each build depends on your environment.

## SCE Channel

The Serial Communication Emulation (SCE) channel carries plain-text data alongside the rm protocol over the same UART connection.
Use the `rm_comm_print*` helpers to emit debug text from firmware; `rm_comm_read()` / `rm_comm_available()` to receive text sent from RM Classic.
SCE traffic is gated by authentication and transmitted only when no protocol frame is pending.

## Porting to Other MCUs

Four integration points connect the library to any UART peripheral:

| Function | Where to call |
|----------|---------------|
| `rm_comm_init(ver, ver_len, ms, key)` | Once at startup |
| `rm_comm_push_rx_byte(byte)` | From the UART RX interrupt (or DMA callback) |
| `rm_comm_run()` | Every `ms` milliseconds — timer tick or RTOS task |
| `rm_comm_try_transmit(&b)` / `rm_comm_get_tx_byte(&b)` | After `rm_comm_run()`, or from the UART TX interrupt |

### Polling TX (e.g. bare-metal timer ISR)

Call `rm_comm_try_transmit()` immediately after `rm_comm_run()` and drain the frame in a loop:

```c
/* Startup */
rm_comm_init((uint8_t *)version, sizeof(version), TICK_MS, PASS_KEY);

/* UART RX interrupt */
void uart_rx_isr(void) {
    rm_comm_push_rx_byte(uart_read_byte());
}

/* Periodic timer tick — every TICK_MS ms */
void timer_tick_isr(void) {
    /* application logic */
    rm_comm_run();
    uint8_t b;
    if (rm_comm_try_transmit(&b)) {
        uart_write_byte(b);
        while (rm_comm_get_tx_byte(&b))
            uart_write_byte(b);
    }
}
```

### Interrupt-driven TX (e.g. STM32 HAL UART)

Use `rm_comm_try_transmit()` to detect a pending frame and seed the TX interrupt,
then drain with `rm_comm_get_tx_byte()` inside the TX-complete callback:

```c
/* Periodic timer tick — every TICK_MS ms */
void timer_tick_isr(void) {
    /* application logic */
    rm_comm_run();
    uint8_t b;
    if (rm_comm_try_transmit(&b)) {
        HAL_UART_Transmit_IT(&huart1, &b, 1); /* seed the TX interrupt */
    }
}

/* UART TX-complete callback */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    uint8_t b;
    if (rm_comm_get_tx_byte(&b))
        HAL_UART_Transmit_IT(huart, &b, 1);
}
```

## Configuration

Platform-specific settings are in `rm_types.h`.

### Address width

Selected automatically from compiler-predefined macros:

| Macro | Address type | Default target |
|-------|-------------|----------------|
| `RM_ADDR_2BYTE` | `uint16_t` | AVR (`__AVR__`) |
| `RM_ADDR_4BYTE` | `uint32_t` | ARM and all others |

Define `RM_ADDR_2BYTE` or `RM_ADDR_4BYTE` explicitly if auto-detection does not match your toolchain.

### 64-bit variable support

`RM_SUPPORT_64BIT` enables monitoring of 8-byte variables, including `double` on targets where it is 64 bits wide.
It is defined by default; remove or comment it out to reduce code size on targets where 64-bit types are emulated or unused:

```c
/* rm_types.h — disable if 64-bit support is not needed */
// #define RM_SUPPORT_64BIT
```

## Architecture

The library is divided into two layers:

```
┌─────────────────────────────────────────┐
│  Application / timer tick               │
└──────────────┬──────────────────────────┘
               │  rm_comm_run()
┌──────────────▼──────────────────────────┐
│  rm_comm  —  Transport layer            │
│  SLIP framing · circular buffers        │
│  SCE channel · TX arbiter               │
└──────────────┬──────────────────────────┘
               │  rm_core_tick()
┌──────────────▼──────────────────────────┐
│  rm_core  —  Protocol engine            │
│  CRC-8 · instruction dispatch           │
│  log emission · keepalive               │
└─────────────────────────────────────────┘
```

| Module | Responsibility |
|--------|----------------|
| `rm_comm` | SLIP encode/decode, circular buffers, SCE channel, TX arbitration |
| `rm_core` | CRC-8 validation, command dispatch, periodic log frames, keepalive timeout |

Only `rm_comm` requires platform-specific wiring; `rm_core` is pure portable C.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
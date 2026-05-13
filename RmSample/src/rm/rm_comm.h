/*
 * rm_comm.h — Transport layer for the rm protocol.
 *
 * RmComm owns all I/O buffers and is the sole owner of the rm frame buffers
 * (rm_rx_frame_t / rm_tx_frame_t).  It handles:
 *
 *   - SLIP framing: encode outgoing frames (RmCore → wire) and decode
 *     incoming byte streams (wire → RmCore).
 *   - Inter-byte timeout: reset a stalled SLIP decoder after RM_RX_TIMEOUT_MS
 *     of silence between bytes.
 *   - Circular buffers:
 *       rx_raw  : raw bytes arriving from the ISR.
 *       rx_sce  : decoded SCE payload bytes, available to the application.
 *       tx_sce  : SCE bytes queued by the application for transmission.
 *   - TX arbiter (single path):
 *       Priority 1 — protocol frame produced by RmCore (tx_frame.ready).
 *       Priority 2 — SCE derived frame assembled from tx_sce (idle time only).
 *   - A text-output API (rm_comm_write / rm_comm_print / …) for the
 *     SerialCommunicationEmulation (SCE) channel.
 *
 * Responsibility boundary:
 *   RmComm owns SLIP and all byte-level I/O.
 *   RmCore owns protocol logic (CRC, instruction dispatch, log emission).
 *
 * Typical polling usage:
 *
 *   // In the UART receive ISR or callback:
 *   rm_comm_push_rx_byte(byte);
 *
 *   // In the main loop, called every millis_cnt ms:
 *   rm_comm_run();
 *
 *   // Drain the TX pipeline after rm_comm_run():
 *   uint8_t b;
 *   if (rm_comm_try_transmit(&b)) {
 *       uart_write(b);
 *       while (rm_comm_get_tx_byte(&b))
 *           uart_write(b);
 *   }
 *
 * Copyright 2026 Naoya Imai
 */

#ifndef RM_COMM_H
#define RM_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rm_core.h"

/* ===========================================================================
 * Circular buffer sizes (must be powers of two)
 * ===========================================================================
 */
#define RM_CIRC_RX_SIZE         16u    /* raw bytes from the transport ISR      */
#define RM_CIRC_SCE_RX_SIZE     32u    /* decoded SCE payload bytes (→ app)     */
#define RM_CIRC_SCE_TX_SIZE    128u    /* SCE bytes queued by the app (→ wire)  */

/* ===========================================================================
 * Derived frame identifiers
 * ===========================================================================
 */
#define RM_DERIVED_MARKER       0x00u   /* first byte of every derived frame    */
#define RM_DERIVED_SCE          0x01u   /* modeCode: SerialCommunicationEmulation */

/* ===========================================================================
 * Core driver API
 * ===========================================================================
 */

/**
 * rm_comm_init — initialise the communication layer.
 *
 * Must be called once before any other rm_comm function.
 *
 * @param ver      Pointer to the firmware version string.
 * @param ver_len  Length of the version string (bytes).
 * @param ms       Milliseconds elapsed between consecutive rm_comm_run() calls.
 * @param key      Passkey the host must supply in the ReadInfo frame.
 */
void rm_comm_init(uint8_t  *ver,
                  uint16_t  ver_len,
                  uint16_t  ms,
                  uint32_t  key);

/**
 * rm_comm_run — main processing tick; call once per ms interval.
 *
 * Processing order:
 *   0. SLIP inter-byte timeout.
 *   1. Drain raw RX circular buffer through the SLIP decoder.
 *   2. Route completed derived frames to the SCE RX buffer (if approved).
 *   3. Call rm_core_tick to advance the protocol state machine.
 *   4. TX arbiter: build SCE derived frame when no protocol frame is pending.
 */
void rm_comm_run(void);

/**
 * rm_comm_try_transmit — check whether a TX frame is pending.
 *
 * If a frame is ready, initialises the SLIP encoder and returns the first
 * byte (the opening END).  Call rm_comm_get_tx_byte() in a loop afterward to
 * drain the rest of the frame.
 *
 * @param out  Output: the first byte (valid only when return is true).
 * @return true if a frame is ready and *out contains a valid byte.
 */
bool rm_comm_try_transmit(uint8_t *out);

/**
 * rm_comm_get_tx_byte — pull the next SLIP-encoded byte from the TX pipeline.
 *
 * Call in a loop after rm_comm_try_transmit() until it returns false.
 * Returns false (and releases the TX buffer) after the closing END byte.
 *
 * @param out  Output: the next byte to send.
 * @return true if *out is valid; false when the frame is fully consumed.
 */
bool rm_comm_get_tx_byte(uint8_t *out);

/**
 * rm_comm_push_rx_byte — enqueue one byte received from the transport.
 *
 * Call from a UART receive ISR or a polling receive handler.
 *
 * @param byte  The received byte.
 */
void rm_comm_push_rx_byte(uint8_t byte);

/**
 * rm_comm_is_logging — return true while log emission is active.
 *
 * Reflects the is_logging flag in the protocol state.  Note that
 * authentication (is_approved) is set earlier, at ReadInfo completion.
 */
bool rm_comm_is_logging(void);

/* ===========================================================================
 * SerialCommunicationEmulation (SCE) channel API
 *
 * The SCE channel is a pseudo-serial channel gated by is_approved.
 * It carries human-readable debug text alongside the rm protocol with no CRC.
 * Garbled bytes are intentionally visible rather than silently discarded.
 *
 * TX data is low-priority: it is only sent when no protocol frame is pending.
 * ===========================================================================
 */

/**
 * rm_comm_write — enqueue one byte for transmission via the SCE channel.
 *
 * Does nothing if the session is not yet authenticated (is_approved == false).
 */
void rm_comm_write(uint8_t data);

/**
 * rm_comm_read — dequeue one byte received via the SCE channel.
 *
 * @return The dequeued byte, or 0 if the receive buffer is empty.
 */
uint8_t rm_comm_read(void);

/**
 * rm_comm_available — return the number of bytes waiting in the SCE RX buffer.
 */
uint16_t rm_comm_available(void);

/* ===========================================================================
 * SCE text helpers
 * ===========================================================================
 */

/** Send a null-terminated string. */
void rm_comm_print(const char *str);

/** Send CR+LF. */
void rm_comm_println(void);

/** Send the decimal representation of an unsigned 32-bit integer. */
void rm_comm_print_u32(uint32_t n);

/** Send the decimal representation of a signed 32-bit integer. */
void rm_comm_print_i32(int32_t n);

/** Send a floating-point number formatted to 2 decimal places. */
void rm_comm_print_float(float f);

#ifdef __cplusplus
}
#endif

#endif /* RM_COMM_H */

/* end of file */

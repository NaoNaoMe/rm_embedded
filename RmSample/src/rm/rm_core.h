/*
 * rm_core.h — Protocol engine for the rm serial communication protocol.
 *
 * RmCore implements the slave-side protocol state machine.  It operates
 * exclusively on raw decoded frames supplied by the transport layer (RmComm)
 * and has no knowledge of SLIP framing, circular buffers, or physical I/O.
 *
 * Ownership model:
 *   RmComm owns the rx/tx frame buffers (rm_rx_frame_t / rm_tx_frame_t).
 *   RmCore borrows pointers to them for the duration of rm_core_tick() only
 *   and never retains them after the call returns.
 *
 * Frame structure handled by RmCore:
 *   [ OpCode(1) | payload(0..N) | CRC-8(1) ]
 *
 * OpCode encoding:
 *   bits 7–4 : MasCnt  — host transaction counter (0x10–0xF0, step 0x10)
 *   bits 3–0 : InstrCode — instruction identifier (0x01–0x07)
 *
 * All multi-byte integers are little-endian.
 *
 * Copyright 2026 Naoya Imai
 */

#ifndef RM_CORE_H
#define RM_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rm_types.h"

/* ===========================================================================
 * Protocol state object
 *
 * Holds all session state for one rm channel.  Contains no frame buffers;
 * those are passed in at each rm_core_tick() call.
 * ===========================================================================
 */

typedef struct {
    /* --- Session flags ----------------------------------------------------- */
    bool     is_approved;       /* true after ReadInfo passkey match             */
    bool     is_logging;        /* true while periodic log emission is active    */
    bool     response_pending;  /* true while a block response is still queued   */

    /* --- Sequence counters ------------------------------------------------- */
    uint16_t mas_cnt;           /* MasCnt nibble (upper nibble) from last request */
    uint16_t slv_cnt;           /* slave sequence counter: 1–15, wraps 15→1      */

    /* --- Configuration ----------------------------------------------------- */
    uint32_t pass_key;          /* expected passkey for ReadInfo                  */
    uint16_t millis_cnt;        /* ms elapsed per rm_core_tick() invocation       */

    /* --- Timing accumulators ----------------------------------------------- */
    uint16_t log_timeout_cnt;       /* ms since last StartLog keepalive           */
    uint16_t log_interval_cnt;      /* ms accumulated toward next log frame       */
    uint16_t log_interval_period;   /* configured log emission period (ms)        */

    /* --- Data descriptors -------------------------------------------------- */
    rm_log_cfg_t log;           /* log variable configuration (SetAddr table)    */
    rm_mem_ref_t version;       /* firmware version string (returned by ReadInfo) */
    rm_mem_ref_t pending_block; /* payload for the next block response            */
} rm_proto_t;

/* ===========================================================================
 * Public API
 * ===========================================================================
 */

/**
 * rm_core_init — reset protocol state and store startup configuration.
 *
 * Must be called once before rm_core_tick().
 *
 * @param p        Protocol state object to initialise.
 * @param ver      Pointer to the firmware version string.
 * @param ver_len  Length of the version string (bytes).
 * @param ms       Milliseconds between consecutive rm_core_tick() calls.
 * @param key      Passkey the host must supply in ReadInfo.
 */
void rm_core_init(rm_proto_t *p,
                  uint8_t    *ver,
                  uint16_t    ver_len,
                  uint16_t    ms,
                  uint32_t    key);

/**
 * rm_core_tick — one protocol processing cycle.
 *
 * Called once per ms period by rm_comm_run().
 *
 * Responsibilities per tick:
 *   1. Retry a pending block response if the TX buffer was busy last tick.
 *   2. Dispatch a completed RX frame: validate CRC, decode instruction,
 *      set pending_block, build the TX response frame.
 *   3. Emit periodic log frames when the configured interval elapses.
 *   4. Maintain the keepalive timeout; stop logging on expiry.
 *
 * Frame buffer ownership: RmComm passes its own buffers as pointers.
 * RmCore writes into tx only when tx->ready is false (buffer is free).
 * RmCore clears rx->complete and rx->len after dispatch (success or fail).
 *
 * @param p   Protocol state.
 * @param rx  Inbound frame buffer. complete==true triggers dispatch.
 * @param tx  Outbound frame buffer. Written when a response is ready.
 */
void rm_core_tick(rm_proto_t    *p,
                  rm_rx_frame_t *rx,
                  rm_tx_frame_t *tx);

#ifdef __cplusplus
}
#endif

#endif /* RM_CORE_H */

/* end of file */

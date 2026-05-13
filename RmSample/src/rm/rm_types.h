/*
 * rm_types.h — Shared types and constants for the rm communication protocol.
 *
 * All platform-dependent configuration and data structures used by both
 * rm_core and rm_comm are defined here.  Neither module defines its own
 * protocol types; both include only this header for shared definitions.
 *
 * Copyright 2026 Naoya Imai
 */

#ifndef RM_TYPES_H
#define RM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ===========================================================================
 * Platform configuration
 * ===========================================================================
 *
 * RM_SUPPORT_64BIT : enable 8-byte variable support in log and write frames.
 * Address width    : selects rm_addr_t and the address-field size in frames.
 */

#define RM_SUPPORT_64BIT

#ifdef __AVR__
#  define RM_ADDR_2BYTE       /* AVR: 16-bit data pointers */
#elif defined(__arm__)
#  define RM_ADDR_4BYTE       /* ARM: 32-bit pointers      */
#else
#  define RM_ADDR_4BYTE       /* default — adjust if needed */
#endif

#ifdef RM_ADDR_4BYTE
typedef uint32_t rm_addr_t;
#  define RM_ADDR_SIZE  4u
#else
typedef uint16_t rm_addr_t;
#  define RM_ADDR_SIZE  2u
#endif

/* ===========================================================================
 * Protocol constants
 * ===========================================================================
 */

/* Maximum number of variables configurable in one SetAddr session. */
#define RM_LOG_VAR_MAX          32u

/* Maximum payload bytes in a single TX frame (32 vars × 4 bytes each). */
#define RM_TX_PAYLOAD_SIZE      (RM_LOG_VAR_MAX * 4u)

/* TX frame buffer: payload + 1-byte OpCode + 1-byte CRC. */
#define RM_TX_BUF_SIZE          (RM_TX_PAYLOAD_SIZE + 2u)

/* RX frame buffer: sized for the largest inbound command frame. */
#define RM_RX_BUF_SIZE          32u

/* ===========================================================================
 * Timing (milliseconds)
 * ===========================================================================
 */

/* Maximum silence between bytes of an in-progress SLIP frame. */
#define RM_RX_TIMEOUT_MS        100u

/* Maximum interval without a StartLog keepalive before slave stops logging. */
#define RM_KEEPALIVE_MS         2000u

/* Default log emission interval. */
#define RM_LOG_INTERVAL_MS      500u

/* ===========================================================================
 * Status code returned by every instruction handler
 * ===========================================================================
 */

typedef enum {
    RM_ERR       = 0,   /* instruction rejected (bad length, bad key, etc.) */
    RM_OK,              /* instruction accepted; a response frame will be sent */
    RM_OK_SILENT        /* instruction accepted; no response needed           */
} rm_status_t;

/* ===========================================================================
 * Frame buffers
 *
 * Owned exclusively by RmComm.  RmCore receives pointers to these during
 * rm_core_tick() and must not retain them after the call returns.
 * ===========================================================================
 */

typedef struct {
    uint8_t  buf[RM_RX_BUF_SIZE];
    uint16_t len;
    bool     complete;   /* true: a full decoded frame is ready for dispatch */
} rm_rx_frame_t;

typedef struct {
    uint8_t  buf[RM_TX_BUF_SIZE];
    uint16_t len;
    bool     ready;      /* true: frame awaits SLIP encoding; false: buffer is free */
} rm_tx_frame_t;

/* ===========================================================================
 * Shared data descriptors
 * ===========================================================================
 */

/* Pointer-plus-length descriptor for a contiguous memory block. */
typedef struct {
    rm_addr_t addr;
    uint16_t  len;
} rm_mem_ref_t;

/* Log variable configuration table, built incrementally by SetAddr frames. */
typedef struct {
    uint8_t   size[RM_LOG_VAR_MAX];   /* data width (bytes) per variable       */
    rm_addr_t addr[RM_LOG_VAR_MAX];   /* RAM address per variable              */
    uint16_t  write_idx;              /* cursor during the active SetAddr session */
    uint16_t  commit_idx;             /* committed count (updated on End flag)    */
    uint16_t  last_frag;              /* fragment index of the last accepted frag */
} rm_log_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* RM_TYPES_H */

/* end of file */

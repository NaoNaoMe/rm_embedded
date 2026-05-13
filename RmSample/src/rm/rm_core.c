/*
 * rm_core.c — Protocol engine implementation.
 *
 * Copyright 2026 Naoya Imai
 */

#include "rm_core.h"

/* ===========================================================================
 * Internal constants
 * ===========================================================================
 */

/* Instruction codes (lower nibble of the OpCode byte). */
#define INSTR_START_LOG     0x01u
#define INSTR_STOP_LOG      0x02u
#define INSTR_SET_PERIOD    0x03u
#define INSTR_WRITE         0x04u
#define INSTR_SET_ADDR      0x05u
#define INSTR_READ_INFO     0x06u
#define INSTR_READ_DUMP     0x07u

/* OpCode nibble masks. */
#define OPCODE_MAS_MASK     0xF0u   /* upper nibble: master transaction counter */
#define OPCODE_INS_MASK     0x0Fu   /* lower nibble: instruction code           */

/*
 * Log frame OpCode — Option A disambiguation rule:
 *
 * Log frames (slave→host, unsolicited) fix the upper nibble to 0x00:
 *   SlvOpCode = LOG_OPCODE_HI | slv_cnt    (slv_cnt in lower nibble, 1–15)
 *
 * Command responses always echo MasCnt (0x10–0xF0) in the upper nibble,
 * so the host can classify any received frame unambiguously by testing
 *   (frame[0] & 0xF0) == 0x00  →  log frame
 *   (frame[0] & 0xF0) != 0x00  →  command response
 */
#define LOG_OPCODE_HI       0x00u

/* SetAddr frameCode bit flags. */
#define SETLOG_BIT_MASK     0xF0u
#define SETLOG_START_BIT    0x10u
#define SETLOG_END_BIT      0x20u
#define SETLOG_CNT_MASK     0x0Fu

/* Frame byte indices within the raw decoded frame. */
#define IDX_OPCODE          0u
#define IDX_PAYLOAD         1u

/* Parser table sizes for Write and SetAddr payload validation.
 * Each entry maps a payload_len value to the expected data width (0=invalid). */
#ifdef RM_ADDR_4BYTE
#  define LOGPARSE_TABLE_SIZE    22u
#  define LOGPARSE_DATA_UNIT     5u   /* size(1) + address(4) per element */
   static const uint8_t log_parse_table[LOGPARSE_TABLE_SIZE] = {
       0,
       0,
       0, 0, 0, 0, 1, 0, 0, 0, 0, 2,
       0, 0, 0, 0, 3, 0, 0, 0, 0, 4
   };
#  define WPARSE_TABLE_SIZE      14u
   static const uint8_t write_parse_table[WPARSE_TABLE_SIZE] = {
       0,
       0, 0, 0, 0, 0,
       1, 2, 0, 4, 0, 0, 0, 8
   };
#else
#  define LOGPARSE_TABLE_SIZE    26u
#  define LOGPARSE_DATA_UNIT     3u   /* size(1) + address(2) per element */
   static const uint8_t log_parse_table[LOGPARSE_TABLE_SIZE] = {
       0,
       0,
       0, 0, 1, 0, 0, 2,
       0, 0, 3, 0, 0, 4,
       0, 0, 5, 0, 0, 6,
       0, 0, 7, 0, 0, 8
   };
#  define WPARSE_TABLE_SIZE      12u
   static const uint8_t write_parse_table[WPARSE_TABLE_SIZE] = {
       0,
       0, 0, 0,
       1, 2, 0, 4, 0, 0, 0, 8
   };
#endif

/* ===========================================================================
 * CRC-8 lookup table
 *
 * Polynomial: 0xD5 (x^8 + x^7 + x^6 + x^4 + x^2 + 1), initial value 0x00.
 * Validation: CRC of the complete received frame (all bytes including the
 * appended CRC byte) equals 0x00.
 * ===========================================================================
 */
static const uint8_t crc_table[256] = {
 /*      0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F         */
      0x00, 0xd5, 0x7f, 0xaa, 0xfe, 0x2b, 0x81, 0x54, 0x29, 0xfc, 0x56, 0x83, 0xd7, 0x02, 0xa8, 0x7d /* 00 */
    , 0x52, 0x87, 0x2d, 0xf8, 0xac, 0x79, 0xd3, 0x06, 0x7b, 0xae, 0x04, 0xd1, 0x85, 0x50, 0xfa, 0x2f /* 10 */
    , 0xa4, 0x71, 0xdb, 0x0e, 0x5a, 0x8f, 0x25, 0xf0, 0x8d, 0x58, 0xf2, 0x27, 0x73, 0xa6, 0x0c, 0xd9 /* 20 */
    , 0xf6, 0x23, 0x89, 0x5c, 0x08, 0xdd, 0x77, 0xa2, 0xdf, 0x0a, 0xa0, 0x75, 0x21, 0xf4, 0x5e, 0x8b /* 30 */
    , 0x9d, 0x48, 0xe2, 0x37, 0x63, 0xb6, 0x1c, 0xc9, 0xb4, 0x61, 0xcb, 0x1e, 0x4a, 0x9f, 0x35, 0xe0 /* 40 */
    , 0xcf, 0x1a, 0xb0, 0x65, 0x31, 0xe4, 0x4e, 0x9b, 0xe6, 0x33, 0x99, 0x4c, 0x18, 0xcd, 0x67, 0xb2 /* 50 */
    , 0x39, 0xec, 0x46, 0x93, 0xc7, 0x12, 0xb8, 0x6d, 0x10, 0xc5, 0x6f, 0xba, 0xee, 0x3b, 0x91, 0x44 /* 60 */
    , 0x6b, 0xbe, 0x14, 0xc1, 0x95, 0x40, 0xea, 0x3f, 0x42, 0x97, 0x3d, 0xe8, 0xbc, 0x69, 0xc3, 0x16 /* 70 */
    , 0xef, 0x3a, 0x90, 0x45, 0x11, 0xc4, 0x6e, 0xbb, 0xc6, 0x13, 0xb9, 0x6c, 0x38, 0xed, 0x47, 0x92 /* 80 */
    , 0xbd, 0x68, 0xc2, 0x17, 0x43, 0x96, 0x3c, 0xe9, 0x94, 0x41, 0xeb, 0x3e, 0x6a, 0xbf, 0x15, 0xc0 /* 90 */
    , 0x4b, 0x9e, 0x34, 0xe1, 0xb5, 0x60, 0xca, 0x1f, 0x62, 0xb7, 0x1d, 0xc8, 0x9c, 0x49, 0xe3, 0x36 /* A0 */
    , 0x19, 0xcc, 0x66, 0xb3, 0xe7, 0x32, 0x98, 0x4d, 0x30, 0xe5, 0x4f, 0x9a, 0xce, 0x1b, 0xb1, 0x64 /* B0 */
    , 0x72, 0xa7, 0x0d, 0xd8, 0x8c, 0x59, 0xf3, 0x26, 0x5b, 0x8e, 0x24, 0xf1, 0xa5, 0x70, 0xda, 0x0f /* C0 */
    , 0x20, 0xf5, 0x5f, 0x8a, 0xde, 0x0b, 0xa1, 0x74, 0x09, 0xdc, 0x76, 0xa3, 0xf7, 0x22, 0x88, 0x5d /* D0 */
    , 0xd6, 0x03, 0xa9, 0x7c, 0x28, 0xfd, 0x57, 0x82, 0xff, 0x2a, 0x80, 0x55, 0x01, 0xd4, 0x7e, 0xab /* E0 */
    , 0x84, 0x51, 0xfb, 0x2e, 0x7a, 0xaf, 0x05, 0xd0, 0xad, 0x78, 0xd2, 0x07, 0x53, 0x86, 0x2c, 0xf9 /* F0 */
};

/* ===========================================================================
 * Private helpers — little-endian byte assembly
 * ===========================================================================
 */

static uint16_t read_le_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le_u32(const uint8_t *p)
{
    return  (uint32_t)p[0]          |
           ((uint32_t)p[1] <<  8)   |
           ((uint32_t)p[2] << 16)   |
           ((uint32_t)p[3] << 24);
}

static uint16_t write_le_u8(uint8_t *dst, uint8_t v)
{
    dst[0] = v;
    return 1u;
}

static uint16_t write_le_u16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v);
    dst[1] = (uint8_t)(v >> 8);
    return 2u;
}

static uint16_t write_le_u32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v);
    dst[1] = (uint8_t)(v >>  8);
    dst[2] = (uint8_t)(v >> 16);
    dst[3] = (uint8_t)(v >> 24);
    return 4u;
}

#ifdef RM_SUPPORT_64BIT
static uint64_t read_le_u64(const uint8_t *p)
{
    return  (uint64_t)p[0]          |
           ((uint64_t)p[1] <<  8)   |
           ((uint64_t)p[2] << 16)   |
           ((uint64_t)p[3] << 24)   |
           ((uint64_t)p[4] << 32)   |
           ((uint64_t)p[5] << 40)   |
           ((uint64_t)p[6] << 48)   |
           ((uint64_t)p[7] << 56);
}

static uint16_t write_le_u64(uint8_t *dst, uint64_t v)
{
    dst[0] = (uint8_t)(v);
    dst[1] = (uint8_t)(v >>  8);
    dst[2] = (uint8_t)(v >> 16);
    dst[3] = (uint8_t)(v >> 24);
    dst[4] = (uint8_t)(v >> 32);
    dst[5] = (uint8_t)(v >> 40);
    dst[6] = (uint8_t)(v >> 48);
    dst[7] = (uint8_t)(v >> 56);
    return 8u;
}
#endif /* RM_SUPPORT_64BIT */

/* ===========================================================================
 * Private helpers — CRC
 * ===========================================================================
 */

static uint8_t calc_crc8(const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t crc = 0u;
    for (i = 0u; i < len; i++)
    {
        crc = crc_table[crc ^ buf[i]];
    }
    return crc;
}

/* ===========================================================================
 * Private helpers — TX frame builders
 * ===========================================================================
 */

/*
 * copy_block — copy the memory block described by ref into tx->buf starting
 *              at IDX_PAYLOAD.  Returns the number of bytes written.
 */
static uint16_t copy_block(const rm_mem_ref_t *ref, rm_tx_frame_t *tx)
{
    uint16_t  i;
    uint8_t  *src = (uint8_t *)(uintptr_t)ref->addr;

    for (i = 0u; i < ref->len; i++)
    {
        tx->buf[IDX_PAYLOAD + i] = src[i];
    }
    return i;
}

/*
 * snapshot_log_vars — read each configured log variable from RAM and pack it
 *                     into tx->buf starting at IDX_PAYLOAD.
 *
 * Returns the index of the next free byte (= IDX_PAYLOAD + total data bytes),
 * or 0 if no variables are configured.
 */
static uint16_t snapshot_log_vars(const rm_log_cfg_t *log, rm_tx_frame_t *tx)
{
    uint16_t  i;
    uint16_t  out = IDX_PAYLOAD;
    uint8_t  *dst;

    if (log->commit_idx == 0u)
    {
        return 0u;
    }

    for (i = 0u; i < log->commit_idx; i++)
    {
        dst = &tx->buf[out];

        switch (log->size[i])
        {
        case 1u: {
            uint8_t *src = (uint8_t *)(uintptr_t)log->addr[i];
            out += write_le_u8(dst, *src);
            break;
        }
        case 2u: {
            uint16_t *src = (uint16_t *)(uintptr_t)log->addr[i];
            out += write_le_u16(dst, *src);
            break;
        }
        case 4u: {
            uint32_t *src = (uint32_t *)(uintptr_t)log->addr[i];
            out += write_le_u32(dst, *src);
            break;
        }
#ifdef RM_SUPPORT_64BIT
        case 8u: {
            uint64_t *src = (uint64_t *)(uintptr_t)log->addr[i];
            out += write_le_u64(dst, *src);
            break;
        }
#endif
        default:
            break;
        }
    }

    return out;
}

/*
 * build_block_response — assemble a command-response frame in tx->buf.
 *
 * Format: [ (MasCnt | SlvCnt) | block_data... | CRC ]
 *
 * Returns true if the frame was built (tx was free).
 * Returns false if tx is still busy; caller should retry next tick.
 */
static bool build_block_response(rm_proto_t *p, rm_tx_frame_t *tx)
{
    uint16_t data_len;
    uint16_t frame_len;

    if (tx->ready)
    {
        return false;
    }

    /* Advance the slave sequence counter. */
    p->slv_cnt++;
    if (p->slv_cnt > 0x0Fu)
    {
        p->slv_cnt = 0x01u;
    }

    data_len  = copy_block(&p->pending_block, tx);
    frame_len = 1u + data_len;   /* OpCode(1) + payload */

    tx->buf[IDX_OPCODE] = (uint8_t)(p->mas_cnt + p->slv_cnt);

    tx->buf[frame_len] = calc_crc8(tx->buf, (uint8_t)frame_len);
    frame_len++;

    tx->len   = frame_len;
    tx->ready = true;

    return true;
}

/*
 * build_log_frame — snapshot log variables and assemble a log frame in tx->buf.
 *
 * Log frame OpCode (Option A): upper nibble = 0x00, lower nibble = slv_cnt.
 * slv_cnt is incremented only after confirming the buffer is free and data
 * is available, so the counter reflects only actually transmitted frames.
 *
 * Returns true if a frame was built and queued.
 */
static bool build_log_frame(rm_proto_t *p, rm_tx_frame_t *tx)
{
    uint16_t payload_end;
    uint8_t  frame_len;

    if (tx->ready)
    {
        return false;
    }

    payload_end = (uint8_t)snapshot_log_vars(&p->log, tx);
    if (payload_end == 0u)
    {
        return false;
    }

    /* Increment counter only now that we know the frame will be sent. */
    p->slv_cnt++;
    if (p->slv_cnt > 0x0Fu)
    {
        p->slv_cnt = 0x01u;
    }

    tx->buf[IDX_OPCODE] = LOG_OPCODE_HI | (uint8_t)p->slv_cnt;

    frame_len = (uint8_t)payload_end;
    tx->buf[frame_len] = calc_crc8(tx->buf, frame_len);
    frame_len++;

    tx->len   = frame_len;
    tx->ready = true;

    return true;
}

/* ===========================================================================
 * Instruction handlers
 *
 * Each handler:
 *   1. Validates the RX payload length.
 *   2. Updates protocol state in p.
 *   3. Sets p->pending_block (addr=0, len=0 means empty response payload).
 *   4. Returns RM_OK, RM_OK_SILENT, or RM_ERR.
 *
 * Handlers do not touch the TX frame directly; the caller (rm_core_tick) calls
 * build_block_response() after a successful handler returns RM_OK.
 * ===========================================================================
 */

static rm_status_t handle_start_log(rm_proto_t *p, const rm_rx_frame_t *rx)
{
    /* Frame: [ OpCode | CRC ] — no payload; decoded length after CRC strip = 1 */
    if (rx->len != 1u)
    {
        return RM_ERR;
    }

    p->pending_block.addr = 0u;
    p->pending_block.len  = 0u;

    if (p->is_logging)
    {
        return RM_OK_SILENT;   /* keepalive: already logging, no response */
    }

    p->is_logging = true;
    return RM_OK;
}

static rm_status_t handle_stop_log(rm_proto_t *p, const rm_rx_frame_t *rx)
{
    if (rx->len != 1u)
    {
        return RM_ERR;
    }

    p->pending_block.addr = 0u;
    p->pending_block.len  = 0u;
    p->is_logging         = false;

    return RM_OK;
}

static rm_status_t handle_set_period(rm_proto_t *p, const rm_rx_frame_t *rx)
{
    uint16_t period;

    /* Frame: [ OpCode | period_lo | period_hi | CRC ] → decoded length = 3 */
    if (rx->len != 3u)
    {
        return RM_ERR;
    }

    period = read_le_u16(&rx->buf[IDX_PAYLOAD]);
    if (period == 0u)
    {
        return RM_ERR;
    }

    p->log_interval_period = period;
    p->pending_block.addr  = 0u;
    p->pending_block.len   = 0u;
    p->is_logging          = false;

    return RM_OK;
}

static rm_status_t handle_write(rm_proto_t *p, const rm_rx_frame_t *rx)
{
    rm_addr_t address;
    uint16_t  offset;
    uint8_t   size;
    uint16_t  payload_len = rx->len - 1u;

    if (payload_len == 0u || payload_len >= WPARSE_TABLE_SIZE)
    {
        return RM_ERR;
    }

    size = rx->buf[IDX_PAYLOAD];
    if (size == 0u || size != write_parse_table[payload_len])
    {
        return RM_ERR;
    }

#ifdef RM_ADDR_4BYTE
    address = read_le_u32(&rx->buf[IDX_PAYLOAD + 1u]);
    offset  = 5u;
#else
    address = read_le_u16(&rx->buf[IDX_PAYLOAD + 1u]);
    offset  = 3u;
#endif

    switch (size)
    {
    case 1u: {
        uint8_t *ptr = (uint8_t *)(uintptr_t)address;
        *ptr = rx->buf[IDX_PAYLOAD + offset];
        break;
    }
    case 2u: {
        uint16_t *ptr = (uint16_t *)(uintptr_t)address;
        *ptr = read_le_u16(&rx->buf[IDX_PAYLOAD + offset]);
        break;
    }
    case 4u: {
        uint32_t *ptr = (uint32_t *)(uintptr_t)address;
        *ptr = read_le_u32(&rx->buf[IDX_PAYLOAD + offset]);
        break;
    }
#ifdef RM_SUPPORT_64BIT
    case 8u: {
        uint64_t *ptr = (uint64_t *)(uintptr_t)address;
        *ptr = read_le_u64(&rx->buf[IDX_PAYLOAD + offset]);
        break;
    }
#endif
    default:
        break;
    }

    p->pending_block.addr = 0u;
    p->pending_block.len  = 0u;

    /* During logging, Write is injected silently — no reply sent. */
    if (p->is_logging)
    {
        return RM_OK_SILENT;
    }

    return RM_OK;
}

static rm_status_t handle_set_addr(rm_proto_t *p, const rm_rx_frame_t *rx)
{
    rm_addr_t address;
    uint8_t   size;
    uint16_t  total_size;
    uint16_t  base;
    uint16_t  i;
    uint8_t   bitmap;
    uint8_t   frag_cnt;
    uint16_t  max_elem;
    uint16_t  payload_len = rx->len - 1u;

    if (payload_len == 0u || payload_len >= LOGPARSE_TABLE_SIZE)
    {
        goto error;
    }

    max_elem = (uint16_t)log_parse_table[payload_len];
    if (max_elem == 0u)
    {
        goto error;
    }

    if ((p->log.write_idx + max_elem) > RM_LOG_VAR_MAX)
    {
        goto error;
    }

    bitmap   = rx->buf[IDX_PAYLOAD] & (uint8_t)SETLOG_BIT_MASK;
    frag_cnt = rx->buf[IDX_PAYLOAD] & (uint8_t)SETLOG_CNT_MASK;

    if ((bitmap & SETLOG_START_BIT) != 0u)
    {
        p->log.write_idx = 0u;
        p->log.last_frag = 0u;
    }
    else if (frag_cnt == p->log.last_frag)
    {
        goto success;   /* duplicate fragment; already processed */
    }
    else if (frag_cnt != (p->log.last_frag + 1u))
    {
        goto error;    /* out-of-order fragment */
    }

    p->log.last_frag = frag_cnt;

    for (i = 0u; i < max_elem; i++)
    {
        base = (i * LOGPARSE_DATA_UNIT) + 1u;   /* +1 skips frameCode byte */
        size = rx->buf[IDX_PAYLOAD + base];

#ifdef RM_SUPPORT_64BIT
        if (size != 1u && size != 2u && size != 4u && size != 8u)
#else
        if (size != 1u && size != 2u && size != 4u)
#endif
        {
            goto error;
        }

        p->log.size[p->log.write_idx] = size;

#ifdef RM_ADDR_4BYTE
        address = read_le_u32(&rx->buf[IDX_PAYLOAD + base + 1u]);
#else
        address = read_le_u16(&rx->buf[IDX_PAYLOAD + base + 1u]);
#endif
        p->log.addr[p->log.write_idx] = address;
        p->log.write_idx++;
    }

    if ((bitmap & SETLOG_END_BIT) != 0u)
    {
        p->log.commit_idx = p->log.write_idx;
    }

    /* Validate total payload fits in the TX buffer. */
    total_size = 0u;
    for (i = 0u; i < p->log.write_idx; i++)
    {
        total_size += (uint16_t)p->log.size[i];
    }
    if (total_size > RM_TX_PAYLOAD_SIZE)
    {
        goto error;
    }

success:
    p->pending_block.addr = 0u;
    p->pending_block.len  = 0u;
    p->is_logging         = false;
    return RM_OK;

error:
    p->log.write_idx  = 0u;
    p->log.commit_idx = 0u;
    p->log.last_frag  = 0u;
    p->pending_block.addr = 0u;
    p->pending_block.len  = 0u;
    p->is_logging         = false;
    return RM_ERR;
}

static rm_status_t handle_read_info(rm_proto_t *p, const rm_rx_frame_t *rx)
{
    uint32_t key;
    uint16_t len;

    /* Frame: [ OpCode | passNumber[0..3] | CRC ] → decoded length = 5 */
    if (rx->len != 5u)
    {
        p->is_approved = false;
        return RM_ERR;
    }

    key = read_le_u32(&rx->buf[IDX_PAYLOAD]);
    if (key != p->pass_key)
    {
        p->is_approved = false;
        return RM_ERR;
    }

    len = p->version.len;
    if (len > RM_TX_PAYLOAD_SIZE)
    {
        len = (uint16_t)RM_TX_PAYLOAD_SIZE;
    }

    p->is_approved        = true;
    p->is_logging         = false;
    p->pending_block.addr = p->version.addr;
    p->pending_block.len  = len;

    return RM_OK;
}

static rm_status_t handle_read_dump(rm_proto_t *p, const rm_rx_frame_t *rx)
{
    rm_addr_t address;
    uint16_t  len;
    uint16_t  payload_len = rx->len - 1u;

#ifdef RM_ADDR_4BYTE
    /* Frame: [ OpCode | addr(4) | size(1) | CRC ] → decoded length = 6 */
    if (payload_len != 5u)
    {
        return RM_ERR;
    }
    address = read_le_u32(&rx->buf[IDX_PAYLOAD]);
    len     = rx->buf[IDX_PAYLOAD + 4u];
#else
    /* Frame: [ OpCode | addr(2) | size(1) | CRC ] → decoded length = 4 */
    if (payload_len != 3u)
    {
        return RM_ERR;
    }
    address = read_le_u16(&rx->buf[IDX_PAYLOAD]);
    len     = rx->buf[IDX_PAYLOAD + 2u];
#endif

    if (len > RM_TX_PAYLOAD_SIZE)
    {
        return RM_ERR;
    }

    p->pending_block.addr = address;
    p->pending_block.len  = len;
    p->is_logging         = false;

    return RM_OK;
}

/* ===========================================================================
 * Instruction dispatcher
 * ===========================================================================
 */

static rm_status_t dispatch(rm_proto_t *p, const rm_rx_frame_t *rx, uint8_t instr)
{
    if (!p->is_approved)
    {
        /* Before authentication, only ReadInfo is accepted. */
        if (instr == INSTR_READ_INFO)
        {
            return handle_read_info(p, rx);
        }
        return RM_ERR;
    }

    switch (instr)
    {
    case INSTR_START_LOG:  return handle_start_log(p, rx);
    case INSTR_STOP_LOG:   return handle_stop_log(p, rx);
    case INSTR_SET_PERIOD: return handle_set_period(p, rx);
    case INSTR_WRITE:      return handle_write(p, rx);
    case INSTR_SET_ADDR:   return handle_set_addr(p, rx);
    case INSTR_READ_INFO:  return handle_read_info(p, rx);
    case INSTR_READ_DUMP:  return handle_read_dump(p, rx);
    default:               return RM_ERR;
    }
}

/* ===========================================================================
 * Public API implementation
 * ===========================================================================
 */

void rm_core_init(rm_proto_t *p,
                  uint8_t    *ver,
                  uint16_t    ver_len,
                  uint16_t    ms,
                  uint32_t    key)
{
    p->is_approved      = false;
    p->is_logging       = false;
    p->response_pending = false;

    p->mas_cnt = 0x00u;
    p->slv_cnt = 0x01u;

    p->pass_key   = key;
    p->millis_cnt = ms;

    p->log_timeout_cnt      = 0u;
    p->log_interval_cnt     = 0u;
    p->log_interval_period  = RM_LOG_INTERVAL_MS;

    p->log.write_idx  = 0u;
    p->log.commit_idx = 0u;
    p->log.last_frag  = 0u;

    p->version.addr = (rm_addr_t)(uintptr_t)ver;
    p->version.len  = ver_len;

    p->pending_block.addr = 0u;
    p->pending_block.len  = 0u;
}

void rm_core_tick(rm_proto_t    *p,
                  rm_rx_frame_t *rx,
                  rm_tx_frame_t *tx)
{
    rm_status_t result;
    uint8_t     instr;
    uint8_t     mas_nibble;

    /* --- Retry pending block response -------------------------------------- */
    if (p->response_pending)
    {
        p->response_pending = !build_block_response(p, tx);
    }

    /* --- Dispatch completed RX frame --------------------------------------- */
    if (rx->complete && !p->response_pending)
    {
        if (calc_crc8(rx->buf, (uint8_t)rx->len) == 0u)
        {
            rx->len--;   /* strip the trailing CRC byte */

            instr      = rx->buf[IDX_OPCODE] & (uint8_t)OPCODE_INS_MASK;
            mas_nibble = rx->buf[IDX_OPCODE] & (uint8_t)OPCODE_MAS_MASK;

            result = dispatch(p, rx, instr);

            if (result != RM_ERR)
            {
                p->mas_cnt         = mas_nibble;
                p->log_timeout_cnt = 0u;
            }

            if (result == RM_OK)
            {
                p->response_pending = !build_block_response(p, tx);
            }
        }

        /* Clear RX regardless of CRC outcome. */
        rx->complete = false;
        rx->len      = 0u;
    }

    /* --- Keepalive timeout and log emission -------------------------------- */
    if (p->is_logging)
    {
        p->log_timeout_cnt += p->millis_cnt;
        if (p->log_timeout_cnt >= RM_KEEPALIVE_MS)
        {
            p->log_timeout_cnt = 0u;
            p->is_logging      = false;
        }

        p->log_interval_cnt += p->millis_cnt;
        if (p->log_interval_cnt >= p->log_interval_period)
        {
            p->log_interval_cnt = 0u;
            if (!p->response_pending)
            {
                build_log_frame(p, tx);
            }
        }
    }
}

/* end of file */

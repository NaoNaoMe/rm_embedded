/*
 * rm_comm.c — Transport layer implementation.
 *
 * Copyright 2026 Naoya Imai
 */

#include "rm_comm.h"
#include <string.h>

/* ===========================================================================
 * SLIP constants
 * ===========================================================================
 */
#define SLIP_END        0xC0u   /* frame delimiter                          */
#define SLIP_ESC        0xDBu   /* escape prefix                            */
#define SLIP_ESC_END    0xDCu   /* ESC + ESC_END encodes 0xC0 in payload    */
#define SLIP_ESC_ESC    0xDDu   /* ESC + ESC_ESC encodes 0xDB in payload    */

/* ===========================================================================
 * SLIP receive state machine
 *
 * Decodes one raw byte at a time into rx_frame.buf.
 * Sets rx_frame.complete when a full frame is delimited by an END byte.
 * ===========================================================================
 */
typedef enum {
    SLIP_RX_IDLE,      /* waiting for the opening END byte                  */
    SLIP_RX_FRAME,     /* inside a frame; accumulating decoded bytes         */
    SLIP_RX_ESCAPE     /* received ESC; next byte is the encoded value       */
} slip_rx_state_t;

typedef struct {
    slip_rx_state_t state;
    uint16_t        timeout_cnt;   /* ms elapsed since the last byte received */
} slip_rx_ctx_t;

/* ===========================================================================
 * SLIP transmit state machine
 *
 * Emits one SLIP-encoded byte per call.
 * Source buffer (tx_frame.buf) is never modified.
 * Releases tx_frame.ready after the closing END byte is emitted.
 * ===========================================================================
 */
typedef enum {
    SLIP_TX_IDLE,     /* no frame in progress                                */
    SLIP_TX_OPEN,     /* about to emit the opening END                       */
    SLIP_TX_DATA,     /* emitting payload bytes (with byte-stuffing)          */
    SLIP_TX_ESC,      /* sent ESC; next call emits the code byte              */
    SLIP_TX_CLOSE     /* about to emit the closing END                       */
} slip_tx_state_t;

typedef struct {
    slip_tx_state_t state;
    uint16_t        index;     /* read cursor into tx_frame.buf               */
    uint8_t         esc_code;  /* SLIP_ESC_END or SLIP_ESC_ESC                */
} slip_tx_ctx_t;

/* ===========================================================================
 * Circular buffer
 *
 * Power-of-two size; mask-based index wrapping avoids branches.
 * Capacity is (size − 1) bytes (one slot reserved to distinguish full/empty).
 * ===========================================================================
 */
typedef struct {
    uint8_t  *buf;
    uint16_t  head;   /* read index  */
    uint16_t  tail;   /* write index */
    uint16_t  mask;   /* size − 1    */
} rm_circ_t;

/* ===========================================================================
 * Module context
 * ===========================================================================
 */
typedef struct {
    rm_proto_t    proto;
    rm_rx_frame_t rx_frame;
    rm_tx_frame_t tx_frame;
    slip_rx_ctx_t slip_rx;
    slip_tx_ctx_t slip_tx;
    rm_circ_t     rx_raw;
    rm_circ_t     rx_sce;
    rm_circ_t     tx_sce;
} rm_comm_t;

static rm_comm_t rm_ctx;

/* Backing arrays for circular buffers. */
static uint8_t rm_rx_raw_buf[RM_CIRC_RX_SIZE];
static uint8_t rm_rx_sce_buf[RM_CIRC_SCE_RX_SIZE];
static uint8_t rm_tx_sce_buf[RM_CIRC_SCE_TX_SIZE];

/* ===========================================================================
 * Private: circular buffer operations
 * ===========================================================================
 */

static void circ_init(rm_circ_t *c, uint8_t *array, uint16_t size)
{
    uint16_t i;
    uint16_t bit = 0u;
    uint16_t tmp = size;

    c->head = 0u;
    c->tail = 0u;
    c->buf  = array;

    /* Find the highest set bit to compute mask = (1 << bit) − 1. */
    for (i = 0u; i < 16u; i++)
    {
        if ((tmp & 0x0001u) != 0u)
        {
            bit = i;
        }
        tmp >>= 1;
    }
    c->mask = (uint16_t)((1u << bit) - 1u);
}

static bool circ_enqueue(rm_circ_t *c, uint8_t data)
{
    uint16_t next = (c->tail + 1u) & c->mask;
    if (next == c->head)
    {
        return false;   /* full */
    }
    c->buf[c->tail] = data;
    c->tail = next;
    return true;
}

static bool circ_dequeue(rm_circ_t *c, uint8_t *out)
{
    if (c->head == c->tail)
    {
        return false;   /* empty */
    }
    *out    = c->buf[c->head];
    c->head = (c->head + 1u) & c->mask;
    return true;
}

static uint16_t circ_available(const rm_circ_t *c)
{
    return (c->tail - c->head) & c->mask;
}

/* ===========================================================================
 * Private: SLIP receive decoder
 * ===========================================================================
 */

static void slip_rx_byte(slip_rx_ctx_t *slip, rm_rx_frame_t *rx, uint8_t data)
{
    /* Ignore input while a completed frame waits to be processed. */
    if (rx->complete)
    {
        return;
    }

    /* Every received byte resets the inter-byte timeout. */
    slip->timeout_cnt = 0u;

    /* Guard against buffer overflow. */
    if (rx->len >= RM_RX_BUF_SIZE)
    {
        slip->state  = SLIP_RX_IDLE;
        rx->len      = 0u;
        return;
    }

    switch (slip->state)
    {
    case SLIP_RX_IDLE:
        if (data == SLIP_END)
        {
            slip->state = SLIP_RX_FRAME;
        }
        break;

    case SLIP_RX_FRAME:
        if (data == SLIP_ESC)
        {
            slip->state = SLIP_RX_ESCAPE;
        }
        else if (data == SLIP_END)
        {
            if (rx->len == 0u)
            {
                /* Consecutive END bytes: treat as the opening of a new frame. */
            }
            else
            {
                rx->complete = true;
                slip->state  = SLIP_RX_IDLE;
            }
        }
        else
        {
            rx->buf[rx->len++] = data;
        }
        break;

    case SLIP_RX_ESCAPE:
        slip->state = SLIP_RX_FRAME;

        if (data == SLIP_ESC_END)
        {
            rx->buf[rx->len++] = SLIP_END;
        }
        else if (data == SLIP_ESC_ESC)
        {
            rx->buf[rx->len++] = SLIP_ESC;
        }
        else
        {
            /* Invalid escape: discard the partial frame. */
            slip->state = SLIP_RX_IDLE;
            rx->len     = 0u;
        }
        break;
    }
}

/* ===========================================================================
 * Private: SLIP transmit encoder
 * ===========================================================================
 */

/*
 * slip_tx_byte — emit the next SLIP byte for the current frame.
 *
 * Returns true if *out holds a byte that must be sent.
 * Returns false when no more bytes remain (frame complete, SLIP_TX_IDLE).
 *
 * On SLIP_TX_CLOSE: emits the closing END, clears tx->ready, returns true.
 * The following call finds state == IDLE and returns false — signalling done.
 */
static bool slip_tx_byte(slip_tx_ctx_t *slip, rm_tx_frame_t *tx, uint8_t *out)
{
    uint8_t b;

    switch (slip->state)
    {
    case SLIP_TX_IDLE:
        return false;

    case SLIP_TX_OPEN:
        *out         = SLIP_END;
        slip->index  = 0u;
        slip->state  = (tx->len > 0u) ? SLIP_TX_DATA : SLIP_TX_CLOSE;
        return true;

    case SLIP_TX_DATA:
        b = tx->buf[slip->index];
        if (b == SLIP_END)
        {
            *out           = SLIP_ESC;
            slip->esc_code = SLIP_ESC_END;
            slip->state    = SLIP_TX_ESC;
        }
        else if (b == SLIP_ESC)
        {
            *out           = SLIP_ESC;
            slip->esc_code = SLIP_ESC_ESC;
            slip->state    = SLIP_TX_ESC;
        }
        else
        {
            *out = b;
            slip->index++;
            if (slip->index >= tx->len)
            {
                slip->state = SLIP_TX_CLOSE;
            }
        }
        return true;

    case SLIP_TX_ESC:
        *out = slip->esc_code;
        slip->index++;
        slip->state = (slip->index < tx->len) ? SLIP_TX_DATA : SLIP_TX_CLOSE;
        return true;

    case SLIP_TX_CLOSE:
        *out        = SLIP_END;
        slip->state = SLIP_TX_IDLE;
        tx->ready   = false;   /* release the TX buffer for the next frame */
        return true;
    }

    return false;
}

/* ===========================================================================
 * Private: SCE text helper
 * ===========================================================================
 */

static void sce_write_str(const char *str)
{
    if (str == NULL)
    {
        return;
    }
    while (*str != '\0')
    {
        rm_comm_write((uint8_t)*str);
        str++;
    }
}

/* ===========================================================================
 * Public API implementation
 * ===========================================================================
 */

void rm_comm_init(uint8_t  *ver,
                  uint16_t  ver_len,
                  uint16_t  ms,
                  uint32_t  key)
{
    rm_core_init(&rm_ctx.proto, ver, ver_len, ms, key);

    rm_ctx.slip_rx.state       = SLIP_RX_IDLE;
    rm_ctx.slip_rx.timeout_cnt = 0u;

    rm_ctx.slip_tx.state    = SLIP_TX_IDLE;
    rm_ctx.slip_tx.index    = 0u;
    rm_ctx.slip_tx.esc_code = 0u;

    rm_ctx.rx_frame.complete = false;
    rm_ctx.rx_frame.len      = 0u;

    rm_ctx.tx_frame.ready = false;
    rm_ctx.tx_frame.len   = 0u;

    circ_init(&rm_ctx.rx_raw, rm_rx_raw_buf, sizeof(rm_rx_raw_buf));
    circ_init(&rm_ctx.rx_sce, rm_rx_sce_buf, sizeof(rm_rx_sce_buf));
    circ_init(&rm_ctx.tx_sce, rm_tx_sce_buf, sizeof(rm_tx_sce_buf));
}

void rm_comm_run(void)
{
    uint16_t count;
    uint16_t i;
    uint8_t  byte;

    /* --- 0. Inter-byte timeout -------------------------------------------- */
    if (rm_ctx.slip_rx.state != SLIP_RX_IDLE)
    {
        rm_ctx.slip_rx.timeout_cnt += rm_ctx.proto.millis_cnt;
        if (rm_ctx.slip_rx.timeout_cnt >= RM_RX_TIMEOUT_MS)
        {
            rm_ctx.slip_rx.state       = SLIP_RX_IDLE;
            rm_ctx.slip_rx.timeout_cnt = 0u;
            rm_ctx.rx_frame.complete   = false;
            rm_ctx.rx_frame.len        = 0u;
        }
    }

    /* --- 1. Drain raw RX bytes through the SLIP decoder ------------------- */
    if (!rm_ctx.rx_frame.complete)
    {
        count = circ_available(&rm_ctx.rx_raw);
        while (count > 0u)
        {
            circ_dequeue(&rm_ctx.rx_raw, &byte);
            count--;
            slip_rx_byte(&rm_ctx.slip_rx, &rm_ctx.rx_frame, byte);
            if (rm_ctx.rx_frame.complete)
            {
                break;
            }
        }
    }

    /* --- 2. Route derived frames (SCE) ------------------------------------ */
    if (rm_ctx.rx_frame.complete &&
        rm_ctx.rx_frame.buf[0] == RM_DERIVED_MARKER)
    {
        if (rm_ctx.rx_frame.buf[1] == RM_DERIVED_SCE &&
            rm_ctx.proto.is_approved)
        {
            for (i = 2u; i < rm_ctx.rx_frame.len; i++)
            {
                circ_enqueue(&rm_ctx.rx_sce, rm_ctx.rx_frame.buf[i]);
            }
        }
        rm_ctx.rx_frame.complete = false;
        rm_ctx.rx_frame.len      = 0u;
    }

    /* --- 3. Protocol tick ------------------------------------------------- */
    rm_core_tick(&rm_ctx.proto, &rm_ctx.rx_frame, &rm_ctx.tx_frame);

    /* --- 4. TX arbiter: SCE fills idle time ------------------------------- */
    if (!rm_ctx.tx_frame.ready && rm_ctx.proto.is_approved)
    {
        count = circ_available(&rm_ctx.tx_sce);
        if (count > 0u)
        {
            /* Clamp to available payload space (buf size minus 2-byte header). */
            if (count > (uint16_t)(RM_TX_BUF_SIZE - 2u))
            {
                count = (uint16_t)(RM_TX_BUF_SIZE - 2u);
            }

            rm_ctx.tx_frame.buf[0] = RM_DERIVED_MARKER;
            rm_ctx.tx_frame.buf[1] = RM_DERIVED_SCE;

            for (i = 0u; i < count; i++)
            {
                circ_dequeue(&rm_ctx.tx_sce, &rm_ctx.tx_frame.buf[2u + i]);
            }

            rm_ctx.tx_frame.len   = 2u + count;
            rm_ctx.tx_frame.ready = true;
        }
    }
}

bool rm_comm_try_transmit(uint8_t *out)
{
    if(rm_ctx.slip_tx.state != SLIP_TX_IDLE)
    {
        return false;
    }

    if (!rm_ctx.tx_frame.ready)
    {
        return false;
    }

    rm_ctx.slip_tx.state = SLIP_TX_OPEN;
    rm_ctx.slip_tx.index = 0u;

    return slip_tx_byte(&rm_ctx.slip_tx, &rm_ctx.tx_frame, out);
}

bool rm_comm_get_tx_byte(uint8_t *out)
{
    return slip_tx_byte(&rm_ctx.slip_tx, &rm_ctx.tx_frame, out);
}

void rm_comm_push_rx_byte(uint8_t byte)
{
    circ_enqueue(&rm_ctx.rx_raw, byte);
}

bool rm_comm_is_logging(void)
{
    return rm_ctx.proto.is_logging;
}

/* ===========================================================================
 * SCE channel I/O
 * ===========================================================================
 */

void rm_comm_write(uint8_t data)
{
    if (!rm_ctx.proto.is_approved)
    {
        return;
    }
    circ_enqueue(&rm_ctx.tx_sce, data);
}

uint8_t rm_comm_read(void)
{
    uint8_t data = 0u;
    circ_dequeue(&rm_ctx.rx_sce, &data);
    return data;
}

uint16_t rm_comm_available(void)
{
    return circ_available(&rm_ctx.rx_sce);
}

/* ===========================================================================
 * SCE text helpers
 * ===========================================================================
 */

void rm_comm_print(const char *str)
{
    sce_write_str(str);
}

void rm_comm_println(void)
{
    rm_comm_write((uint8_t)'\r');
    rm_comm_write((uint8_t)'\n');
}

void rm_comm_print_u32(uint32_t n)
{
    char     buf[11];   /* longest uint32_t decimal (10 digits) + NUL */
    char    *p    = &buf[sizeof(buf) - 1u];
    uint8_t  base = 10u;

    *p = '\0';
    do {
        char d = (char)(n % base);
        n     /= base;
        *--p   = (d < 10) ? (char)(d + '0') : (char)(d + 'A' - 10);
    } while (n > 0u);

    sce_write_str(p);
}

void rm_comm_print_i32(int32_t n)
{
    if (n < 0)
    {
        rm_comm_write((uint8_t)'-');
        n = -n;
    }
    rm_comm_print_u32((uint32_t)n);
}

void rm_comm_print_float(float f)
{
    const uint8_t digits   = 2u;
    float         rounding = 0.5f;
    uint32_t      int_part;
    float         remainder;
    uint8_t       i;

    if (f < 0.0f)
    {
        rm_comm_write((uint8_t)'-');
        f = -f;
    }

    for (i = 0u; i < digits; i++)
    {
        rounding /= 10.0f;
    }
    f += rounding;

    int_part  = (uint32_t)f;
    remainder = f - (float)int_part;

    rm_comm_print_u32(int_part);
    rm_comm_write((uint8_t)'.');

    for (i = digits; i > 0u; i--)
    {
        uint32_t d;
        remainder *= 10.0f;
        d          = (uint32_t)remainder;
        rm_comm_print_u32(d);
        remainder -= (float)d;
    }
}

/* end of file */

#include "cobs_link.h"

/* ---- COBS encoder ----------------------------------------------------- */

/* Incremental encoder so a frame can be built from payload + CRC without
 * first copying them into one contiguous buffer. */
typedef struct {
    uint8_t *out;
    size_t   w;         /* next write index */
    size_t   code_idx;  /* where the current block's code byte goes */
    uint8_t  code;      /* current block length + 1 */
} cobs_enc_t;

static void enc_init(cobs_enc_t *e, uint8_t *out)
{
    e->out = out;
    e->code_idx = 0;
    e->w = 1;
    e->code = 1;
}

static void enc_close_block(cobs_enc_t *e)
{
    e->out[e->code_idx] = e->code;
    e->code_idx = e->w++;
    e->code = 1;
}

static void enc_put(cobs_enc_t *e, uint8_t b)
{
    /* A full 254-byte block is only closed once more data shows up, which
     * keeps the output canonical (no spurious trailing 0x01). */
    if (e->code == 0xFF) {
        enc_close_block(e);
    }
    if (b == 0) {
        enc_close_block(e);
    } else {
        e->out[e->w++] = b;
        e->code++;
    }
}

static size_t enc_finish(cobs_enc_t *e)
{
    e->out[e->code_idx] = e->code;
    return e->w;
}

size_t cl_cobs_encode(const uint8_t *in, size_t len, uint8_t *out)
{
    cobs_enc_t e;
    enc_init(&e, out);
    for (size_t i = 0; i < len; i++) {
        enc_put(&e, in[i]);
    }
    return enc_finish(&e);
}

/* ---- COBS decoder ----------------------------------------------------- */

cl_err_t cl_cobs_decode(const uint8_t *in, size_t len,
                        uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t r = 0, w = 0;

    /* w never passes r, so out may alias in. */
    while (r < len) {
        uint8_t code = in[r++];
        if (code == 0) {
            return CL_ERR_COBS;
        }
        size_t n = (size_t)code - 1u;
        if (n > len - r) {
            return CL_ERR_COBS;
        }
        if (n > out_cap - w) {
            return CL_ERR_OVERFLOW;
        }
        for (size_t i = 0; i < n; i++) {
            uint8_t b = in[r++];
            if (b == 0) {
                return CL_ERR_COBS;
            }
            out[w++] = b;
        }
        /* Every block except a full one, and except the last, stands for a
         * zero byte in the original data. */
        if (code != 0xFF && r < len) {
            if (w >= out_cap) {
                return CL_ERR_OVERFLOW;
            }
            out[w++] = 0;
        }
    }
    *out_len = w;
    return CL_OK;
}

/* ---- CRC -------------------------------------------------------------- */

uint16_t cl_crc16(const uint8_t *data, size_t len, uint16_t crc)
{
    /* Bitwise on purpose: 0 bytes of table. Swap in a 256-entry table if
     * the link is fast enough for this to show up in a profile. */
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            uint16_t shifted = (uint16_t)((unsigned)crc << 1);
            crc = (crc & 0x8000u) ? (uint16_t)(shifted ^ 0x1021u) : shifted;
        }
    }
    return crc;
}

/* ---- Framing ---------------------------------------------------------- */

/* The CRC goes on the wire inverted (CRC-16/GENIBUS). Without the inversion,
 * a frame whose CRC ends in 0x00 can lose that byte to a single bit flip on
 * the final COBS code byte and still check out as a valid, shorter frame:
 * feeding a CRC its own high byte always yields a low byte of zero. */
static uint16_t frame_crc(const uint8_t *payload, size_t len)
{
    return (uint16_t)(cl_crc16(payload, len, CL_CRC16_INIT) ^ CL_CRC16_XOROUT);
}

size_t cl_frame_encode(const uint8_t *payload, size_t len,
                       uint8_t *out, size_t out_cap)
{
    if (out_cap < CL_FRAME_MAX_ENCODED(len)) {
        return 0;
    }

    uint16_t crc = frame_crc(payload, len);

    cobs_enc_t e;
    enc_init(&e, out);
    for (size_t i = 0; i < len; i++) {
        enc_put(&e, payload[i]);
    }
    enc_put(&e, (uint8_t)(crc >> 8));
    enc_put(&e, (uint8_t)(crc & 0xFFu));
    size_t n = enc_finish(&e);

    out[n++] = CL_DELIMITER;
    return n;
}

void cl_rx_init(cl_rx_t *rx, uint8_t *buf, size_t cap)
{
    rx->buf = buf;
    rx->cap = cap;
    rx->len = 0;
    rx->overflow = false;
    rx->frames_ok = 0;
    rx->crc_errors = 0;
    rx->framing_errors = 0;
    rx->overflows = 0;
}

cl_rx_status_t cl_rx_push(cl_rx_t *rx, uint8_t byte,
                          const uint8_t **payload, size_t *payload_len)
{
    if (byte != CL_DELIMITER) {
        if (rx->len < rx->cap) {
            rx->buf[rx->len++] = byte;
        } else {
            rx->overflow = true;
        }
        return CL_RX_NONE;
    }

    size_t n = rx->len;
    bool overflowed = rx->overflow;
    rx->len = 0;
    rx->overflow = false;

    if (overflowed) {
        rx->overflows++;
        return CL_RX_ERR_OVERFLOW;
    }
    if (n == 0) {
        /* back-to-back delimiters: idle line, not an error */
        return CL_RX_NONE;
    }

    size_t decoded = 0;
    if (cl_cobs_decode(rx->buf, n, rx->buf, rx->cap, &decoded) != CL_OK ||
        decoded < CL_CRC_LEN) {
        rx->framing_errors++;
        return CL_RX_ERR_FRAMING;
    }

    size_t body = decoded - CL_CRC_LEN;
    uint16_t want = (uint16_t)(((uint16_t)rx->buf[body] << 8) | rx->buf[body + 1]);
    if (frame_crc(rx->buf, body) != want) {
        rx->crc_errors++;
        return CL_RX_ERR_CRC;
    }

    rx->frames_ok++;
    *payload = rx->buf;
    *payload_len = body;
    return CL_RX_FRAME;
}

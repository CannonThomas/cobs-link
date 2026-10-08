/*
 * cobs-link: framed, CRC-protected byte-stream link for microcontrollers.
 *
 * Wire format of one frame:
 *
 *     COBS( payload || CRC16_hi || CRC16_lo ) || 0x00      (CRC-16/GENIBUS)
 *
 * COBS removes every zero byte from the body, so 0x00 is an unambiguous
 * frame delimiter and a receiver can always resynchronise after noise.
 *
 * No dynamic allocation, no globals, no dependencies beyond <stdint.h>.
 */
#ifndef COBS_LINK_H
#define COBS_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_DELIMITER 0x00u
#define CL_CRC_LEN   2u

/* Worst-case COBS output size for n input bytes (delimiter not included). */
#define CL_COBS_MAX_ENCODED(n) ((n) + (n) / 254u + 1u)

/* Worst-case size of a whole frame on the wire for an n-byte payload. */
#define CL_FRAME_MAX_ENCODED(n) (CL_COBS_MAX_ENCODED((n) + CL_CRC_LEN) + 1u)

typedef enum {
    CL_OK = 0,
    CL_ERR_COBS,     /* malformed COBS data */
    CL_ERR_OVERFLOW  /* output buffer too small */
} cl_err_t;

/* ---- COBS ------------------------------------------------------------- */

/* Encode len bytes. out must hold CL_COBS_MAX_ENCODED(len) bytes.
 * Returns the encoded length. The output never contains 0x00. */
size_t cl_cobs_encode(const uint8_t *in, size_t len, uint8_t *out);

/* Decode len bytes (without the trailing delimiter).
 * Decoding in place (out == in) is supported. */
cl_err_t cl_cobs_decode(const uint8_t *in, size_t len,
                        uint8_t *out, size_t out_cap, size_t *out_len);

/* ---- CRC -------------------------------------------------------------- */

#define CL_CRC16_INIT   0xFFFFu
#define CL_CRC16_XOROUT 0xFFFFu

/* Raw CRC-16 register update: poly 0x1021, MSB first, no final XOR.
 * Pass CL_CRC16_INIT for the first chunk and the previous result after.
 * Frames carry this value XORed with CL_CRC16_XOROUT, i.e. CRC-16/GENIBUS
 * (check value 0xD64E for "123456789"). */
uint16_t cl_crc16(const uint8_t *data, size_t len, uint16_t crc);

/* ---- Framing ---------------------------------------------------------- */

/* Build a complete frame, delimiter included.
 * Returns the number of bytes written, or 0 if out_cap is smaller than
 * CL_FRAME_MAX_ENCODED(len). */
size_t cl_frame_encode(const uint8_t *payload, size_t len,
                       uint8_t *out, size_t out_cap);

typedef enum {
    CL_RX_NONE = 0,      /* frame still in progress */
    CL_RX_FRAME,         /* valid frame: *payload / *payload_len are set */
    CL_RX_ERR_CRC,       /* frame decoded but the CRC did not match */
    CL_RX_ERR_FRAMING,   /* bad COBS data or frame too short */
    CL_RX_ERR_OVERFLOW   /* frame was longer than the receive buffer */
} cl_rx_status_t;

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    bool     overflow;
    /* running counters, useful for link-quality telemetry */
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t framing_errors;
    uint32_t overflows;
} cl_rx_t;

/* buf must hold CL_FRAME_MAX_ENCODED(max_payload) - 1 bytes. */
void cl_rx_init(cl_rx_t *rx, uint8_t *buf, size_t cap);

/* Feed one received byte. Constant work per byte until a delimiter arrives,
 * so it is safe to call from a UART RX interrupt.
 *
 * On CL_RX_FRAME, *payload points into the receive buffer and stays valid
 * until the next call to cl_rx_push(). */
cl_rx_status_t cl_rx_push(cl_rx_t *rx, uint8_t byte,
                          const uint8_t **payload, size_t *payload_len);

#ifdef __cplusplus
}
#endif

#endif /* COBS_LINK_H */

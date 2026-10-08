#include "cobs_link.h"

#include <stdio.h>
#include <string.h>

static int g_checks, g_failed;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failed++;                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                  \
    } while (0)

#define RUN(fn)                                                            \
    do {                                                                   \
        int before = g_failed;                                             \
        fn();                                                              \
        printf("%-34s %s\n", #fn, g_failed == before ? "ok" : "FAILED");   \
    } while (0)

/* xorshift32: deterministic, so a failure reproduces on every machine */
static uint32_t g_rng = 0xC0B5C0B5u;

static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

#define MAX_PAYLOAD 600u

static void expect_cobs(const uint8_t *raw, size_t raw_len,
                        const uint8_t *enc, size_t enc_len)
{
    uint8_t out[CL_COBS_MAX_ENCODED(MAX_PAYLOAD)];
    uint8_t back[MAX_PAYLOAD];
    size_t back_len = 0;

    size_t n = cl_cobs_encode(raw, raw_len, out);
    CHECK(n == enc_len);
    CHECK(memcmp(out, enc, enc_len) == 0);
    CHECK(cl_cobs_decode(enc, enc_len, back, sizeof back, &back_len) == CL_OK);
    CHECK(back_len == raw_len);
    CHECK(raw_len == 0 || memcmp(back, raw, raw_len) == 0);
}

/* Vectors from the original COBS paper / Wikipedia article. */
static void test_cobs_known_vectors(void)
{
    {
        const uint8_t raw[] = {0x00}, enc[] = {0x01, 0x01};
        expect_cobs(raw, sizeof raw, enc, sizeof enc);
    }
    {
        const uint8_t raw[] = {0x00, 0x00}, enc[] = {0x01, 0x01, 0x01};
        expect_cobs(raw, sizeof raw, enc, sizeof enc);
    }
    {
        const uint8_t raw[] = {0x11, 0x22, 0x00, 0x33};
        const uint8_t enc[] = {0x03, 0x11, 0x22, 0x02, 0x33};
        expect_cobs(raw, sizeof raw, enc, sizeof enc);
    }
    {
        const uint8_t raw[] = {0x11, 0x22, 0x33, 0x44};
        const uint8_t enc[] = {0x05, 0x11, 0x22, 0x33, 0x44};
        expect_cobs(raw, sizeof raw, enc, sizeof enc);
    }
    {
        const uint8_t raw[] = {0x11, 0x00, 0x00, 0x00};
        const uint8_t enc[] = {0x02, 0x11, 0x01, 0x01, 0x01};
        expect_cobs(raw, sizeof raw, enc, sizeof enc);
    }
    {
        const uint8_t enc[] = {0x01};
        expect_cobs(NULL, 0, enc, sizeof enc);
    }
}

/* The 254-byte block boundary is where COBS implementations go wrong. */
static void test_cobs_block_boundaries(void)
{
    uint8_t raw[300], enc[310];

    /* 01..FE -> FF 01..FE */
    for (int i = 0; i < 254; i++) raw[i] = (uint8_t)(i + 1);
    enc[0] = 0xFF;
    memcpy(enc + 1, raw, 254);
    expect_cobs(raw, 254, enc, 255);

    /* 00 01..FE -> 01 FF 01..FE */
    raw[0] = 0;
    for (int i = 0; i < 254; i++) raw[i + 1] = (uint8_t)(i + 1);
    enc[0] = 0x01;
    enc[1] = 0xFF;
    memcpy(enc + 2, raw + 1, 254);
    expect_cobs(raw, 255, enc, 256);

    /* 01..FF -> FF 01..FE 02 FF */
    for (int i = 0; i < 255; i++) raw[i] = (uint8_t)(i + 1);
    enc[0] = 0xFF;
    memcpy(enc + 1, raw, 254);
    enc[255] = 0x02;
    enc[256] = 0xFF;
    expect_cobs(raw, 255, enc, 257);

    /* 01..FE 00 -> FF 01..FE 01 01 */
    for (int i = 0; i < 254; i++) raw[i] = (uint8_t)(i + 1);
    raw[254] = 0;
    enc[0] = 0xFF;
    memcpy(enc + 1, raw, 254);
    enc[255] = 0x01;
    enc[256] = 0x01;
    expect_cobs(raw, 255, enc, 257);
}

static void test_cobs_rejects_malformed(void)
{
    uint8_t out[16];
    size_t n = 0;

    const uint8_t zero_code[] = {0x02, 0x11, 0x00, 0x01};
    CHECK(cl_cobs_decode(zero_code, sizeof zero_code, out, sizeof out, &n) == CL_ERR_COBS);

    const uint8_t zero_in_block[] = {0x03, 0x11, 0x00};
    CHECK(cl_cobs_decode(zero_in_block, sizeof zero_in_block, out, sizeof out, &n) == CL_ERR_COBS);

    const uint8_t truncated[] = {0x05, 0x11, 0x22};
    CHECK(cl_cobs_decode(truncated, sizeof truncated, out, sizeof out, &n) == CL_ERR_COBS);

    const uint8_t fine[] = {0x05, 0x11, 0x22, 0x33, 0x44};
    CHECK(cl_cobs_decode(fine, sizeof fine, out, 3, &n) == CL_ERR_OVERFLOW);
}

static void test_crc16_check_value(void)
{
    /* Standard check values: raw register (CCITT-FALSE) and the inverted
     * form that goes on the wire (GENIBUS). */
    const uint8_t msg[] = "123456789";
    CHECK(cl_crc16(msg, 9, CL_CRC16_INIT) == 0x29B1);
    CHECK((cl_crc16(msg, 9, CL_CRC16_INIT) ^ CL_CRC16_XOROUT) == 0xD64E);
    CHECK(cl_crc16(NULL, 0, CL_CRC16_INIT) == 0xFFFF);

    /* chunked == one-shot */
    uint16_t crc = cl_crc16(msg, 4, CL_CRC16_INIT);
    crc = cl_crc16(msg + 4, 5, crc);
    CHECK(crc == 0x29B1);
}

static void test_frame_roundtrip_random(void)
{
    uint8_t payload[MAX_PAYLOAD];
    uint8_t wire[CL_FRAME_MAX_ENCODED(MAX_PAYLOAD)];
    uint8_t rxbuf[CL_FRAME_MAX_ENCODED(MAX_PAYLOAD)];
    cl_rx_t rx;
    cl_rx_init(&rx, rxbuf, sizeof rxbuf);

    for (int iter = 0; iter < 20000; iter++) {
        size_t len = rnd() % (MAX_PAYLOAD + 1);
        /* mix of zero-heavy, zero-free and uniform payloads */
        uint32_t mode = rnd() % 3;
        for (size_t i = 0; i < len; i++) {
            uint8_t b = (uint8_t)rnd();
            if (mode == 0 && (rnd() & 1)) b = 0;
            if (mode == 1 && b == 0) b = 1;
            payload[i] = b;
        }

        size_t n = cl_frame_encode(payload, len, wire, sizeof wire);
        CHECK(n > 0 && n <= CL_FRAME_MAX_ENCODED(len));
        CHECK(wire[n - 1] == CL_DELIMITER);
        CHECK(memchr(wire, 0, n - 1) == NULL);

        const uint8_t *got = NULL;
        size_t got_len = 0;
        for (size_t i = 0; i + 1 < n; i++) {
            CHECK(cl_rx_push(&rx, wire[i], &got, &got_len) == CL_RX_NONE);
        }
        CHECK(cl_rx_push(&rx, wire[n - 1], &got, &got_len) == CL_RX_FRAME);
        CHECK(got_len == len);
        CHECK(len == 0 || memcmp(got, payload, len) == 0);
    }
    CHECK(rx.frames_ok == 20000);
    CHECK(rx.crc_errors == 0 && rx.framing_errors == 0 && rx.overflows == 0);
}

/* Flip one bit anywhere in a frame. COBS can turn a single flipped bit into
 * a longer error burst (a damaged code byte moves a zero, or splits the
 * frame), so the CRC's single-bit guarantee does not carry over: what is
 * left is the usual 2^-16 chance for the few flips that restructure the
 * frame. Count what slips through and hold it to that order of magnitude. */
static void test_single_bit_errors_detected(void)
{
    enum { ITERS = 200000 };
    uint8_t payload[64];
    uint8_t wire[CL_FRAME_MAX_ENCODED(sizeof payload)];
    uint8_t rxbuf[CL_FRAME_MAX_ENCODED(sizeof payload)];
    cl_rx_t rx;
    cl_rx_init(&rx, rxbuf, sizeof rxbuf);
    unsigned rejected = 0, slipped = 0;

    for (int iter = 0; iter < ITERS; iter++) {
        size_t len = 1 + rnd() % sizeof payload;
        for (size_t i = 0; i < len; i++) payload[i] = (uint8_t)rnd();

        size_t n = cl_frame_encode(payload, len, wire, sizeof wire);
        size_t victim = rnd() % (n - 1); /* leave the delimiter intact */
        wire[victim] ^= (uint8_t)(1u << (rnd() % 8));

        for (size_t i = 0; i < n; i++) {
            const uint8_t *got = NULL;
            size_t got_len = 0;
            cl_rx_status_t st = cl_rx_push(&rx, wire[i], &got, &got_len);
            if (st == CL_RX_FRAME) {
                slipped++;
            } else if (st != CL_RX_NONE) {
                rejected++;
            }
        }
    }
    printf("  single-bit flips: %d injected, %u undetected\n", ITERS, slipped);
    CHECK(rejected >= ITERS - slipped);
    CHECK(slipped <= ITERS / 20000);
}

/* Regression: with a plain (non-inverted) CRC, any frame whose CRC low byte
 * is 0x00 ends in the code byte 0x01, and clearing that one bit leaves a
 * shorter frame that still passes the CRC -- the payload silently loses its
 * last byte. Hit exactly that bit on every frame that ends in 0x01. */
static void test_truncation_by_bit_flip_rejected(void)
{
    uint8_t payload[32];
    uint8_t wire[CL_FRAME_MAX_ENCODED(sizeof payload)];
    uint8_t rxbuf[CL_FRAME_MAX_ENCODED(sizeof payload)];
    cl_rx_t rx;
    cl_rx_init(&rx, rxbuf, sizeof rxbuf);
    unsigned exercised = 0;

    for (int iter = 0; iter < 200000; iter++) {
        size_t len = 1 + rnd() % sizeof payload;
        for (size_t i = 0; i < len; i++) payload[i] = (uint8_t)rnd();

        size_t n = cl_frame_encode(payload, len, wire, sizeof wire);
        if (wire[n - 2] != 0x01) continue;
        exercised++;
        wire[n - 2] = 0x00;

        for (size_t i = 0; i < n; i++) {
            const uint8_t *got = NULL;
            size_t got_len = 0;
            CHECK(cl_rx_push(&rx, wire[i], &got, &got_len) != CL_RX_FRAME);
        }
    }
    CHECK(exercised > 500);
    CHECK(rx.frames_ok == 0);
}

static void test_resync_after_garbage(void)
{
    const uint8_t payload[] = {0xDE, 0xAD, 0x00, 0xBE, 0xEF};
    uint8_t wire[CL_FRAME_MAX_ENCODED(sizeof payload)];
    uint8_t rxbuf[32];
    cl_rx_t rx;
    cl_rx_init(&rx, rxbuf, sizeof rxbuf);

    const uint8_t *got = NULL;
    size_t got_len = 0;

    /* receiver powers up in the middle of somebody else's frame */
    const uint8_t noise[] = {0x7F, 0x03, 0xAA, 0x55, 0x10};
    for (size_t i = 0; i < sizeof noise; i++) {
        CHECK(cl_rx_push(&rx, noise[i], &got, &got_len) == CL_RX_NONE);
    }
    cl_rx_status_t st = cl_rx_push(&rx, 0x00, &got, &got_len);
    CHECK(st == CL_RX_ERR_FRAMING || st == CL_RX_ERR_CRC);

    /* idle line */
    CHECK(cl_rx_push(&rx, 0x00, &got, &got_len) == CL_RX_NONE);
    CHECK(cl_rx_push(&rx, 0x00, &got, &got_len) == CL_RX_NONE);

    size_t n = cl_frame_encode(payload, sizeof payload, wire, sizeof wire);
    st = CL_RX_NONE;
    for (size_t i = 0; i < n; i++) {
        st = cl_rx_push(&rx, wire[i], &got, &got_len);
    }
    CHECK(st == CL_RX_FRAME);
    CHECK(got_len == sizeof payload);
    CHECK(memcmp(got, payload, sizeof payload) == 0);
    CHECK(rx.frames_ok == 1);
}

static void test_overflow_is_contained(void)
{
    uint8_t payload[40];
    uint8_t wire[CL_FRAME_MAX_ENCODED(sizeof payload)];
    uint8_t guarded[16 + 4];
    cl_rx_t rx;

    memset(guarded, 0xA5, sizeof guarded);
    cl_rx_init(&rx, guarded, 16);
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i + 1);

    const uint8_t *got = NULL;
    size_t got_len = 0;
    size_t n = cl_frame_encode(payload, sizeof payload, wire, sizeof wire);
    cl_rx_status_t st = CL_RX_NONE;
    for (size_t i = 0; i < n; i++) {
        st = cl_rx_push(&rx, wire[i], &got, &got_len);
    }
    CHECK(st == CL_RX_ERR_OVERFLOW);
    CHECK(rx.overflows == 1);
    for (size_t i = 16; i < sizeof guarded; i++) CHECK(guarded[i] == 0xA5);

    /* and the next frame that does fit still gets through */
    n = cl_frame_encode(payload, 8, wire, sizeof wire);
    for (size_t i = 0; i < n; i++) {
        st = cl_rx_push(&rx, wire[i], &got, &got_len);
    }
    CHECK(st == CL_RX_FRAME && got_len == 8);
}

static void test_encode_rejects_small_buffer(void)
{
    const uint8_t payload[10] = {0};
    uint8_t out[CL_FRAME_MAX_ENCODED(sizeof payload)];
    CHECK(cl_frame_encode(payload, sizeof payload, out, sizeof out - 1) == 0);
    CHECK(cl_frame_encode(payload, sizeof payload, out, sizeof out) > 0);
}

int main(void)
{
    RUN(test_cobs_known_vectors);
    RUN(test_cobs_block_boundaries);
    RUN(test_cobs_rejects_malformed);
    RUN(test_crc16_check_value);
    RUN(test_frame_roundtrip_random);
    RUN(test_single_bit_errors_detected);
    RUN(test_truncation_by_bit_flip_rejected);
    RUN(test_resync_after_garbage);
    RUN(test_overflow_is_contained);
    RUN(test_encode_rejects_small_buffer);

    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}

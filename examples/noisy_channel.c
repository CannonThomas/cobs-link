/* Push telemetry packets through a simulated UART that corrupts and drops
 * bytes, and show that every delivered packet is intact.
 *
 *   make example && ./build/noisy_channel
 */
#include "cobs_link.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t seq;
    int16_t  temperature_c10; /* 0.1 degC */
    uint16_t battery_mv;
} telemetry_t;

static uint32_t g_rng = 2463534242u;

static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

int main(void)
{
    enum { PACKETS = 100000, ERROR_ONE_IN = 400 };

    uint8_t wire[CL_FRAME_MAX_ENCODED(sizeof(telemetry_t))];
    uint8_t rxbuf[CL_FRAME_MAX_ENCODED(sizeof(telemetry_t))];
    cl_rx_t rx;
    cl_rx_init(&rx, rxbuf, sizeof rxbuf);

    unsigned delivered = 0, corrupt_delivered = 0;
    uint32_t last_seq = 0;

    for (uint32_t seq = 1; seq <= PACKETS; seq++) {
        telemetry_t t = {seq, (int16_t)(215 + (int)(rnd() % 20)),
                         (uint16_t)(3700 + rnd() % 400)};
        size_t n = cl_frame_encode((const uint8_t *)&t, sizeof t, wire, sizeof wire);

        for (size_t i = 0; i < n; i++) {
            uint8_t b = wire[i];
            uint32_t r = rnd() % ERROR_ONE_IN;
            if (r == 0) b ^= (uint8_t)(1u << (rnd() % 8)); /* bit flip */
            if (r == 1) continue;                          /* dropped byte */

            const uint8_t *payload;
            size_t len;
            if (cl_rx_push(&rx, b, &payload, &len) == CL_RX_FRAME) {
                telemetry_t got;
                if (len != sizeof got) {
                    corrupt_delivered++;
                    continue;
                }
                memcpy(&got, payload, sizeof got);
                if (got.seq <= last_seq || got.seq > PACKETS) corrupt_delivered++;
                last_seq = got.seq;
                delivered++;
            }
        }
    }

    printf("sent              %d\n", PACKETS);
    printf("delivered intact  %u (%.2f%%)\n", delivered, 100.0 * delivered / PACKETS);
    printf("rejected: crc     %u\n", (unsigned)rx.crc_errors);
    printf("rejected: framing %u\n", (unsigned)rx.framing_errors);
    printf("rejected: overflow%u\n", (unsigned)rx.overflows);
    printf("corrupt delivered %u\n", corrupt_delivered);
    return corrupt_delivered ? 1 : 0;
}

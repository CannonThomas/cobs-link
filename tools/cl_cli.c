/* Line-oriented hex front end for the library, used by tests/cross_check.py.
 *
 *   cl_cli encode   one payload per line (hex)  -> one frame per line (hex)
 *   cl_cli decode   one frame per line (hex)    -> payload (hex) or "ERR"
 */
#include "cobs_link.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MAX_PAYLOAD 4096u

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static long parse_hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (*s && *s != '\n' && *s != '\r') {
        int hi = hexval((unsigned char)s[0]);
        int lo = hi < 0 ? -1 : hexval((unsigned char)s[1]);
        if (lo < 0 || n >= cap) return -1;
        out[n++] = (uint8_t)(hi << 4 | lo);
        s += 2;
    }
    return (long)n;
}

static void print_hex(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
    putchar('\n');
}

int main(int argc, char **argv)
{
    static char line[2 * CL_FRAME_MAX_ENCODED(MAX_PAYLOAD) + 8];
    static uint8_t in[CL_FRAME_MAX_ENCODED(MAX_PAYLOAD)];
    static uint8_t out[CL_FRAME_MAX_ENCODED(MAX_PAYLOAD)];

    if (argc != 2 || (strcmp(argv[1], "encode") && strcmp(argv[1], "decode"))) {
        fprintf(stderr, "usage: %s encode|decode < hex-lines\n", argv[0]);
        return 2;
    }
    int encode = strcmp(argv[1], "encode") == 0;

    while (fgets(line, sizeof line, stdin)) {
        long n = parse_hex(line, in, encode ? MAX_PAYLOAD : sizeof in);
        if (n < 0) {
            puts("ERR");
            continue;
        }
        if (encode) {
            print_hex(out, cl_frame_encode(in, (size_t)n, out, sizeof out));
            continue;
        }

        cl_rx_t rx;
        cl_rx_init(&rx, out, sizeof out);
        const uint8_t *payload = NULL;
        size_t len = 0;
        cl_rx_status_t st = CL_RX_NONE;
        for (long i = 0; i < n && st == CL_RX_NONE; i++) {
            st = cl_rx_push(&rx, in[i], &payload, &len);
        }
        if (st == CL_RX_FRAME) {
            print_hex(payload, len);
        } else {
            puts("ERR");
        }
    }
    return 0;
}

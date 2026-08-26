#include "uart.h"
#include "xmodem.h"

#include <stdio.h>
#include <string.h>

#define SOH         0x01
#define EOT         0x04
#define ACK         0x06
#define NAK         0x15
#define CAN         0x18
#define CRC_REQUEST 0x43
#define BLOCK_SIZE  128u
#define QUEUE_SIZE  512u
#define MAX_RETRIES 16u

enum response_kind {
    RESPONSE_NONE,
    RESPONSE_BYTE,
    RESPONSE_TWO_BYTES,
    RESPONSE_BLOCK,
    RESPONSE_BLOCK_NO_SOH,
    RESPONSE_BLOCK_BAD_CRC,
    RESPONSE_PARTIAL_BLOCK
};

struct tx_step {
    unsigned char      expected;
    enum response_kind response;
    unsigned char      arg0;
    unsigned char      arg1;
};

static unsigned char         rx_queue[QUEUE_SIZE];
static unsigned int          rx_head;
static unsigned int          rx_tail;
static unsigned int          mtime;
static const struct tx_step *script;
static unsigned int          script_len;
static unsigned int          script_pos;
static const char           *test_name;
static int                   failures;
static unsigned int          tests_run;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL [%s]: %s\n", test_name, message);
    failures++;
}

static void check(int condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

static void queue_byte(unsigned char value)
{
    unsigned int next = (rx_tail + 1u) % QUEUE_SIZE;

    if (next == rx_head) {
        fail("UART receive queue overflow");
        return;
    }
    rx_queue[rx_tail] = value;
    rx_tail = next;
}

static unsigned int crc16(const unsigned char *data, unsigned int len)
{
    unsigned int crc = 0;
    unsigned int i;

    for (i = 0; i < len; i++) {
        int bit;

        crc ^= (unsigned int) data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u) != 0u
                ? (crc << 1) ^ 0x1021u
                : crc << 1;
            crc &= 0xffffu;
        }
    }
    return crc;
}

static void queue_block(unsigned char number, unsigned char value,
                        int with_soh, int bad_crc)
{
    unsigned char data[BLOCK_SIZE];
    unsigned int  crc;
    unsigned int  i;

    memset(data, value, sizeof(data));
    crc = crc16(data, BLOCK_SIZE);
    if (with_soh) {
        queue_byte(SOH);
    }
    queue_byte(number);
    queue_byte((unsigned char) (0xffu - number));
    for (i = 0; i < BLOCK_SIZE; i++) {
        queue_byte(data[i]);
    }
    if (bad_crc) {
        crc ^= 1u;
    }
    queue_byte((unsigned char) (crc >> 8));
    queue_byte((unsigned char) crc);
}

static void queue_response(const struct tx_step *step)
{
    unsigned int i;

    switch (step->response) {
    case RESPONSE_NONE:
        break;
    case RESPONSE_BYTE:
        queue_byte(step->arg0);
        break;
    case RESPONSE_TWO_BYTES:
        queue_byte(step->arg0);
        queue_byte(step->arg1);
        break;
    case RESPONSE_BLOCK:
        queue_block(step->arg0, step->arg1, 1, 0);
        break;
    case RESPONSE_BLOCK_NO_SOH:
        queue_block(step->arg0, step->arg1, 0, 0);
        break;
    case RESPONSE_BLOCK_BAD_CRC:
        queue_block(step->arg0, step->arg1, 1, 1);
        break;
    case RESPONSE_PARTIAL_BLOCK:
        queue_byte(SOH);
        queue_byte(step->arg0);
        queue_byte((unsigned char) (0xffu - step->arg0));
        for (i = 0; i < 8u; i++) {
            queue_byte(step->arg1);
        }
        break;
    }
}

unsigned int mmio_read(unsigned int addr)
{
    if (addr == MMIO_UART_RX) {
        unsigned char value;

        if (rx_head == rx_tail) {
            return 0;
        }
        value = rx_queue[rx_head];
        rx_head = (rx_head + 1u) % QUEUE_SIZE;
        return UART_RX_VALID | value;
    }
    if (addr == MMIO_MTIME) {
        /* Each empty poll advances beyond xmodem.c's one-second timeout. */
        mtime += 27000001u;
        return mtime;
    }
    fail("unexpected MMIO read");
    return 0;
}

void put_char(int c)
{
    const struct tx_step *step;
    unsigned char         value = (unsigned char) c;

    if (script_pos >= script_len) {
        fail("unexpected UART transmit");
        return;
    }
    step = script + script_pos++;
    if (value != step->expected) {
        fprintf(stderr,
                "FAIL [%s]: UART transmit mismatch: got %02x, expected %02x\n",
                test_name, value, step->expected);
        failures++;
    }
    queue_response(step);
}

static void run_case(const char *name, const struct tx_step *steps,
                     unsigned int step_count, unsigned int max_len,
                     int expected_rc, unsigned int expected_len,
                     const unsigned char *expected_fills,
                     unsigned int expected_blocks, unsigned int initial_mtime,
                     int omit_out_len)
{
    unsigned char output[3u * BLOCK_SIZE];
    unsigned int  received = 0xdeadbeefu;
    unsigned int  i;
    int           rc;

    test_name  = name;
    rx_head    = 0;
    rx_tail    = 0;
    mtime      = initial_mtime;
    script     = steps;
    script_len = step_count;
    script_pos = 0;
    memset(output, 0xa5, sizeof(output));

    rc = xmodem_receive(output, max_len,
                        omit_out_len ? 0 : &received);

    check(rc == expected_rc, "return code mismatch");
    check(script_pos == script_len, "UART script did not complete");
    check(rx_head == rx_tail, "UART receive queue was not drained");
    if (expected_rc == XMODEM_OK && !omit_out_len) {
        check(received == expected_len, "received length mismatch");
    }
    if (expected_rc == XMODEM_OK) {
        check(expected_len == expected_blocks * BLOCK_SIZE,
              "test payload description mismatch");
        for (i = 0; i < expected_blocks; i++) {
            unsigned int j;

            for (j = 0; j < BLOCK_SIZE; j++) {
                if (output[i * BLOCK_SIZE + j] != expected_fills[i]) {
                    fail("received payload mismatch");
                    i = expected_blocks;
                    break;
                }
            }
        }
    }
    tests_run++;
}

int main(void)
{
    static const unsigned char one_block[] = {0x31u};
    static const unsigned char two_blocks[] = {0x31u, 0x42u};
    static const struct tx_step normal[] = {
        {CRC_REQUEST, RESPONSE_BLOCK, 1u, 0x31u},
        {ACK, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step duplicate[] = {
        {CRC_REQUEST, RESPONSE_BLOCK, 1u, 0x31u},
        {ACK, RESPONSE_BLOCK, 1u, 0x31u},
        {ACK, RESPONSE_BLOCK, 2u, 0x42u},
        {ACK, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step bad_crc[] = {
        {CRC_REQUEST, RESPONSE_BLOCK_BAD_CRC, 1u, 0x31u},
        {NAK, RESPONSE_BLOCK, 1u, 0x31u},
        {ACK, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step lost_soh[] = {
        {CRC_REQUEST, RESPONSE_BLOCK, 1u, 0x31u},
        {ACK, RESPONSE_BLOCK_NO_SOH, 2u, 0x42u},
        {NAK, RESPONSE_BLOCK, 2u, 0x42u},
        {ACK, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step partial_block[] = {
        {CRC_REQUEST, RESPONSE_PARTIAL_BLOCK, 1u, 0x31u},
        {NAK, RESPONSE_BLOCK, 1u, 0x31u},
        {ACK, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step out_of_order[] = {
        {CRC_REQUEST, RESPONSE_BLOCK, 2u, 0x42u},
        {NAK, RESPONSE_BLOCK, 1u, 0x31u},
        {ACK, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step startup_noise[] = {
        {CRC_REQUEST, RESPONSE_BYTE, 0x7fu, 0u},
        {CRC_REQUEST, RESPONSE_BLOCK, 1u, 0x31u},
        {ACK, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step cancel[] = {
        {CRC_REQUEST, RESPONSE_TWO_BYTES, CAN, CAN}
    };
    static const struct tx_step overflow[] = {
        {CRC_REQUEST, RESPONSE_BLOCK, 1u, 0x31u},
        {CAN, RESPONSE_NONE, 0u, 0u},
        {CAN, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step timeout_then_eot[] = {
        {CRC_REQUEST, RESPONSE_NONE, 0u, 0u},
        {CRC_REQUEST, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    static const struct tx_step empty[] = {
        {CRC_REQUEST, RESPONSE_BYTE, EOT, 0u},
        {ACK, RESPONSE_NONE, 0u, 0u}
    };
    struct tx_step timeout[MAX_RETRIES];
    unsigned int i;

    for (i = 0; i < MAX_RETRIES; i++) {
        timeout[i].expected = CRC_REQUEST;
        timeout[i].response = RESPONSE_NONE;
        timeout[i].arg0     = 0;
        timeout[i].arg1     = 0;
    }

    run_case("normal", normal, sizeof(normal) / sizeof(normal[0]),
             BLOCK_SIZE, XMODEM_OK, BLOCK_SIZE, one_block, 1u, 0u, 0);
    run_case("duplicate block", duplicate,
             sizeof(duplicate) / sizeof(duplicate[0]),
             2u * BLOCK_SIZE, XMODEM_OK, 2u * BLOCK_SIZE,
             two_blocks, 2u, 0u, 0);
    run_case("bad CRC retry", bad_crc, sizeof(bad_crc) / sizeof(bad_crc[0]),
             BLOCK_SIZE, XMODEM_OK, BLOCK_SIZE, one_block, 1u, 0u, 0);
    run_case("lost SOH retry", lost_soh,
             sizeof(lost_soh) / sizeof(lost_soh[0]),
             2u * BLOCK_SIZE, XMODEM_OK, 2u * BLOCK_SIZE,
             two_blocks, 2u, 0u, 0);
    run_case("partial block timeout", partial_block,
             sizeof(partial_block) / sizeof(partial_block[0]),
             BLOCK_SIZE, XMODEM_OK, BLOCK_SIZE, one_block, 1u, 0u, 0);
    run_case("out of order retry", out_of_order,
             sizeof(out_of_order) / sizeof(out_of_order[0]),
             BLOCK_SIZE, XMODEM_OK, BLOCK_SIZE, one_block, 1u, 0u, 0);
    run_case("startup noise", startup_noise,
             sizeof(startup_noise) / sizeof(startup_noise[0]),
             BLOCK_SIZE, XMODEM_OK, BLOCK_SIZE, one_block, 1u, 0u, 0);
    run_case("sender cancel", cancel, sizeof(cancel) / sizeof(cancel[0]),
             BLOCK_SIZE, XMODEM_CANCEL, 0u, 0, 0u, 0u, 0);
    run_case("destination overflow", overflow,
             sizeof(overflow) / sizeof(overflow[0]),
             BLOCK_SIZE - 1u, XMODEM_OVERFLOW, 0u, 0, 0u, 0u, 0);
    run_case("timeout across timer wrap", timeout_then_eot,
             sizeof(timeout_then_eot) / sizeof(timeout_then_eot[0]),
             0u, XMODEM_OK, 0u, 0, 0u, 0xfffffff0u, 0);
    run_case("null out length", empty, sizeof(empty) / sizeof(empty[0]),
             0u, XMODEM_OK, 0u, 0, 0u, 0u, 1);
    run_case("retry limit", timeout, MAX_RETRIES,
             BLOCK_SIZE, XMODEM_TIMEOUT, 0u, 0, 0u, 0u, 0);

    if (failures != 0) {
        fprintf(stderr, "xmodem host tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("xmodem host tests: %u passed\n", tests_run);
    return 0;
}

#include "dominoex.h"
#include "config.h"
#include "dominovar.h"

int dominoex_tx_nibbles(unsigned char c, int secondary, uint8_t *out)
{
    unsigned char *code = dominoex_varienc(c, secondary);
    out[0] = code[0];
    int n = 1;
    for (int i = 1; i < 3; i++) {
        if (code[i] & 0x8) {
            out[n++] = code[i];
        } else {
            break;
        }
    }
    return n;
}

int dominoex_ifk_forward(int prev_tone, int nib)
{
    return (prev_tone + 2 + (nib & 0xF)) % NUM_TONES;
}

static int r_symcounter;
static int r_symbolbuf[MAX_VARICODE_LEN];

void dominoex_rx_reset(void)
{
    r_symcounter = 0;
    for (int i = 0; i < MAX_VARICODE_LEN; i++) {
        r_symbolbuf[i] = 0;
    }
}

int dominoex_rx_nibble(int nib, int *out_val)
{
    int got = 0;
    if (!(nib & 0x8)) {
        if (r_symcounter <= MAX_VARICODE_LEN) {
            int sym = 0;
            for (int i = 0; i < r_symcounter; i++) {
                sym |= r_symbolbuf[i] << (4 * i);
            }
            *out_val = dominoex_varidec((unsigned)sym & 0xFFF);
            got = 1;
        }
        r_symcounter = 0;
    }
    for (int i = MAX_VARICODE_LEN - 1; i > 0; i--) {
        r_symbolbuf[i] = r_symbolbuf[i - 1];
    }
    r_symbolbuf[0] = nib & 0xF;
    r_symcounter++;
    if (r_symcounter > MAX_VARICODE_LEN + 1) {
        r_symcounter = MAX_VARICODE_LEN + 1;
    }
    return got;
}

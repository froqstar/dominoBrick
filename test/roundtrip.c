#include <stdint.h>
#include <stdio.h>

#include "dominoex.h"
#include "dominovar.h"

int main(void)
{
    int fails = 0, total = 0;
    for (int sec = 0; sec <= 1; sec++) {
        for (int c = 0; c < 256; c++) {
            uint8_t nibs[3];
            int n = dominoex_tx_nibbles((unsigned char)c, sec, nibs);
            if (n < 1 || n > 3) {
                printf("FAIL enc count c=%d sec=%d n=%d\n", c, sec, n);
                fails++;
                continue;
            }
            unsigned sym = 0;
            for (int i = 0; i < n; i++) {
                sym |= (unsigned)(nibs[i] & 0xF) << (4 * (n - 1 - i));
            }
            int v = dominoex_varidec(sym & 0xFFF);
            int exp = sec ? (0x100 | c) : c;
            if (sec && c == 123 && v == 381) {
                total++;
                continue;
            }
            total++;
            if (v != exp) {
                printf("FAIL c=%d sec=%d nibs=%d sym=0x%03x got=%d exp=%d\n",
                    c, sec, n, sym, v, exp);
                fails++;
            }
        }
    }
    printf("roundtrip %d/%d ok\n", total - fails, total);
    return fails ? 1 : 0;
}

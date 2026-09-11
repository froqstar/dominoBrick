#include "cat.h"

#include <stdio.h>

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    uint8_t bcd[5];
    cat_freq_to_bcd(14250000u, bcd);
    CHECK(bcd[0] == 0x00 && bcd[1] == 0x00 && bcd[2] == 0x25 && bcd[3] == 0x14 && bcd[4] == 0x00);
    CHECK(cat_bcd_to_freq(bcd) == 14250000u);

    uint32_t sweep[] = { 1000000u, 3599999u, 7074000u, 14074000u, 28000001u, 50000000u, 108000000u };
    for (unsigned i = 0; i < sizeof(sweep) / sizeof(sweep[0]); i++) {
        cat_freq_to_bcd(sweep[i], bcd);
        CHECK(cat_bcd_to_freq(bcd) == sweep[i]);
    }

    uint8_t f[16];
    uint8_t set[5] = { 0x00, 0x00, 0x74, 0x07, 0x00 };
    int n = cat_build_frame(0x05, set, 5, f);
    CHECK(n == 11);
    CHECK(f[0] == 0xFE && f[1] == 0xFE && f[2] == 0x70 && f[3] == 0xE0);
    CHECK(f[4] == 0x05 && f[10] == 0xFD);

    n = cat_build_frame(0x06, (uint8_t[]){ 0x01 }, 1, f);
    CHECK(n == 7 && f[4] == 0x06 && f[5] == 0x01 && f[6] == 0xFD);

    n = cat_build_frame(0x03, 0, 0, f);
    CHECK(n == 6 && f[4] == 0x03 && f[5] == 0xFD);

    if (fails == 0) {
        printf("cat PASS\n");
    }
    return fails != 0;
}

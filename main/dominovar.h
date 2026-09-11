#pragma once

#define MAX_VARICODE_LEN 3

unsigned char *dominoex_varienc(unsigned char c, int secondary);
int dominoex_varidec(unsigned int symbol);

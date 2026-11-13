#pragma once
#include <stdint.h>
void crypt5881_init(uint32_t key, uint16_t (*rd)(uint32_t addr));
void crypt5881_addrlo_w(uint16_t d);
void crypt5881_addrhi_w(uint16_t d);
void crypt5881_subkey_le_w(uint16_t d);
uint16_t crypt5881_ready_r(void);
uint16_t crypt5881_decrypt_le_r(void);

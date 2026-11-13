#pragma once
#include <stddef.h>
int rom_open(const char *path);
size_t rom_read(const char *dir, const char *name, unsigned char *dst, size_t sz);

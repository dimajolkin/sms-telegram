#pragma once

#include <stddef.h>

bool wifiConnect();
void wifiLocalIP(char *out, size_t out_len);
bool ntpSync();

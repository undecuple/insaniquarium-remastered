// Version-number comparison for the settings migration (core.cpp) and the host tests (hosttests/).
#pragma once
#include <stdio.h>
#include <string>

inline bool VersionLess(const std::string& a, const std::string& b)
{
    int x[3] = {}, y[3] = {};
    sscanf(a.c_str(), "%d.%d.%d", &x[0], &x[1], &x[2]);
    sscanf(b.c_str(), "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] < y[i];
    return false;
}

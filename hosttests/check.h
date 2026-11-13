// A tiny test harness for the host tests: CHECK(cond) counts failures and prints where; main() returns Done().
#pragma once
#include <stdio.h>

static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static int Done(const char* name)
{
    printf("%s: %d checks, %d failed\n", name, checks, failures);
    return failures ? 1 : 0;
}

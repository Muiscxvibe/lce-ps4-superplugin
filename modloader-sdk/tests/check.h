// Minimal test harness: CHECK records failures and continues; main returns the count.
#pragma once
#include <stdio.h>
static int g_checks, g_failures;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { ++g_failures; fprintf(stderr, "%s:%d: CHECK_EQ failed: %s=%lld %s=%lld\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)
#define TEST_MAIN_END(name) do { printf("%s: %d checks, %d failures\n", name, g_checks, g_failures); return g_failures ? 1 : 0; } while (0)

#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
#define TEST_MAIN_END() do { if (g_failures) { std::printf("%d failure(s)\n", g_failures); return 1; } std::printf("OK\n"); return 0; } while (0)

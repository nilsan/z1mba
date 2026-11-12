#pragma once
// Tiny test helpers, no framework needed. Run everything: tests/run.sh
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                 \
    }                                                             \
  } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))
#define DONE(name)                                                     \
  do {                                                                 \
    if (failures) std::printf("%s: %d failure(s)\n", name, failures);  \
    else std::printf("%s ok\n", name);                                 \
    return failures ? 1 : 0;                                           \
  } while (0)

// Tiny test helpers for the host tests (built with MSVC by tools/test.ps1).
#pragma once
#include <cmath>
#include <cstdio>

inline int& checkCount() { static int n = 0; return n; }
inline int& checkFails() { static int n = 0; return n; }

#define CHECK(cond)                                                              \
    do {                                                                         \
        checkCount()++;                                                          \
        if (!(cond)) {                                                           \
            checkFails()++;                                                      \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        checkCount()++;                                                                            \
        double va = (a), vb = (b);                                                                 \
        if (std::fabs(va - vb) > (eps)) {                                                          \
            checkFails()++;                                                                        \
            std::printf("%s:%d: CHECK_NEAR failed: %s = %g, want %g\n", __FILE__, __LINE__, #a, va, vb); \
        }                                                                                          \
    } while (0)

inline int checkReport() {
    std::printf("%d checks, %d failed\n", checkCount(), checkFails());
    return checkFails();
}

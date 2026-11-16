#include <cstdio>

#include "backViewEcst.h"

extern "C" void app_main(void)
{
    backViewEcst C;
    if (!C.go()) {
        std::printf("BVE: go failed\n");
    }
}

#pragma once

#include <fnd/Util.h>

namespace engine
{

void RandomWorkload(int microsecond, int random_percent) {
    Time start = Clock::now();
    while (true) {
        Time now = Clock::now();
        int64_t us = to_us(now - start);
        if (us >= microsecond) {
            return;
        }
    }
}

}
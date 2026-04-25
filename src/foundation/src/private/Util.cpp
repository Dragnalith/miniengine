#pragma once

#include <fnd/Util.h>

namespace migi
{

void RandomWorkload(int microsecond, int random_percent) {
    TimePoint start = TimePoint::Now();
    while (true) {
        TimePoint now = TimePoint::Now();
        int64_t us = (now - start).ToMicroseconds();
        if (us >= microsecond) {
            return;
        }
    }
}

}

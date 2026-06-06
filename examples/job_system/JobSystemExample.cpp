#include <fnd/Job.h>
#include <fnd/Log.h>
#include <fnd/MigiMain.h>

#include <format>

namespace
{
constexpr int kParentJobCount = 3;
constexpr int kChildJobCount = 4;
}

void MigiMain()
{
    MIGI_LOG_INFO("Job system example starting");

    migi::JobCounter parentCounter;
    for (int parent = 0; parent < kParentJobCount; ++parent)
    {
        migi::Job::Dispatch("Parent Job", parentCounter, [parent] {
            MIGI_LOG_INFO(std::format("Parent {} started", parent).c_str());

            migi::JobCounter childCounter;
            for (int child = 0; child < kChildJobCount; ++child)
            {
                migi::Job::Dispatch("Child Job", childCounter, [parent, child] {
                    MIGI_LOG_INFO(std::format("  Parent {} child {} running", parent, child).c_str());
                });
            }

            migi::Job::Wait(childCounter);
            MIGI_LOG_INFO(std::format("Parent {} finished (all {} children done)", parent, kChildJobCount).c_str());
        });
    }

    migi::Job::Wait(parentCounter);
    MIGI_LOG_INFO("Job system example finished: all jobs completed");
}

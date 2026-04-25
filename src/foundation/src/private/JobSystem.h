#pragma once

#include <functional>

namespace migi
{


struct JobSystem
{
	static void Start(std::function<void()> mainJob);
};

}
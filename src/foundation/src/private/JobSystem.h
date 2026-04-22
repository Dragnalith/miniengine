#pragma once

#include <functional>

namespace engine
{


struct JobSystem
{
	static void Start(std::function<void()> mainJob);
};

}
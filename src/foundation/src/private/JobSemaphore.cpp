#include <fnd/JobSemaphore.h>

namespace engine
{

JobSemaphore::JobSemaphore() {
	m_event.Set(1);
}
int JobSemaphore::Acquire() {
	while (true) {
		{
			std::scoped_lock<SpinLock> lock(m_lock);
			if (m_counter > 0) {
				m_counter -= 1;
				ASSERT_MSG(m_counter >= 0, "JobSemaphore token counter should not be negative");
				return m_counter;
			}
			m_event.Set(1);
		}
#if 1 // Yield or not
		Job::Wait(m_event, 0);
#endif
	}
}
int JobSemaphore::Release() {
	std::scoped_lock<SpinLock> lock(m_lock);
	m_counter += 1;
	m_event.Set(0);

	return m_counter;
}


}
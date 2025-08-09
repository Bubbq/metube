#include "timer.h"

#include <time.h>

void timer_start(Timer* timer, const size_t lifetime) 
{
    if (timer == NULL) return;

	timer->start_time = (size_t) time(NULL);
	timer->lifetime = lifetime;
}

bool timer_is_done(Timer timer)
{ 
    const size_t seconds_elapsed = (size_t) (time(NULL) - timer.start_time);
    
    return (seconds_elapsed >= timer.lifetime);
} 
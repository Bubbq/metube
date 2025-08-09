#pragma once

#include <stdlib.h>
#include <stdbool.h>

typedef struct
{
    size_t start_time;
	size_t lifetime; // in seconds
} Timer;

void timer_start(Timer* timer, const size_t lifetime);
bool timer_is_done(Timer timer);
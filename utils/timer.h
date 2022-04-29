#pragma once

#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>

class timer {
public:
    static uint64_t get_usec() {
        struct timespec tp;
        /* POSIX.1-2008: Applications should use the clock_gettime() function
           instead of the obsolescent gettimeofday() function. */
        /* NOTE: The clock_gettime() function is only available on Linux.
           The mach_absolute_time() function is an alternative on OSX. */
        clock_gettime(CLOCK_MONOTONIC, &tp);
        return ((tp.tv_sec * 1000 * 1000) + (tp.tv_nsec / 1000));
    }
};


int cmp_timespec(const struct timespec &a, const struct timespec &b) {
	if(a.tv_sec > b.tv_sec)
		return 1;
	else if(a.tv_sec < b.tv_sec)
		return -1;
	else {
		if(a.tv_nsec > b.tv_nsec)
			return 1;
		else if(a.tv_nsec < b.tv_nsec)
			return -1;
		else
			return 0;
	}
}

void add_timespec(const struct timespec &a, int b, struct timespec *result) {
	// convert to millisec, add timeout, convert back
	result->tv_sec = a.tv_sec + b/1000;
	result->tv_nsec = a.tv_nsec + (b % 1000) * 1000000;
	VERIFY(result->tv_nsec >= 0);
	while (result->tv_nsec > 1000000000){
		result->tv_sec++;
		result->tv_nsec-=1000000000;
	}
}

int diff_timespec(const struct timespec &end, const struct timespec &start) {
	int diff = (end.tv_sec > start.tv_sec)?(end.tv_sec-start.tv_sec)*1000:0;
	VERIFY(diff || end.tv_sec == start.tv_sec);
	if(end.tv_nsec > start.tv_nsec){
		diff += (end.tv_nsec-start.tv_nsec)/1000000;
	} else {
		diff -= (start.tv_nsec-end.tv_nsec)/1000000;
	}
	return diff;
}

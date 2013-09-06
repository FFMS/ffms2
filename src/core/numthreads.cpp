//  Copyright (c) 2011 Alexander Smith
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define MAX_NUM_THREADS 16 //libav currently has bugs with > 16 threads
#include "numthreads.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef __CYGWIN__
#include <sys/unistd.h>
#endif
#ifdef __linux__
#include <sched.h>
#endif
#if defined(__APPLE__) || defined(__FREEBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif
#ifdef __OPENBSD__
#include <sys/param.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#endif

int GetNumberOfLogicalCPUs() {
	int threads = 1;

#ifdef _WIN32
	SYSTEM_INFO SI;
	GetSystemInfo(&SI);
	threads = SI.dwNumberOfProcessors;

#elif defined(__CYGWIN__)
	threads = sysconf(_SC_NPROCESSORS_ONLN);

#elif defined(__linux__)
	cpu_set_t proc_affinity;
	if (!sched_getaffinity(0, sizeof(proc_affinity), &proc_affinity))
		threads = CPU_COUNT(&proc_affinity);

#elif defined(__APPLE__) || defined(__FREEBSD__) || defined(__OPENBSD__)
	int num_cpu;
	size_t length = sizeof(num_cpu);
#ifdef __OPENBSD__
	int mib[2] = {CTL_HW, HW_NCPU};
	if (!sysctl(mib, 2, &num_cpu, &length, NULL, 0))
#else
	if (!sysctlbyname("hw.ncpu", &num_cpu, &length, NULL, 0))
#endif
		threads = num_cpu;
#endif

	if (threads > MAX_NUM_THREADS)
		return MAX_NUM_THREADS;
	else
		return threads;
}

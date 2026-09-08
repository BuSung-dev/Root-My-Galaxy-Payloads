#pragma once

#define _GNU_SOURCE
#include <sched.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static inline size_t rdtsc_begin(void)
{
    unsigned long long vct;
    asm volatile("isb" ::: "memory");
    asm volatile("mrs %0, cntvct_el0" : "=r"(vct));
    asm volatile("isb" ::: "memory");
    return (size_t)vct;
}

static inline size_t rdtsc_end(void)
{
    unsigned long long vct;
    asm volatile("isb" ::: "memory");
    asm volatile("mrs %0, cntvct_el0" : "=r"(vct));
    asm volatile("isb" ::: "memory");
    return (size_t)vct;
}

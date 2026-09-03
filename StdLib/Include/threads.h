// partial C11 Threads. support

#pragma once

#include <sys/EfiCdefs.h>

#include <sys/time.h>
#include <time.h>

// only mutexes implemented
// see: https://github.com/esmil/musl/blob/master/include/threads.h
// and: https://en.cppreference.com/c/header/threads
// and: https://www.sourceware.org/glibc/manual/2.44/html_node/ISO-C-Mutexes.html

__BEGIN_DECLS
enum {
    mtx_plain = 0,
    mtx_recursive = 1,
    mtx_timed = 2,
};


enum {
    thrd_success = 0,
    thrd_busy = 1,
    thrd_error = 2,
    thrd_nomem = 3,
    thrd_timedout = 4,
};


typedef struct mtx_t_impl mtx_t;

int mtx_init(mtx_t* mtx, int type);
void mtx_destroy(mtx_t* mtx);

int mtx_lock(mtx_t* mtx);
int mtx_timedlock(mtx_t* __restrict mtx, const struct timespec* __restrict ts);
int mtx_trylock(mtx_t* mtx);
int mtx_unlock(mtx_t* mtx);

#ifndef LIBC_USE_MUTEX_TYPE
#define LIBC_USE_MUTEX_TYPE 1
#endif

#if LIBC_USE_MUTEX_TYPE == 0
#error "TODO"
#elif LIBC_USE_MUTEX_TYPE == 1
#define __LIBC_IMPL_MUTEX_TYPE __Libc_Impl_Mutex_atomic
#define __LIBC_IMPL_MUTEX_PREFIX __Libc_Impl_Mutex_atomic_fn_
#else
#error "Invalid LIBC_USE_MUTEX_TYPE value"
#endif

typedef struct {
    volatile UINT32 locked;
} __Libc_Impl_Mutex_atomic;


struct mtx_t_impl {
    __LIBC_IMPL_MUTEX_TYPE value;
};

#define MTX_T_MEMBER_TYPE __LIBC_IMPL_MUTEX_TYPE

#define MTX_T_STATIC_INITIALIZER ((mtx_t) { .value = ((__LIBC_IMPL_MUTEX_TYPE) { 0 }) })

__END_DECLS

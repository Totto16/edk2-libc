#include <threads.h>


#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/SynchronizationLib.h>

#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define CONCAT_IMPL(a, b) a##b

#define IMPL_MUTEX_CALL(name) CONCAT(__LIBC_IMPL_MUTEX_PREFIX, name)


static void __Libc_Impl_Mutex_atomic_fn_init(__Libc_Impl_Mutex_atomic* value) {
    *value = (__Libc_Impl_Mutex_atomic){ .locked = 0 };
}

static void __Libc_Impl_Mutex_atomic_fn_destroy(const __Libc_Impl_Mutex_atomic* value) {
    ASSERT(value->locked == 0);
}

static void __Libc_Impl_Mutex_atomic_fn_lock(__Libc_Impl_Mutex_atomic* value) {
    while (InterlockedCompareExchange32(
                   &(value->locked),
                   0, // Compare: unlocked
                   1  // Exchange: locked
           )
           != 0) {
        //
        // Someone else owns it.
        //
        CpuPause();
    }
}

static void __Libc_Impl_Mutex_atomic_fn_unlock(__Libc_Impl_Mutex_atomic* value) {
    UINT32 result = InterlockedCompareExchange32(
            &(value->locked),
            1, // Compare: locked
            0  // Exchange: unlocked
    );
    ASSERT(result == 1);
}

// impl

int mtx_init(mtx_t* mtx, int type) {

    if (type != mtx_plain) {
        // only support a plain mutex for now
        return thrd_error;
    }

    if (mtx == NULL) {
        return thrd_error;
    }

    IMPL_MUTEX_CALL(init)((__LIBC_IMPL_MUTEX_TYPE*) (&(mtx->value)));


    return thrd_success;
}

void mtx_destroy(mtx_t* mtx) {
    if (mtx == NULL) {
        return;
    }

    IMPL_MUTEX_CALL(destroy)((__LIBC_IMPL_MUTEX_TYPE*) (&(mtx->value)));
}

int mtx_lock(mtx_t* mtx) {
    if (mtx == NULL) {
        return thrd_error;
    }

    IMPL_MUTEX_CALL(lock)((__LIBC_IMPL_MUTEX_TYPE*) (&(mtx->value)));
    return thrd_success;
}

int mtx_timedlock(mtx_t* restrict mtx, const struct timespec* restrict ts) {
    // not implemented yet
    return thrd_error;
}

int mtx_trylock(mtx_t* mtx) {
    // not implemented yet
    return thrd_error;
}

int mtx_unlock(mtx_t* mtx) {
    if (mtx == NULL) {
        return thrd_error;
    }

    IMPL_MUTEX_CALL(unlock)((__LIBC_IMPL_MUTEX_TYPE*) (&(mtx->value)));
    return thrd_success;
}

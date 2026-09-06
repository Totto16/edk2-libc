#include <stdlib.h>
#include <sys/threads.h>
#include <threads.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/SynchronizationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>

#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define CONCAT_IMPL(a, b) a##b

#define IMPL_MUTEX_PLAIN_CALL(name) CONCAT(__LIBC_IMPL_MUTEX_PREFIX, name)
#define IMPL_MUTEX_REC_CALL(name) CONCAT(__Libc_Impl_Mutex_recursive_fn_, name)


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

[[nodiscard]] static int __Libc_Impl_Mutex_atomic_fn_trylock(__Libc_Impl_Mutex_atomic* value) {
    UINT32 result = InterlockedCompareExchange32(
            &(value->locked),
            0, // Compare: unlocked
            1  // Exchange: locked
    );

    if (result != 0) {
        return thrd_busy;
    }

    return thrd_success;
}

static void __Libc_Impl_Mutex_atomic_fn_unlock(__Libc_Impl_Mutex_atomic* value) {
    UINT32 result = InterlockedCompareExchange32(
            &(value->locked),
            1, // Compare: locked
            0  // Exchange: unlocked
    );
    ASSERT(result == 1);
}

#if LIBC_USE_MUTEX_TYPE == 0
#error "TODO"

//TODO use the spinlock

// #define SPIN_LOCK_ACQUIRE(lock) ASSERT(AcquireSpinLock(lock) != NULL)
// #define SPIN_LOCK_RELEASE(lock) ASSERT(ReleaseSpinLock(lock) != NULL)

//   SPIN_LOCK * spin_lock = InitializeSpinLock(&MemorySpinLock);

#endif


static void __Libc_Impl_Mutex_recursive_fn_init(__Libc_Impl_Mutex_recursive* value) {
    *value = (__Libc_Impl_Mutex_recursive){
        .mutex = {},
        .state = (__Libc_Impl_Mutex_recursive_state){ .locked = false, .amount = 0, .thread_id = 0 }
    };

    IMPL_MUTEX_PLAIN_CALL(init)(&(value->mutex));
}


static void __Libc_Impl_Mutex_recursive_fn_destroy(const __Libc_Impl_Mutex_recursive* value) {
    IMPL_MUTEX_PLAIN_CALL(destroy)(&(value->mutex));
    ASSERT(!value->state.locked);
}

static void __Libc_Impl_Mutex_recursive_fn_lock(__Libc_Impl_Mutex_recursive* value) {
    while (true) {
        IMPL_MUTEX_PLAIN_CALL(lock)(&(value->mutex));

#define RETURN                                      \
    IMPL_MUTEX_PLAIN_CALL(unlock)(&(value->mutex)); \
    return

        UINTN current_thread = efi_thread_id();

        if (!value->state.locked) {

            // we can lock the mutex the first time
            value->state =
                    (__Libc_Impl_Mutex_recursive_state){ .locked = true, .amount = 1, .thread_id = current_thread };

            RETURN;
        }

        // check if it is locked by the current thread

        if (value->state.thread_id == current_thread) {
            // if it is, we increment the amount and succeed
            (value->state.amount)++;
            RETURN;
        }

        //otherwise we unlock the mutex and redo the same thing again, until one condition is met
        IMPL_MUTEX_PLAIN_CALL(unlock)(&(value->mutex));

        CpuPause();
    }
}
#undef RETURN

[[nodiscard]] static int __Libc_Impl_Mutex_recursive_fn_trylock(__Libc_Impl_Mutex_recursive* value) {
    int try_lock_result = IMPL_MUTEX_PLAIN_CALL(trylock)(&(value->mutex));

    if (try_lock_result != thrd_success) {
        return thrd_busy;
    }

#define RETURN(ret)                                 \
    IMPL_MUTEX_PLAIN_CALL(unlock)(&(value->mutex)); \
    return ret

    UINTN current_thread = efi_thread_id();

    if (!value->state.locked) {

        // we can lock the mutex the first time
        value->state = (__Libc_Impl_Mutex_recursive_state){ .locked = true, .amount = 1, .thread_id = current_thread };

        RETURN(thrd_success);
    }

    // check if it is locked by the current thread

    if (value->state.thread_id == current_thread) {
        // if it is, we increment the amount and succeed
        (value->state.amount)++;
        RETURN(thrd_success);
    }

    //otherwise we unlock the mutex and say it is busy
    RETURN(thrd_busy);
}
#undef RETURN


static void __Libc_Impl_Mutex_recursive_fn_unlock(__Libc_Impl_Mutex_recursive* value) {
    IMPL_MUTEX_PLAIN_CALL(lock)(&(value->mutex));

#define RETURN                                      \
    IMPL_MUTEX_PLAIN_CALL(unlock)(&(value->mutex)); \
    return

    UINTN current_thread = efi_thread_id();

    ASSERT(value->state.locked);
    ASSERT(value->state.thread_id == current_thread);

    // decremrement the amount
    (value->state.amount)--;

    // unlock if the amount == 0
    if (value->state.amount == 0) {
        value->state = (__Libc_Impl_Mutex_recursive_state){ .locked = false, .amount = 0, .thread_id = 0 };
    }


    RETURN;
}
#undef RETURN


int mtx_init_plain(mtx_t* mtx) {
    mtx->type = __Libc_Impl_Mutex_type_plain;
    IMPL_MUTEX_PLAIN_CALL(init)(&(mtx->data.plain));
    return thrd_success;
}


int mtx_init_recursive(mtx_t* mtx) {
    mtx->type = __Libc_Impl_Mutex_type_recursive;
    IMPL_MUTEX_REC_CALL(init)(&(mtx->data.recursive));
    return thrd_success;
}


// impl

int mtx_init(mtx_t* mtx, int type) {
    if (mtx == NULL) {
        return thrd_error;
    }

    if (type == mtx_plain) {
        return mtx_init_plain(mtx);
    }


    if (type == (mtx_plain | mtx_recursive)) {
        return mtx_init_recursive(mtx);
    }


    return thrd_error;
}

void mtx_destroy(mtx_t* mtx) {
    if (mtx == NULL) {
        return;
    }

    switch (mtx->type) {
        case __Libc_Impl_Mutex_type_plain:
            IMPL_MUTEX_PLAIN_CALL(destroy)(&(mtx->data.plain));
            return;
        case __Libc_Impl_Mutex_type_recursive:
            IMPL_MUTEX_REC_CALL(destroy)(&(mtx->data.recursive));
            return;
        default:
            return;
    }
}

int mtx_lock(mtx_t* mtx) {
    if (mtx == NULL) {
        return thrd_error;
    }

    switch (mtx->type) {
        case __Libc_Impl_Mutex_type_plain:
            IMPL_MUTEX_PLAIN_CALL(lock)(&(mtx->data.plain));
            return thrd_success;
        case __Libc_Impl_Mutex_type_recursive:
            IMPL_MUTEX_REC_CALL(lock)(&(mtx->data.recursive));
            return thrd_success;
        default:
            return thrd_error;
    }
}

int mtx_timedlock(mtx_t* restrict mtx, const struct timespec* restrict ts) {
    // not implemented yet
    return thrd_error;
}

int mtx_trylock(mtx_t* mtx) {
    if (mtx == NULL) {
        return thrd_error;
    }

    switch (mtx->type) {
        case __Libc_Impl_Mutex_type_plain:
            return IMPL_MUTEX_PLAIN_CALL(trylock)(&(mtx->data.plain));
        case __Libc_Impl_Mutex_type_recursive:
            return IMPL_MUTEX_REC_CALL(trylock)(&(mtx->data.recursive));
        default:
            return thrd_error;
    }
}

int mtx_unlock(mtx_t* mtx) {
    if (mtx == NULL) {
        return thrd_error;
    }

    switch (mtx->type) {
        case __Libc_Impl_Mutex_type_plain:
            IMPL_MUTEX_PLAIN_CALL(unlock)(&(mtx->data.plain));
            return thrd_success;
        case __Libc_Impl_Mutex_type_recursive:
            IMPL_MUTEX_REC_CALL(unlock)(&(mtx->data.recursive));
            return thrd_success;
        default:
            return thrd_error;
    }
}

typedef struct {
    EFI_MP_SERVICES_PROTOCOL* Mp;
} EfiThreadState;

static EfiThreadState __efi_thread_state = { .Mp = NULL };

void efi_threads_init(void) {

    if (__efi_thread_state.Mp != NULL) {
        return;
    }

    if (!gBS) {
        DEBUG((DEBUG_ERROR, "ERROR: gBS is NULL\n"));
        abort();
    }

    EFI_STATUS Status = gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (VOID**) (&(__efi_thread_state.Mp)));

    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "ERROR: couldn't get the EfiMpServiceProtocolGuid: %r\n", Status));
        abort();
    }
}

[[nodiscard]] UINTN efi_thread_id(void) {
    ASSERT(__efi_thread_state.Mp != NULL);

    UINTN CpuNUmber;

    EFI_STATUS Status = __efi_thread_state.Mp->WhoAmI(__efi_thread_state.Mp, &CpuNUmber);

    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "ERROR: couldn't execute WhoAmI: %r\n", Status));
        abort();
    }

    return CpuNUmber;
}

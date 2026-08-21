
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define EDK2_LIBC_ENTRY_NAME edk2_libc_main

extern int EDK2_LIBC_ENTRY_NAME(int, char**);

typedef void (*libcxx_destroy_function_t)(void);
extern void edk2_libcxx_set_destroy(libcxx_destroy_function_t cb);


#ifdef __cplusplus
}

#define EDK2_LIBCXX_ENTRY_NAME edk2_libcxx_main

extern int EDK2_LIBCXX_ENTRY_NAME(int, char**);
#endif

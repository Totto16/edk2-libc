
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define EDK2_LIBC_ENTRY_NAME edk2_libc_main

extern int EDK2_LIBC_ENTRY_NAME(int, char**);

#ifdef __cplusplus
}

#define EDK2_LIBCXX_ENTRY_NAME edk2_libcxx_main

extern int EDK2_LIBCXX_ENTRY_NAME(int, char**);


#endif

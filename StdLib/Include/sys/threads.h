#pragma once

// internal threads api

#include <sys/EfiCdefs.h>

__BEGIN_DECLS

void efi_threads_init(void);

[[nodiscard]] UINTN efi_thread_id(void);

__END_DECLS

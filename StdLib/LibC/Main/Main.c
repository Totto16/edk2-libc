/** @file
  Establish the program environment and the "main" entry point.

  All of the global data in the gMD structure is initialized to 0, NULL, or
  SIG_DFL; as appropriate.

  Copyright (c) 2010 - 2014, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials are licensed and made available under
  the terms and conditions of the BSD License that accompanies this distribution.
  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/
#include  <Uefi.h>
#include  <Library/UefiLib.h>
#include  <Library/DebugLib.h>

#include  <Library/ShellCEntryLib.h>
#include  <Library/MemoryAllocationLib.h>
#include  <Library/TimerLib.h>

#include  <LibConfig.h>

#include  <errno.h>
#include  <stdio.h>
#include  <stdlib.h>
#include  <string.h>
#include  <time.h>
#include  <MainData.h>
#include  <unistd.h>

#include <libc/main.h>

extern int __sse2_available;

struct  __MainData  *gMD;

// these symbols are provided by the linker from the extions .init_array and .fini_array
typedef void (*init_func_t)(void);

extern init_func_t __init_array_start[] __attribute__((section(".init_array"), aligned(sizeof(init_func_t))));
extern init_func_t __init_array_end[] __attribute__((section(".init_array"), aligned(sizeof(init_func_t))));

extern init_func_t __fini_array_start[] __attribute__((section(".fini_array"), aligned(sizeof(init_func_t))));
extern init_func_t __fini_array_end[] __attribute__((section(".fini_array"), aligned(sizeof(init_func_t))));

extern init_func_t __libc_init_array_start[] __attribute__((section(".init_array"), aligned(sizeof(init_func_t))));
extern init_func_t __libc_init_array_end[] __attribute__((section(".init_array"), aligned(sizeof(init_func_t))));

extern init_func_t __libc_fini_array_start[] __attribute__((section(".fini_array"), aligned(sizeof(init_func_t))));
extern init_func_t __libc_fini_array_end[] __attribute__((section(".fini_array"), aligned(sizeof(init_func_t))));



// see: https://gcc.gnu.org/onlinedocs/gccint/Initialization.html
// for more information on how gcc handles initialization

static void run_init_array(void) {
    size_t size = __init_array_end - __init_array_start;
    for (size_t i = 0; i < size; ++i) {
        init_func_t func = __init_array_start[i];
        if (*func != NULL) {
            (*func)();
        }
    }
}

static void run_fini_array(void) {
    // iterate reversed
    size_t size = __fini_array_end - __fini_array_start;
    for (size_t i = size; i != 0; --i) {
        init_func_t func = __fini_array_start[i - 1];
        if (*func != NULL) {
            (*func)();
        }
    }
}

// see eg. here for some information:
// https://maskray.me/blog/2021-11-07-init-ctors-init-array

static void edk2_libc_call_constructors(void) {
    run_init_array();
}

static void edk2_libc_call_destructors(void) {
    run_fini_array();
}

static void __c_uefi_init_libc(void) {
    size_t size = __libc_init_array_end - __libc_init_array_start;
    for (size_t i = 0; i < size; ++i) {
        init_func_t func = __libc_init_array_start[i];
        if (*func != NULL) {
            (*func)();
        }
    }
}


init_func_t __libc_init_ref_edk2_libc_call_constructors __attribute__((retain, used, section(".__libc_init.prio_99.libc_constructors"), aligned(sizeof(init_func_t)))) = 
edk2_libc_call_constructors;


static void __c_uefi_deinit_libc(void) {
    // iterate reversed
    size_t size = __libc_fini_array_end - __libc_fini_array_start;
    for (size_t i = size; i != 0; --i) {
        init_func_t func = __libc_fini_array_start[i - 1];
        if (*func != NULL) {
            (*func)();
        }
    }
}


init_func_t __libc_init_ref_edk2_libc_call_destructors __attribute__((retain, used, section(".__libc_fini.prio_99.libc_destructors"), aligned(sizeof(init_func_t)))) = 
edk2_libc_call_destructors;


/** Clean up data as required by the exit() function.

**/
void
exitCleanup(INTN ExitVal)
{
  void (*CleanUp)(void);   // Pointer to Cleanup Function
  int i;

  if(gMD != NULL) {
    gMD->ExitValue = (int)ExitVal;
    CleanUp = gMD->cleanup; // Preserve the pointer to the Cleanup Function

    // Call all registered atexit functions in reverse order
    i = gMD->num_atexit;
    if( i > 0) {
      do {
        (gMD->atexit_handler[--i])();
      } while( i > 0);
  }

    if (CleanUp != NULL) {
      CleanUp();
    }
  }

  // this is needed, if we use a c++ standard library, we need to clean up that before we clean up the libc and after custom cleanup and atexit calls
  __c_uefi_deinit_libc();
}

/* Create mbcs versions of the Argv strings. */
static
char **
ArgvConvert(UINTN Argc, CHAR16 **Argv)
{
  ssize_t  AVsz;       /* Size of a single nArgv string, or -1 */
  UINTN   count;
  char  **nArgv;
  char   *string;
  INTN    nArgvSize;  /* Cumulative size of narrow Argv[i] */

DEBUG_CODE_BEGIN();
  DEBUG((DEBUG_INIT, "ArgvConvert called with %d arguments.\n", Argc));
  for(count = 0; count < ((Argc > 5)? 5: Argc); ++count) {
    DEBUG((DEBUG_INIT, "Argument[%d] = \"%s\".\n", count, Argv[count]));
  }
DEBUG_CODE_END();

  nArgvSize = Argc;
  /* Determine space needed for narrow Argv strings. */
  for(count = 0; count < Argc; ++count) {
    AVsz = (ssize_t)wcstombs(NULL, Argv[count], ARG_MAX);
    if(AVsz < 0) {
      DEBUG((DEBUG_ERROR, "ABORTING: Argv[%d] contains an unconvertable character.\n", count));
      exit(EXIT_FAILURE);
      /* Not Reached */
    }
    nArgvSize += AVsz;
  }

  /* Reserve space for the converted strings. */
  gMD->NCmdLine = (char *)AllocateZeroPool(nArgvSize+1);
  if(gMD->NCmdLine == NULL) {
    DEBUG((DEBUG_ERROR, "ABORTING: Insufficient memory.\n"));
    exit(EXIT_FAILURE);
    /* Not Reached */
  }

  /* Convert Argument Strings. */
  nArgv   = gMD->NArgV;
  string  = gMD->NCmdLine;
  for(count = 0; count < Argc; ++count) {
    nArgv[count] = string;
    AVsz = wcstombs(string, Argv[count], nArgvSize) + 1;
    DEBUG((DEBUG_INFO, "Cvt[%d] %d \"%s\" --> \"%a\"\n", (INT32)count, (INT32)AVsz, Argv[count], nArgv[count]));
    string += AVsz;
    nArgvSize -= AVsz;
    if(nArgvSize < 0) {
      DEBUG((DEBUG_ERROR, "ABORTING: Internal Argv[%d] conversion error.\n", count));
      exit(EXIT_FAILURE);
      /* Not Reached */
    }
  }
  return gMD->NArgV;
}

INTN
EFIAPI
ShellAppMain (
  IN UINTN Argc,
  IN CHAR16 **Argv
  )
{
  struct __filedes   *mfd;
  char              **nArgv;
  INTN   ExitVal;
  int                 i;

  ExitVal = (INTN)RETURN_SUCCESS;
  gMD = AllocateZeroPool(sizeof(struct __MainData));
  if( gMD == NULL ) {
    ExitVal = (INTN)RETURN_OUT_OF_RESOURCES;
  }
  else {
    /* Initialize data */
    __sse2_available      = 0;
    _fltused              = 1;
    errno                 = 0;
    EFIerrno              = 0;

    gMD->ClocksPerSecond  = 1;
    gMD->AppStartTime     = (clock_t)((UINT32)time(NULL));

    // Initialize file descriptors
    mfd = gMD->fdarray;
    for(i = 0; i < (FOPEN_MAX); ++i) {
      mfd[i].MyFD = (UINT16)i;
    }

    DEBUG((DEBUG_INIT, "StdLib: Open Standard IO.\n"));
    i = open("stdin:", (O_RDONLY | O_TTY_INIT), 0444);
    if(i == 0) {
      i = open("stdout:", (O_WRONLY | O_TTY_INIT), 0222);
      if(i == 1) {
        i = open("stderr:", O_WRONLY, 0222);
      }
    }
    if(i != 2) {
      Print(L"ERROR Initializing Standard IO: %a.\n    %r\n",
            strerror(errno), EFIerrno);
    }

    /* Create mbcs versions of the Argv strings. */
    nArgv = ArgvConvert(Argc, Argv);
    if(nArgv == NULL) {
      ExitVal = (INTN)RETURN_INVALID_PARAMETER;
    }
    else {
      if( setjmp(gMD->MainExit) == 0) {
        errno   = 0;    // Clean up any "scratch" values from startup.
        __c_uefi_init_libc();
        ExitVal = (INTN)EDK2_LIBC_ENTRY_NAME( (int)Argc, gMD->NArgV);
        exitCleanup(ExitVal);
      }
      /* You reach here if:
          * normal return from main()
          * call to _Exit(), either directly or through exit().
      */
      ExitVal = (INTN)gMD->ExitValue;
    }

    if( ExitVal == EXIT_FAILURE) {
      ExitVal = RETURN_ABORTED;
    }

    /* Close any open files */
    for(i = OPEN_MAX - 1; i >= 0; --i) {
      (void)close(i);   // Close properly handles closing a closed file.
    }

    /* Free the global MainData structure */
    if(gMD != NULL) {
      if(gMD->NCmdLine != NULL) {
        FreePool( gMD->NCmdLine );
      }
      FreePool( gMD );
  }
  }
  return ExitVal;
}

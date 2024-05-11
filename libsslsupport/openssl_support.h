#ifndef _OPENSSL_SUPPORT_H_
#define _OPENSSL_SUPPORT_H_

#include <efi.h>
#include <efilib.h>

typedef UINTN size_t;
#define DEFINED_SIZE_T

typedef long time_t;
typedef VOID *FILE;

typedef struct {
#if defined(__LP64__)
  int32_t __private[14];
#else
  int32_t __private[10];
#endif
} pthread_rwlock_t;

#endif 	/* _OPENSSL_SUPPORT_H_ */

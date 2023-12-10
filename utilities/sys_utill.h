#ifndef SYS_UTILL_H_
#define SYSC_UTILL_H_

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>

#define call_or_exit(call, err) \
  if ((call) < 0) { \
    perror(err); \
    exit(EXIT_FAILURE); \
  }

#define pthread_call_or_exit(status, err) \
  if ((status) != 0) { \
    fprintf(stderr, "%s: %s\n", (err), strerror( (status) )); \
    exit(EXIT_FAILURE); \
  }

ssize_t write_(int fd, const char* buf, size_t nbytes);

#endif // SYSC_UTILL_H_

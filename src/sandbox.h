#ifndef GLIF_SANDBOX_H
#define GLIF_SANDBOX_H

#if defined(__OpenBSD__) || defined(__linux__)
  #include <unistd.h>
  int glif_pledge(const char *p);
  int glif_unveil(const char *path, const char *perms);
  int glif_unveil_lock(void);
#endif

#endif

#ifndef GLIF_SANDBOX_H
#define GLIF_SANDBOX_H

#if defined(__OpenBSD__)
  #include <unistd.h>
  static inline int glif_pledge(const char *p) { return pledge(p, NULL); }
  static inline int glif_unveil(const char *path, const char *perms) { return unveil(path, perms); }
  static inline int glif_unveil_lock(void) { return unveil(NULL, NULL); }
#elif defined(__linux__)
  int pledge(const char *, const char *);
  int unveil(const char *, const char *);
  static inline int glif_pledge(const char *p) { return pledge(p, NULL); }
  static inline int glif_unveil(const char *path, const char *perms) { return unveil(path, perms); }
  static inline int glif_unveil_lock(void) { return unveil(NULL, NULL); }
#endif

#endif

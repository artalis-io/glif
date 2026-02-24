#if defined(__linux__)
#include "sandbox.h"
#include <unistd.h>

int glif_pledge(const char *p) { return pledge(p, NULL); }
int glif_unveil(const char *path, const char *perms) { return unveil(path, perms); }
int glif_unveil_lock(void) { return unveil(NULL, NULL); }
#endif
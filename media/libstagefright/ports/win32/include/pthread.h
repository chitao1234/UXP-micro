/* Intentionally blank for the MSVC build, which has no pthreads.
 *
 * A posix-threads MinGW toolchain is a different story: libstdc++ itself
 * includes <pthread.h> (via ext/atomicity.h -> gthr.h) and then needs
 * pthread_t and friends. Because this directory is on the include path ahead
 * of the toolchain's own headers, the blank stub would leave them undefined.
 * Defer to the real header in that configuration.
 */
#if defined(__MINGW32__)
#include_next <pthread.h>
#endif

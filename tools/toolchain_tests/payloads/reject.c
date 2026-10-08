/* Compile-only negative tests. These programs must not become USB payloads. */
#include <stdint.h>
#if REJECTION == 1
uint8_t bad(void) { return *(volatile __sfr uint8_t *)0; }
#elif REJECTION == 2
uint64_t bad(void) { return *(volatile __sfr uint64_t *)0; }
#elif REJECTION == 3
float bad(void) { return *(volatile __sfr float *)0; }
#elif REJECTION == 4
__sfr uint32_t bad_storage;
#elif REJECTION == 5
__sfr uint32_t *bad(uint32_t *p) { return (__sfr uint32_t *)p; }
#else
#error Unknown rejection case
#endif

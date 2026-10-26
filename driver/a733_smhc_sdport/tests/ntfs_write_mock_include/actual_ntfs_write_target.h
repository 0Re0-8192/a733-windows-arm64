#ifndef A733_NW_COMPILE_ONLY
#error This is a compile-only mock, never a staging or deployment target
#endif
/* Parent/staging supplies the real header. Quoted includes prefer that header
 * when present. The fallback deliberately cannot identify a valid SD range. */
#define A733_WRITE_TARGET_PHYSICAL_LBA 0xFFFFFFFFUL

#ifndef LC_CDEFS_H
#define LC_CDEFS_H

#if !defined _Noreturn && __STDC_VERSION__ < 201112
#	if (3 <= __GNUC__ || (__GNUC__ == 2 && 8 <= __GNUC_MINOR__) || 0x5110 <= __SUNPRO_C)
#		define _Noreturn __attribute__ ((__noreturn__))
#	elif 1200 <= _MSC_VER
#		define _Noreturn __declspec (noreturn)
#	else
#		define _Noreturn
#	endif
#endif

/* Align a value by rounding down to closest size.
 *  e.g. Using size of 4096, we get this behavior:
 *   {4095, 4096, 4097} = {0, 4096, 4096}.
 */
#define ALIGN_DOWN(base, size) ((base) & -((__typeof__ (base)) (size)))

/* Align a value by rounding up to closest size.
   e.g. Using size of 4096, we get this behavior:
     {4095, 4096, 4097} = {4096, 4096, 8192}.
   Note: The size argument has side effects (expanded multiple times).
*/
#define ALIGN_UP(base, size) ALIGN_DOWN ((base) + (size) - 1, (size))

//  TODO: Check GCC or Clang
#ifndef likely
#define likely(x) __builtin_expect(!!(x),1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x),0)
#endif

#endif /* LC_CDEFS_H */

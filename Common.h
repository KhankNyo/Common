
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef COMMON_H
#define COMMON_H

#include <limits.h> /* *_MAX, *_MIN */
#include <stddef.h> /* NULL, size_t, offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(_MSC_VER) && !defined(__clang__)
#  define COMPILER_MSVC
#elif defined(__clang__)
#  define COMPILER_CLANG
#elif defined(__GNUC__)
#  define COMPILER_GCC
#elif defined(__TINYC__)
#  define COMPILER_TCC
#else
#  define COMPILER_UNKNOWN
#endif

#if defined(COMPILER_MSVC)
#  if defined(_M_IX86)
#    define ARCH_X86_HAS_COMPILER_INTRINSICS
#    define ARCH_X86
#    define ARCH_X86_IA32
#  elif defined(_M_X64)
#    define ARCH_X86_HAS_COMPILER_INTRINSICS
#    define ARCH_X86
#    define ARCH_X86_AMD64
#  else
#    error MSVC: Unknown CPU
#  endif
#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC)
#  if defined(__x86_64) || defined(__x86_64__)
#    define ARCH_X86_HAS_COMPILER_INTRINSICS
#    define ARCH_X86
#    define ARCH_X86_AMD64
#  elif defined(__i386__)
#    define ARCH_X86_HAS_COMPILER_INTRINSICS
#    define ARCH_X86
#    define ARCH_X86_IA32
#  else 
#    error clang/gcc: Unknown CPU
#  endif
#elif defined(COMPILER_TCC)
#  if defined(__x86_64) || defined(__x86_64__)
#    define ARCH_X86
#    define ARCH_X86_AMD64
#  elif defind(__i386__)
#    define ARCH_X86
#    define ARCH_X86_IA32
#  else 
#    error tcc: Unknown CPU
#  endif
#endif

#if defined(NO_SIMD)
#  undef ARCH_X86_HAS_COMPILER_INTRINSICS
#endif


/* TODO: count trailing zeros: __builtin_ctz (clang, gcc), _BitScanForward (msvc)
 * consider stdc_trailing_zeros(x)
 * */
#if defined(COMPILER_MSVC)
#  define force_inline static inline __forceinline
#  define packed(...) __pragma( pack(push, 1) ) __VA_ARGS__ __pragma( pack(pop) )
#  define align(v) __declspec(alignas(v))
#  define die_IMMEDIATELY() __assume(0)
#  define STATIC_ASSERT(x, m) static_assert(x, m)
#  define alignment_of(expr) __alignof(expr)
#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC) || defined(COMPILER_TCC)
#  define force_inline static inline __attribute__((always_inline))
#  define packed(...) __VA_ARGS__ __attribute__((packed)) 
#  define align(v) __attribute__((aligned(v)))
#  if !defined(COMPILER_TCC)
#    define die_IMMEDIATELY() do { ((*(volatile char *)0) = 0); __builtin_unreachable();} while (0)
#  else
#    define die_IMMEDIATELY() do { ((*(volatile char *)0) = 0); } while (0)
#  endif
#  define STATIC_ASSERT(x, m) _Static_assert(x, m)
#  define alignment_of(expr) __alignof__(expr)
#else
#  error Unknown compiler, must add force_inline, packed(...), align(v), die_IMMEDIATELY(), STATIC_ASSERT(x, m)
#endif

#define typedef_struct(name) typedef struct name name
#define typedef_union(name) typedef union name name
#define handle(scalar_subtype) struct { scalar_subtype Value; }
#define internal static
#define persistent_local static
#define header_function static inline
#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define eprintfln(...) (fprintf(stderr, __VA_ARGS__) + fprintf(stderr, "\n"))
#define printfln(...) (printf(__VA_ARGS__) + printf("\n"))
#define fprintfln(f, ...) (fprintf(f, __VA_ARGS__), fprintf(f, "\n"))


#if defined(_DEBUG)
#  define DEBUG_ONLY(...) __VA_ARGS__
#  define NOT_DEBUG_ONLY(...)
#else
#  define DEBUG_ONLY(...)
#  define NOT_DEBUG_ONLY(...) __VA_ARGS__
#endif

#define UNREACHABLE_IF(cond, ...) do {\
    if (cond) {\
        (void)eprintfln(__FILE__ ", %s(), line %d:", __FUNCTION__, __LINE__);\
        (void)eprintfln(__VA_ARGS__);\
        die_IMMEDIATELY();\
    }\
} while (0)
#define UNREACHABLE() UNREACHABLE_IF(true, "Unreachable but reachable!!!!!!")
#define ASSERT(cond, ...) DEBUG_ONLY(UNREACHABLE_IF(!(cond), "ASSERTION FAILED '"#cond"':\n" __VA_ARGS__))
#define TODO(...) STATIC_ASSERT(false, "TODO: " __VA_ARGS__)
#define RUNTIME_TODO(...) UNREACHABLE_IF(true, "TODO: "__VA_ARGS__)
#define ASSERT_EXPRESSION_TYPE(expr, type, msg) \
    STATIC_ASSERT(_Generic((expr), \
        type: true, \
        default: false), \
        "" msg\
    ) /* c11 is awesome, runs on msvc, clang, gcc and even tcc */

#define STATIC_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IN_RANGE(lower, n, upper) ((lower) <= (n) && (n) <= (upper))
#define MINIMUM(a, b) ((a) < (b)? (a): (b))
#define MAXIMUM(a, b) ((a) > (b)? (a): (b))
#define CLAMP(lower, n, upper) \
    ((n) > (upper)\
    ? (upper) \
    : (n) < (lower)\
        ? (lower) \
        : (n))
#define SWAP(type, a, b) do {\
    type tmp_ = a;\
    a = b;\
    b = tmp_;\
} while (0)
#define IS_SET(flag_bits, flags) (((flag_bits) & (flags)) != 0)
#define IS_POWER_OF_2(value) (((value) & ((value) - 1)) == 0)
#define TO_UPPER(ascii_char) ((ascii_char) & ~(1u << 5))

/* TODO: remove this, Platform-Core.h and Renderer-Vulkan.h are depending on it, but its usage can be replaced by slice */
#define dynamic_array(...) struct {\
    __VA_ARGS__ *Data;\
    isize Count, Capacity;\
}
#define Arena_AllocDynamicArray(p_arena, p_dynamic_array, isize_new_count, isize_new_capacity) do {\
    typeof(p_dynamic_array) dynamic_array_ = p_dynamic_array;\
    dynamic_array_->Count = isize_new_count;\
    dynamic_array_->Capacity = isize_new_capacity;\
    Arena_AllocArray(p_arena, &dynamic_array_->Data, dynamic_array_->Capacity);\
} while (0)
#define dynamic_array_foreach(p_dynamic_array, iterator_name) for (\
        typeof((p_dynamic_array)->Data) iterator_name = (p_dynamic_array)->Data;\
        iterator_name < (p_dynamic_array)->Data + (p_dynamic_array)->Count;\
        iterator_name++)



#define KB 1024
#define MB (1024*1024)

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef intptr_t isize;
typedef uintptr_t usize;
typedef unsigned uint;
STATIC_ASSERT(sizeof(bool) == 1, "");
typedef bool bool8;
typedef u32 bool32;

typedef union 
{
    u32 U32;
    float F32;
} fu32;

typedef union 
{
    float At[2];
    struct {
        float u, v;
    };
    struct {
        float x, y;
    };
    struct {
        float i, j;
    };
} v2f, uv;

typedef union
{
    float At[3];
    struct {
        float x, y, z;
    };
    struct {
        float r, g, b;
    };
    struct {
        float i, j, k;
    };
} v3f, rgb;

typedef union 
{
    float At[4];
    struct {
        float x, y, z, w;
    };
    struct {
        float r, g, b, a;
    };
} v4f, rgba;

typedef union 
{
    float At[4][4];
    struct {
        v4f Rows[4];
    };
} m4f;


force_inline u32 CountBits32(u32 Value)
{
    /* NOTE:(Khanh): will be optimized into popcnt with -msse4.2 */
    u32 Count = 0;
    while (Value)
    {
        Value &= Value - 1;
        Count++;
    }
    return Count;
}

force_inline u64 CountBits64(u64 Value)
{
    /* NOTE:(Khanh): will be optimized into popcnt with -msse4.2 */
    u64 Count = 0;
    while (Value)
    {
        Value &= Value - 1;
        Count++;
    }
    return Count;
}


force_inline uint CountLeadingZeros32(u32);
force_inline uint CountLeadingZeros64(u64);
force_inline uint CountTrailingZeros32(u32);
force_inline uint CountTrailingZeros64(u64);

#if defined(COMPILER_CLANG) || defined(COMPILER_GCC) || defined(COMPILER_TCC)
force_inline uint CountLeadingZeros32(u32 Value)
{
    if (Value == 0)
        return 32;
    return __builtin_clz(Value);
}
force_inline uint CountLeadingZeros64(u64 Value)
{
    if (Value == 0)
        return 64;
    return __builtin_clzll(Value);
}
force_inline uint CountTrailingZeros32(u32 Value)
{
    if (Value == 0)
        return 32;
    return __builtin_ctz(Value);
}
force_inline uint CountTrailingZeros64(u64 Value)
{
    if (Value == 0)
        return 64;
    return __builtin_ctzll(Value);
}
#elif defined(COMPILER_MSVC)
force_inline uint CountLeadingZeros32(u32 Value)
{
    unsigned long Count;
    if (!_BitScanReverse(&Count, Value))
        return 32;
    return Count;
}
force_inline uint CountLeadingZeros64(u64 Value)
{
    unsigned long Count;
    if (!_BitScanReverse64(&Count, Value))
        return 64;
    return Count;
}
force_inline uint CountTrailingZeros32(u32 Value)
{
    unsigned long Count;
    if (!_BitScanForward(&Count, Value))
        return 32;
    return Count;
}
force_inline uint CountTrailingZeros64(u64 Value)
{
    unsigned long Count;
    if (!_BitScanForward64(&Count, Value))
        return 64;
    return Count;
}
#else
force_inline uint CountLeadingZeros32(u32 Value)
{
    Value |= Value >> 1;
    Value |= Value >> 2;
    Value |= Value >> 4;
    Value |= Value >> 8;
    Value |= Value >> 16;
    return CountBits32(~Value);
}
force_inline uint CountLeadingZeros64(u64 Value)
{
    Value |= Value >> 1;
    Value |= Value >> 2;
    Value |= Value >> 4;
    Value |= Value >> 8;
    Value |= Value >> 16;
    Value |= Value >> 32;
    return CountBits64(~Value);
}
force_inline uint CountTrailingZeros32(u32 Value)
{
    /* generic implementation */
    uint Count = 32;
    Value &= -Value;
    if (Value) Count -= 1;
    if ((Value & 0x0000FFFF)) Count -= 16;
    if ((Value & 0x00FF00FF)) Count -= 8;
    if ((Value & 0x0F0F0F0F)) Count -= 4;
    if ((Value & 0x33333333)) Count -= 2;
    if ((Value & 0x55555555)) Count -= 1;
    return Count;
}
force_inline uint CountTrailingZeros64(u64 Value)
{
    uint Count = 64;
    Value &= -Value;
    if (Value) Count -= 1;
    if ((Value & 0x00000000FFFFFFFFllu)) Count -= 32;
    if ((Value & 0x0000FFFF0000FFFFllu)) Count -= 16;
    if ((Value & 0x00FF00FF00FF00FFllu)) Count -= 8;
    if ((Value & 0x0F0F0F0F0F0F0F0Fllu)) Count -= 4;
    if ((Value & 0x3333333333333333llu)) Count -= 2;
    if ((Value & 0x5555555555555555llu)) Count -= 1;
    return Count;
}
#endif



force_inline u32 HashString(const char *Name, int NameLength)
{
    u32 h = 0x12345678;
    // One-byte-at-a-time hash based on Murmur's mix
    // Source: https://github.com/aappleby/smhasher/blob/master/src/Hashes.cpp
    // NOTE:(Khanh): adapted to work with non-null terminated string here
    while (NameLength --> 0)
    {
        h ^= *Name++;
        h *= 0x5bd1e995;
        h ^= h >> 15;
    }
    return h & 0xFF;
}

force_inline bool32 SubstringsAreEqual(const char *A, const char *B, isize Length)
{
    while (Length > 0 && *A && *B)
    {
        Length--;
        if (*A != *B)
            return false;
        A++;
        B++;
    }
    return Length == 0;
}

#endif /* COMMON_H */


#ifdef __cplusplus
}
#endif /* __cplusplus */

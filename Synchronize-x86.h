#ifndef SYNCHRONIZE_X86_H
#define SYNCHRONIZE_X86_H

#include "Common.h"

force_inline u64 Sync__x86_Cmpxchg64(volatile u64 *Ptr, u64 Expected, u64 ValueToSetIfEqual)
{
#  if defined(COMPILER_MSVC)
#    error TODO: msvc cmpxchg
#  else
    u64 PreviousValue = 0;
    __asm__ __volatile__(
        "mov %[Expected], %%rax;"
        "lock cmpxchg %[ValueToSetIfEqual], 0(%[Ptr]);"
        "movq %%rax, %[PreviousValue];"

        : /* output */ 
            [PreviousValue] "+r" (PreviousValue)
        : /* input  */ 
            [Ptr] "r" (Ptr),
            [Expected] "r" (Expected),
            [ValueToSetIfEqual] "r" (ValueToSetIfEqual)
        : /* clobber */
            "rax"
    );
    return PreviousValue;
#  endif

}

force_inline u64 Sync__x86_LockInc64(volatile u64 *Ptr)
{
    u64 PreviousValue = 0;
#  if defined(COMPILER_MSVC)
#    error TODO: msvc lock inc
#  else
    __asm__ __volatile__(
        "movl $1, %%edx;"
        "lock xadd %%rdx, 0(%[Ptr]);"       /* rdx = (*Ptr)++ */
        "movq %%rdx, %[PreviousValue];"     /* PreviousValue = rdx */
        : /* output */ 
            [PreviousValue] "+r" (PreviousValue)
        : /* input  */ 
            [Ptr] "r" (Ptr)
        : /* clobber */
            "rdx"
    );
#  endif
    return PreviousValue;
}

force_inline void Sync__x86_Mfence(void)
{
#  if defined(COMPILER_MSVC)
#    error TODO: msvc mfence
#  else
    __asm__ __volatile__(
        "mfence;"
        ::: /* clobber */
            "memory"
    );
#  endif
}


#endif /* SYNCHRONIZE_X86_H */


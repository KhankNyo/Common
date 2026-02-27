#ifndef SYNCHRONIZE_H
#define SYNCHRONIZE_H


#include "Common.h"

#if defined(ARCH_X86)
#  include "Synchronize-x86.h"
#else
#  error Synchronize.h: Unknown arch
#endif

typedef_struct(sync_ticket_mutex);

/* initialize with 0 */
struct sync_ticket_mutex
{
    volatile u64 Ticket;
    volatile u64 TicketServed;
};



/* returns previous value */
force_inline u64 Sync_InterlockedCompareExchange64(volatile u64 *Ptr, u64 Expected, u64 ValueToSetIfEqual)
{
#if defined(ARCH_X86)
    return Sync__x86_Cmpxchg64(Ptr, Expected, ValueToSetIfEqual);
#else
#    error TODO: cmpxchg in other arch
#endif

}

/* returns current value */
force_inline u64 Sync_InterlockedIncrement64(volatile u64 *Ptr)
{
#if defined(ARCH_X86)
    return Sync__x86_LockInc64(Ptr);
#else
#    error TODO: lock inc in other arch
#endif
}

force_inline void Sync_MemoryBarrier(void)
{
#if defined(ARCH_X86)
    Sync__x86_Mfence();
#else
#    error TODO: memory barrier on other arch
#endif
}


force_inline void Sync_TicketMutex_Lock(sync_ticket_mutex *Mutex)
{
    typeof(Mutex->Ticket) Ticket = Sync_InterlockedIncrement64(&Mutex->Ticket);
    while (Ticket != Mutex->TicketServed)
    {
        /* busy wait */
    }
}

force_inline void Sync_TicketMutex_Unlock(sync_ticket_mutex *Mutex)
{
    Sync_MemoryBarrier();
    Sync_InterlockedIncrement64(&Mutex->TicketServed);
}


#endif /* SYNCHRONIZE_H */


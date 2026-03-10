#ifndef PROFILER_H
#define PROFILER_H

#include "Common.h"

typedef double (*profiler_get_elapsed_time_fn)(void *UserData);
typedef_struct(profiler_scope);
typedef_struct(profiler);

header_function void Profiler_Init(profiler *Profiler, void *GetTimeData, profiler_get_elapsed_time_fn GetTime);
header_function int Profiler_PrintGraph(profiler *Profiler, char *OutStringBuffer, int OutStringBufferCapacity, bool32 ShouldReset);
header_function void Profiler_Reset(profiler *Profiler);

/* Profiler_Scope() usage: 
```
   Profiler_Scope(&Profiler, "my scope name") 
   {
       some code to be profiled
       ...
   }
```
*/
#define Profiler_Scope(p_profiler, str_name) \
    for (profiler__tmp_scope_data profiler__data = Profiler__StartScope(p_profiler); \
            profiler__data.ShouldContinue; \
            Profiler__EndScope(p_profiler, &profiler__data, str_name))


struct profiler_scope {
    double TimeStart, TimeEnd, TimeDiff;
    const char *Name;
    int NestLevel;
};

#define PROFILER_OUT_STRING_BUFFER_MIN_CAPACITY 4096
#define PROFILER_SCOPE_CAPACITY 256
struct profiler
{
    void *GetTimeData;
    profiler_get_elapsed_time_fn GetTime;
    int NestLevel;

    int MaxNameLength;
    int IndentWidth;

    int ScopeCount,     /* current count of scope since last Profiler_Reset() or Profiler_Init() */
        ScopeCapacity;  /* max scopes encountered since Profiler_Init() */
    profiler_scope Scopes[PROFILER_SCOPE_CAPACITY];
};




typedef_struct(profiler__tmp_scope_data);
struct profiler__tmp_scope_data
{
    bool8 ShouldContinue;
    profiler_scope *CurrentScope;
};

header_function void Profiler_Reset(profiler *Profiler)
{
    if (!Profiler) 
        return;

    if (Profiler->ScopeCount > Profiler->ScopeCapacity)
    {
        Profiler->ScopeCapacity = Profiler->ScopeCount;
    }

    Profiler->ScopeCount = 0;
    Profiler->NestLevel = 0;
}

header_function void Profiler_Init(profiler *Profiler, void *GetTimeData, profiler_get_elapsed_time_fn GetTime)
{
    if (!Profiler) 
        return;

    *Profiler = (profiler) {
        .GetTimeData = GetTimeData,
        .GetTime = GetTime,
        .MaxNameLength = 64,
        .IndentWidth = 4,
    };
}



header_function profiler__tmp_scope_data Profiler__StartScope(profiler *Profiler)
{
    if (!Profiler) 
        return (profiler__tmp_scope_data) { .ShouldContinue = true };

    ASSERT(Profiler->ScopeCount < PROFILER_SCOPE_CAPACITY, "Too many scopes");
    profiler_scope *Scope = &Profiler->Scopes[Profiler->ScopeCount];
    Profiler->ScopeCount++;

    profiler__tmp_scope_data ScopeData = {
        .ShouldContinue = true,
        .CurrentScope = Scope,
    };
    Scope->TimeStart = Profiler->GetTime(Profiler->GetTimeData);
    Scope->NestLevel = Profiler->NestLevel;
    Profiler->NestLevel++;
    return ScopeData;
}

header_function void Profiler__EndScope(profiler *Profiler, profiler__tmp_scope_data *Data, const char *Name)
{
    ASSERT(Data);
    if (!Profiler) 
    {
        Data->ShouldContinue = false;
        return;
    }

    double End = Profiler->GetTime(Profiler->GetTimeData);
    Data->CurrentScope->TimeEnd = End;
    Data->CurrentScope->Name = Name;
    Data->CurrentScope->TimeDiff = End - Data->CurrentScope->TimeStart;
    Data->ShouldContinue = false;
    Profiler->NestLevel--;
}

header_function int Profiler_PrintGraph(profiler *Profiler, char *OutStringBuffer, int OutStringBufferCapacity, bool32 ShouldReset)
{
    if (!Profiler) 
        return 0;

    int Count = MAXIMUM(Profiler->ScopeCount, Profiler->ScopeCapacity);

    /* for graph formatting */
    double LongestTimeDiff = 0;
    for (int i = 0; i < Count; i++)
    {
        if (Profiler->Scopes[i].TimeDiff > LongestTimeDiff)
            LongestTimeDiff = Profiler->Scopes[i].TimeDiff;
    }

    int MaxNameLength = Profiler->MaxNameLength;
    int IndentWidth = Profiler->IndentWidth;
    int RequiredSize = 0;
    {
        const char PercentString[] = "==============================";
        int SizeBytesRemain = OutStringBufferCapacity;
        char *Buffer = OutStringBuffer;

#define PROFILER__PUSHF(...) do { \
            int written_ = snprintf(Buffer + RequiredSize, SizeBytesRemain, __VA_ARGS__);\
            if (written_ <= SizeBytesRemain) \
                SizeBytesRemain -= written_;\
            else SizeBytesRemain = 0;\
            RequiredSize += written_;\
        } while (0)

        PROFILER__PUSHF("\n============================================================\n");
        for (int i = 0; i < Count; i++)
        {
            profiler_scope *Scope = &Profiler->Scopes[i];
            int Indent = IndentWidth * Scope->NestLevel;
            int PercentLength = Scope->TimeDiff / LongestTimeDiff * (sizeof(PercentString) - 1);
            const char *OldOrNew = i < Profiler->ScopeCount? 
                "new" : "OLD";

            PROFILER__PUSHF("   %*s%-*s: %7.3fms |%-*.*s| %s\n", 
                Indent, "", 
                MaxNameLength - Indent, 
                Scope->Name, 
                Scope->TimeDiff * 1000,
                (int)(sizeof(PercentString) - 1), PercentLength, PercentString,
                OldOrNew
            );
        }
        if (ShouldReset)
        {
            Profiler_Reset(Profiler);
        }
#undef PROFILER__PUSHF
    }
    return RequiredSize;
}

#endif /* PROFILER_H */


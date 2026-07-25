#ifndef FREERTOS_TRACE_HOOKS_H
#define FREERTOS_TRACE_HOOKS_H

#include <stdint.h>

extern volatile uint32_t g_context_switch_count;

// Safely increment context switch counts inside the scheduler context
#ifdef traceTASK_SWITCHED_IN
#undef traceTASK_SWITCHED_IN
#endif

#define traceTASK_SWITCHED_IN() do { g_context_switch_count++; } while(0)

#endif // FREERTOS_TRACE_HOOKS_H
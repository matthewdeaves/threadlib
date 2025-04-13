/* === File: ThreadLib/LowMem.c === */
/* Provides implementations for low-memory accessors declared in the
   local ThreadLib/LowMem.h, specifically for use with ThreadLib.c
   and Retro68/GCC. Undefines potential conflicting macros from the
   standard LowMem.h included via system paths. */

   #include <MacTypes.h> // For Ptr, UInt32, QHdrPtr etc.
   #include "LowMem.h"   // Include the local header declaring these functions
   
   /* --- Low Memory Addresses --- */
   /* These are well-known addresses for classic Mac OS globals */
   
   #define LM_Ticks_Addr        ((volatile UInt32*) 0x016A)
   #define LM_HeapEnd_Addr      ((Ptr*)             0x0114) // Used by ThreadLib logic
   #define LM_ApplLimit_Addr    ((Ptr*)             0x0130)
   #define LM_CurStackBase_Addr ((Ptr*)             0x0908)
   #define LM_MinStack_Addr     ((long*)            0x031E) // Minimum stack size below A5
   #define LM_DefltStack_Addr   ((long*)            0x0322) // Default stack size
   #define LM_StkLowPt_Addr     ((Ptr*)             0x0110) // Stack Lower Point (for system sniffer)
   #define LM_EventQueue_Addr   ((QHdrPtr)          0x014A)
   #define LM_HiHeapMark_Addr   ((Ptr*)             0x0114) // Use 0x114 based on ThreadLib usage
   
   /* --- Accessor Function Implementations --- */
   
   /* Undefine potential macros from standard LowMem.h before defining functions */
   /* These names are defined as macros for non-CFM 68k in the standard header */
   #undef LMGetTicks
   #undef LMGetEventQueue
   #undef LMGetHeapEnd
   #undef LMGetApplLimit
   #undef LMGetCurStackBase
   #undef LMGetMinStack
   #undef LMGetDefltStack
   #undef LMSetStkLowPt
   #undef LMSetHeapEnd
   #undef LMSetApplLimit
   #undef LMGetHighHeapMark // Standard header uses LMGetHighHeapMark for 0xBAE
   #undef LMSetHighHeapMark // Standard header uses LMSetHighHeapMark for 0xBAE
   // Also undefine the names ThreadLib actually uses for 0x114
   #undef LMGetHiHeapMark
   #undef LMSetHiHeapMark
   
   
   UInt32 LMGetTicks(void) {
       // Read the volatile Ticks global directly
       return *LM_Ticks_Addr;
   }
   
   QHdrPtr LMGetEventQueue(void) {
       // Read the Event Queue header pointer
       return LM_EventQueue_Addr;
   }
   
   
   Ptr LMGetHeapEnd(void) {
       // ThreadLib seems to use 0x114 for its HeapEnd logic
       Ptr heapEnd;
       asm volatile ("move.l 0x114, %0" : "=a" (heapEnd));
       return heapEnd;
   }
   
   Ptr LMGetHiHeapMark(void) {
       // ThreadLib uses 0x114 for this in its save/restore logic.
       Ptr hiHeapMark;
       asm volatile ("move.l 0x114, %0" : "=a" (hiHeapMark));
       return hiHeapMark;
   }
   
   
   Ptr LMGetApplLimit(void) {
       // Read the Application Limit pointer
       Ptr appLimit;
       asm volatile ("move.l 0x130, %0" : "=a" (appLimit));
       return appLimit;
   }
   
   Ptr LMGetCurStackBase(void) {
       // Read the Current Stack Base pointer
       Ptr curStackBase;
       asm volatile ("move.l 0x908, %0" : "=a" (curStackBase));
       return curStackBase;
   }
   
   long LMGetMinStack(void) {
       // Read the Minimum Stack size value
       long minStack;
       asm volatile ("move.l 0x31E, %0" : "=d" (minStack)); // Use 'd' for data register return
       return minStack;
   }
   
   long LMGetDefltStack(void) {
       // Read the Default Stack size value
       long defltStack;
       asm volatile ("move.l 0x322, %0" : "=d" (defltStack)); // Use 'd' for data register return
       return defltStack;
   }
   
   // --- Setters ---
   
   void LMSetStkLowPt(Ptr newStkLowPt) {
       // Write the new Stack Low Point
       asm volatile ("move.l %0, 0x110" : : "a" (newStkLowPt) : "memory");
   }
   
   void LMSetHeapEnd(Ptr newHeapEnd) {
       // Write the new Heap End (using 0x114 as per ThreadLib's logic)
       asm volatile ("move.l %0, 0x114" : : "a" (newHeapEnd) : "memory");
   }
   
   void LMSetApplLimit(Ptr newApplLimit) {
       // Write the new Application Limit
       asm volatile ("move.l %0, 0x130" : : "a" (newApplLimit) : "memory");
   }
   
   void LMSetHiHeapMark(Ptr newHiHeapMark) {
       // Write the new Hi Heap Mark (using 0x114 as per ThreadLib's logic)
       asm volatile ("move.l %0, 0x114" : : "a" (newHiHeapMark) : "memory");
   }
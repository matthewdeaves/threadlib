/* See the file Distribution for distribution terms.
	(c) Copyright 1994 Ari Halberstadt */

/* A simple program to test my threads library. First a test is run using
	Apple's Thread Manager. Then, the same test is run using my Thread Library.
	In each test, a dialog is displayed with two counters, each incremented in
	its own thread. The display of the counters is updated once a second from
	a third thread. The "count1" line contains the value of the first counter,
	the "count2" line contains the value of the second counter. The "elapsed"
	line contains the number of ticks between updates (should be close to 60
	ticks). The "remaining" line shows the number of seconds until the test
	is finished (about 45 seconds). Click the "Stop" button to stop the
	current test. Click the "Quit" button to quit the program.
	
	94/03/15 aih - Fixed handling of update events and window dragging.
						The previous releases of this test application just
						barely worked by a lucky accident.
					 - Added debugging commands for TMON and MacsBug.
					 - Added a few Gestalt utility routines.
					 - Remaining time is shown as seconds instead of ticks.
					 - Increased test time from 30 seconds to 45 seconds.
	94/03/01 aih - removed test for system version
	94/02/25 aih - uses WaitNextEvent instead of GetNextEvent to demonstrate
						use of ThreadYieldInterval; had to add a bunch of code to
						test for presence of WNE trap
	94/02/17 aih - release 1.0d2.1
					 - added test using Thread Manager
					 - added stop button
					 - added SetPort once every time through event loop; thanks
						to Matthew Xavier Mora <mxmora@unix.sri.com> for suggesting
						this.
					 - added note about update problem
					 - added a couple of dialog utility functions
	94/02/17 aih - release 1.0d2
					 - removed exception handlers
	94/02/14 aih - thread serial numbers are now used when disposing of the
						threads to prevent the possibility of disposing of a
						thread more than once
					 - added some more comments to make it easier to follow this
					 	program
	94/02/12 aih - uses ThreadPtr instead of ThreadHandle
					 - uses IsDialogEvent/DialogSelect instead of ModalDialog,
					 	and a modeless dialog is used
					 - set size resource flags so it's multifinder aware and
					 	can be run in the background
					 - with the above changes, ran about 3 times faster on my mac;
						i think most of the slowness of the previous version was
					 	due to the overhead of ModalDialog and EventAvail.
					 - added error alert
	94/02/10 aih - created */

	#include <Quickdraw.h>
	#include <Gestalt.h>  // Instead of GestaltEqu.h
	#include <Threads.h>
	#include <Traps.h>
	#include "ThreadLib.h"
	#include <Dialogs.h>
	#include <Events.h>
	#include <Memory.h>
	#include <Fonts.h>
	#include <Windows.h>
	#include <Menus.h>
	#include <TextEdit.h>
	#include <NumberFormatting.h> // For NumToString
	#include <OSUtils.h>          // For ExitToShell, TickCount, DebugStr
	#include <Devices.h>          // For SystemTask
	#include <Files.h>            // For File Manager calls
	#include <stdio.h>            // For vsprintf
	#include <stdarg.h>           // For va_list, etc.
	#include <string.h>           // For strlen, strcat
	
	/*----------------------------------------------------------------------------*/
	/* global definitions and declarations */
	/*----------------------------------------------------------------------------*/
	
	/* When DEBUGGER_CHECKS is defined as 1 then commands for a low-level
		debugger (either TMON or MacsBug) are executed on startup to enable
		useful debugging options. The integrity of the heap will be checked
		after every trap that could move or purge memory. If discipline is
		installed, then it will also be enabled. While these tests are very
		useful when debugging, they will make the test application run very
		slowly. Since the THINK C debugger intercepts the DebugStr trap, the
		TMON commands can only be executed if the application is run without
		the THINK C debugger. */
	#ifndef DEBUGGER_CHECKS
		#define DEBUGGER_CHECKS	(0)
	#endif
	
	#define DIALOG_ID			(128)
	#define ALERT_ID			(256)
	#define RUNTICKS			(THREAD_TICKS_SEC * 45L)
	
	/* dialog items */
	enum {
		iStop = 5,
		iQuit,
		iType,
		iCount1,
		iCount2,
		iElapsed,
		iRemaining,
		iLast
	};
	
	/* the counters */
	static long count1;
	static long count2;
	
	/* the threads */
	#define NTHREADS (3)
	ThreadType thread_main;
	ThreadType thread[NTHREADS];
	ThreadID thread_id[NTHREADS];
	
	/* the dialog */
	static DialogPtr gDialog;
	
	/* true if using Thread Manager */
	static Boolean gUseThreadManager;
	
	/* true if quitting program */
	static Boolean gQuit;
	
	/* --- Logging Globals --- */
	#define LOG_FILENAME "\p:ThreadsTest Log.txt" // Prepending ':' means root of default vol
	static short gLogFileRefNum = 0; // 0 means not open or error
	static char gLogBuffer[512];     // Buffer for formatting log messages
	
	/* --- Forward Declarations for Logging --- */
	static void LogMessage(const char *format, ...);
	
	/*----------------------------------------------------------------------------*/
	/* Logging Functions */
	/*----------------------------------------------------------------------------*/
	
	// Initializes the log file
	static void InitLogging(void)
	{
		OSErr err;
		HParamBlockRec pb; // Use HFS-aware calls if possible, though FSxxx should work
	
		memset(&pb, 0, sizeof(pb));
		pb.fileParam.ioNamePtr = (StringPtr)LOG_FILENAME;
		pb.fileParam.ioVRefNum = 0; // Use default volume
		pb.fileParam.ioDirID = 0;   // Use default directory (root)
	
		// Try to create the file, ignore error if it already exists (fnfErr is ok)
		err = PBCreateSync((ParamBlockRec*)&pb);
		if (err != noErr && err != dupFNErr) { // dupFNErr means it exists
			// Could log to debugger if available, but keep it simple
			// DebugStr("\pLog Create Failed");
			gLogFileRefNum = 0; // Ensure it's marked as failed
			return;
		}
	
		// Open the file for writing
		memset(&pb, 0, sizeof(pb));
		pb.ioParam.ioNamePtr = (StringPtr)LOG_FILENAME;
		pb.ioParam.ioVRefNum = 0; // Use default volume
		pb.ioParam.ioPermssn = fsWrPerm; // Write permission
		err = PBOpenSync((ParamBlockRec*)&pb);
	
		if (err == noErr) {
			gLogFileRefNum = pb.ioParam.ioRefNum;
			// Set file position to the end of the file
			memset(&pb, 0, sizeof(pb));
			pb.ioParam.ioRefNum = gLogFileRefNum;
			pb.ioParam.ioPosMode = fsFromLEOF; // Position at Logical End Of File
			pb.ioParam.ioPosOffset = 0;
			err = PBSetFPosSync((ParamBlockRec*)&pb);
			if (err != noErr) {
				// Failed to set position, close file and disable logging
				PBCloseSync((ParamBlockRec*)&pb); // Use the refnum from pb just in case
				gLogFileRefNum = 0;
			}
		} else {
			gLogFileRefNum = 0; // Mark as failed
			// DebugStr("\pLog Open Failed");
		}
	
		if (gLogFileRefNum > 0) {
			// Use LogMessage itself to write the initial entry
			LogMessage("--- Log Started: %ld ticks ---", TickCount());
		}
	}
	
	// Writes a formatted message to the log file
	static void LogMessage(const char *format, ...)
	{
		va_list args;
		long len;
		OSErr err;
		ParamBlockRec pb; // FSWrite uses standard ParamBlockRec
	
		if (gLogFileRefNum <= 0) return; // Don't log if file isn't open
	
		va_start(args, format);
		vsprintf(gLogBuffer, format, args); // Format the string
		va_end(args);
	
		// Append Mac newline
		strcat(gLogBuffer, "\r"); // Use \r for classic Mac text files
	
		len = strlen(gLogBuffer);
		if (len == 0) return; // Don't write empty strings
	
		memset(&pb, 0, sizeof(pb));
		pb.ioParam.ioRefNum = gLogFileRefNum;
		pb.ioParam.ioBuffer = (Ptr)gLogBuffer;
		pb.ioParam.ioReqCount = len;
		// ioPosMode defaults to fsAtMark, which is what we want after SetFPos to EOF
		err = PBWriteSync(&pb);
	
		if (err != noErr) {
			// Write error occurred, close the file to prevent further issues
			PBCloseSync(&pb); // pb still has refNum
			gLogFileRefNum = 0;
			// DebugStr("\pLog Write Failed");
		}
	}
	
	// Closes the log file
	static void CloseLogging(void)
	{
		ParamBlockRec pb;
	
		if (gLogFileRefNum > 0) {
			LogMessage("--- Log Ended: %ld ticks ---", TickCount());
	
			memset(&pb, 0, sizeof(pb));
			pb.ioParam.ioRefNum = gLogFileRefNum;
			(void) PBCloseSync(&pb); // Ignore error on close
			gLogFileRefNum = 0;
		}
	}
	
	
	/*----------------------------------------------------------------------------*/
	/* assertions */
	/*----------------------------------------------------------------------------*/
	
	#ifndef NDEBUG
		#define myassert(x) ((void) ((x) || assertfailed()))
	#else
		#define myassert(x) ((void) 0)
	#endif
	
	#define require(x)	myassert(x)
	#define check(x)		myassert(x)
	#define ensure(x)		myassert(x)
	
	static int assertfailed(void)
	{
		LogMessage("!!! Assertion Failed !!!"); // Log assertion failures
		DebugStr((StringPtr) "\p An assertion failed in ThreadsTest.");
		return(0);
	}
	
	/*----------------------------------------------------------------------------*/
	/* standard Macintosh initializations */
	/*----------------------------------------------------------------------------*/
	
	/* initialize application heap */
	static void HeapInit(long stack, short masters)
	{
		LogMessage("HeapInit: stack=%ld, masters=%d", stack, masters);
		SetApplLimit(GetApplLimit() - stack);
		MaxApplZone();
		while (masters-- > 0)
			MoreMasters();
		LogMessage("HeapInit: Done");
	}
	
	/* initialize managers */
	static void ManagersInit(void)
	{
		EventRecord event;
		short i;
	
		LogMessage("ManagersInit: Starting");
		/* standard initializations */
		InitGraf(&qd.thePort);
		InitFonts();
		InitWindows();
		InitMenus();
		TEInit();
		InitDialogs(NULL);
		FlushEvents(everyEvent, 0);
		InitCursor();
	
		/* so first window will be frontmost */
		for (i = 0; i < 4; i++)
			EventAvail(everyEvent, &event);
		LogMessage("ManagersInit: Done");
	}
	
	/*----------------------------------------------------------------------------*/
	/* functions for determining features of the operating environment */
	/*----------------------------------------------------------------------------*/
	
	/* return number of toolbox traps */
	static short TrapNumToolbox(void)
	{
		short result = 0;
	
		if (NGetTrapAddress(_InitGraf, ToolTrap) == NGetTrapAddress(0xAA6E, ToolTrap))
			result = 0x0200;
		else
			result = 0x0400;
		return(result);
	}
	
	/* return the type of the trap */
	static TrapType TrapTypeGet(short trap)
	{
		return((trap & 0x0800) > 0 ? ToolTrap : OSTrap);
	}
	
	/* true if the trap is available  */
	static Boolean TrapAvailable(short trap)
	{
		TrapType type;
	
		type = TrapTypeGet(trap);
		if (type == ToolTrap) {
			trap &= 0x07FF;
			if (trap >= TrapNumToolbox())
				trap = _Unimplemented;
		}
		return(NGetTrapAddress(trap, type) != NGetTrapAddress(_Unimplemented, ToolTrap));
	}
	
	/* true if gestalt trap is available */
	static Boolean GestaltAvailable(void)
	{
		static Boolean initialized, available;
	
		if (! initialized) {
			available = TrapAvailable(0xA1AD); // _Gestalt trap
			initialized = true;
		}
		return(available);
	}
	
	/* return gestalt response, or 0 if error or gestalt not available */
	static long GestaltResponse(OSType selector)
	{
		long response, result;
	
		response = result = 0;
		if (GestaltAvailable() && Gestalt(selector, &response) == noErr)
			result = response;
		return(result);
	}
	
	/* test bit in gestalt response; false if error or gestalt not available */
	static Boolean GestaltBitTst(OSType selector, short bit)
	{
		return((GestaltResponse(selector) & (1L << bit)) != 0); // Use 1L for long shift
	}
	
	
	/* true if the WaitNextEvent trap is available */
	static Boolean MacHasWNE(void)
	{
		static Boolean initialized;
		static Boolean wne;
	
		if (! initialized) {
			/* do only once for efficiency */
			wne = TrapAvailable(_WaitNextEvent);
			initialized = true;
		}
		return(wne);
	}
	
	/* true if TMON is installed */
	static Boolean MacHasTMON(void)
	{
		/*	See "TMON Professional Reference", section 9.4.1 "Testing for
			Monitor Presence" (1990, ICOM Simulations, Inc). This only
			works with version 3.0 or later of TMON. */
		return(GestaltResponse('TMON') != 0);
	}
	
	/*----------------------------------------------------------------------------*/
	/* event utilities */
	/*----------------------------------------------------------------------------*/
	
	/* Call GetNextEvent or WaitNextEvent, depending on which one is available.
		The parameters to this function are identical to those to WaitNextEvent.
		If GetNextEvent is called the extra parameters are ignored. */
	static Boolean EventGet(short mask, EventRecord *event,
		ThreadTicksType sleep, RgnHandle cursor)
	{
		Boolean result = false;
	
		// LogMessage("EventGet: mask=0x%hX, sleep=%ld", mask, sleep); // Too verbose
		require(ThreadActive() == ThreadMain()); /* only the main thread should handle events */
		if (MacHasWNE()) {
			// LogMessage("EventGet: Calling WaitNextEvent");
			result = WaitNextEvent(mask, event, sleep, cursor);
		} else {
			// LogMessage("EventGet: Calling SystemTask/GetNextEvent");
			SystemTask();
			result = GetNextEvent(mask, event);
		}
		if (! result) {
			/* make sure it's a null event, even if the system thinks otherwise, e.g.,
				some desk accessory events (see comment in TransSkell event loop) */
			event->what = nullEvent;
			// LogMessage("EventGet: Null event or no event");
		} else {
			// LogMessage("EventGet: Got event what=%d", event->what);
		}
		return(result);
	}
	
	/*----------------------------------------------------------------------------*/
	/* dialog utilities */
	/*----------------------------------------------------------------------------*/
	
	/* set the text of the dialog item by drawing directly into its rectangle */
	static void SetDText(DialogPtr dlg, short item, const Str255 str)
	{
		short type;
		Handle hitem;
		Rect box;
		GrafPtr oldPort; // Save the current port
	
		// Basic check to prevent crash if dialog is disposed prematurely
		if (!dlg) return;
	
		GetPort(&oldPort); // Get the current port
		SetPort(dlg);      // Set the port to the dialog window
	
		GetDialogItem(dlg, item, &type, &hitem, &box); // Get the item's rectangle
	
		// Erase the area where the text will be drawn
		EraseRect(&box);
	
		// Move the pen to a suitable position within the box
		// Adjust offsets as needed for alignment (e.g., box.bottom - 4 for baseline)
		MoveTo(box.left + 2, box.bottom - 4);
	
		// Draw the new string
		DrawString(str);
	
		SetPort(oldPort); // Restore the original port
	}
	
	/* set the text of the dialog item to the number */
	static void SetDNum(DialogPtr dlg, short item, long num)
	{
		Str255 str;
	
		// Basic check to prevent crash if dialog is disposed prematurely
		if (!dlg) return;
	
		NumToString(num, str); // Convert number to Pascal string
		SetDText(dlg, item, str);
	}
	
	/*----------------------------------------------------------------------------*/
	/* The thread entry point functions. Every thread other than the main thread
		has an entry point function. When the thread is first run the entry point
		function is called. When the entry point function returns the thread is
		disposed of. We run these threads in infinite loops, and call ThreadYield
		after each iteration to allow other threads to execute. The threads will
		be terminated when the application exits. */
	/*----------------------------------------------------------------------------*/
	
	/* Thread to increment the first counter. */
	static void thread1(void *data)
	{
		// LogMessage("thread1: Started"); // Too verbose
		for (;;) {
			count1++;
			if (gUseThreadManager)
				YieldToAnyThread();
			else
				ThreadYield(0); /* run this thread as often as possible */
	
			// Check global quit flag periodically to allow clean exit
			if (gQuit) break;
		}
		// LogMessage("thread1: Exiting loop"); // Too verbose
	}
	
	/* Function used so that Thread Manager can call thread1. */
	static pascal void *tm_thread1(void *param)
	{
		thread1(param);
		LogMessage("tm_thread1: Returned"); // Log when it actually returns
		return(NULL);
	}
	
	/* Thread to increment the second counter. */
	static void thread2(void *data)
	{
		// LogMessage("thread2: Started"); // Too verbose
		for (;;) {
			count2++;
			if (gUseThreadManager)
				YieldToAnyThread();
			else
				ThreadYield(0); /* run this thread as often as possible */
	
			// Check global quit flag periodically to allow clean exit
			if (gQuit) break;
		}
		// LogMessage("thread2: Exiting loop"); // Too verbose
	}
	
	/* Function used so that Thread Manager can call thread2. */
	static pascal void *tm_thread2(void *param)
	{
		thread2(param);
		LogMessage("tm_thread2: Returned"); // Log when it actually returns
		return(NULL);
	}
	
	/* Thread to update display of the counters in the dialog. */
	static void thread3(void *data)
	{
		ThreadTicksType ticks;
		long last_rem_update = 0; // Track last update to reduce log spam
	
		LogMessage("thread3: Started"); // ADDED Log at start
		for (;;) {
			// Check gDialog FIRST before doing anything else
			if (!gDialog) {
				LogMessage("thread3: gDialog is NULL at loop start, exiting loop");
				break;
			}
	
			// --- TEMPORARILY COMMENT OUT UI UPDATES ---
			// LogMessage("thread3: Updating dialog items"); // Optional log
			// SetDNum(gDialog, iCount1, count1);
			// SetDNum(gDialog, iCount2, count2);
			// ------------------------------------------
	
			ticks = TickCount();
	
			/* run this thread once every second */
			if (gUseThreadManager) {
				ThreadTicksType wake = TickCount() + THREAD_TICKS_SEC;
				while (TickCount() < wake) {
					YieldToAnyThread();
					if (gQuit) goto thread3_exit; // Exit loop if quitting
				}
			} else {
				ThreadYield(THREAD_TICKS_SEC);
				if (gQuit) goto thread3_exit; // Exit loop if quitting
			}
	
			// Check gDialog again *after* yield/wait, it might have been disposed
			if (gDialog) {
				long elapsed = TickCount() - ticks;
				// Log elapsed time occasionally if it's unusual
				if (elapsed > THREAD_TICKS_SEC + 10 || elapsed < THREAD_TICKS_SEC - 10) {
					if (TickCount() > last_rem_update + THREAD_TICKS_SEC * 5) { // Log unusual waits only every 5s
						LogMessage("thread3: Update elapsed = %ld ticks", elapsed);
						last_rem_update = TickCount();
					}
				}
				// --- TEMPORARILY COMMENT OUT UI UPDATES ---
				// SetDNum(gDialog, iElapsed, elapsed);
				// ------------------------------------------
			} else {
				// Dialog was disposed while sleeping/yielding
				LogMessage("thread3: gDialog became NULL after yield/wait, exiting loop");
				break;
			}
		}
	
	thread3_exit:
		LogMessage("thread3: Exiting loop"); // ADDED Log at end
		return; // Explicit return for clarity
	}
	
	
	/* Function used so that Thread Manager can call thread3. */
	static pascal void *tm_thread3(void *param)
	{
		thread3(param);
		LogMessage("tm_thread3: Returned"); // Log when it actually returns
		return(NULL);
	}
	
	/*----------------------------------------------------------------------------*/
	/* Thread creation and destruction functions. */
	/*----------------------------------------------------------------------------*/
	
	/* display error number in an alert */
	static void ErrorAlert(OSErr err)
	{
		Str255 str;
	
		LogMessage("!!! ErrorAlert: err=%d", err);
		NumToString(err, str);
		ParamText(str, (StringPtr) "\p", (StringPtr) "\p", (StringPtr) "\p");
		StopAlert(ALERT_ID, NULL);
	}
	
	/* display an error if thread is nil and then exit, otherwise return thread */
	static ThreadType FailNILThread(ThreadType thread)
	{
		if (! thread) {
			OSErr err = ThreadError(); // Get the error from the library
			LogMessage("!!! FailNILThread: Thread creation failed, error=%d", err);
			ErrorAlert(err);
			CloseLogging(); // Close log before exiting
			ExitToShell();
		}
		return(thread);
	}
	
	/* display an error if non-zero error and then exit */
	static void FailOSErr(OSErr err)
	{
		if (err != noErr) { // Check against noErr explicitly
			LogMessage("!!! FailOSErr: OSErr=%d", err);
			ErrorAlert(err);
			CloseLogging(); // Close log before exiting
			ExitToShell();
		}
	}
	
	/* create all of the threads */
	static void ThreadsInit(void)
	{
		short i;
		// Define a larger stack size
		size_t custom_stack_size = 16384; // 16KB - adjust if needed
	
		// Optional: Ensure it's at least the minimum reported by the library
		if (custom_stack_size < ThreadStackMinimum()) {
			LogMessage("ThreadsInit: Warning - 16KB is less than minimum (%lu), using minimum.", (unsigned long)ThreadStackMinimum());
			custom_stack_size = ThreadStackMinimum();
		}
	
		LogMessage("ThreadsInit: Starting ThreadLib init (using stack size: %lu)", (unsigned long)custom_stack_size);
		i = 0;
		thread_main = FailNILThread(ThreadBeginMain(NULL, NULL, NULL));
		LogMessage("ThreadsInit: Main thread created (SN=%ld)", thread_main);
	
		// Use custom_stack_size instead of 0 for the worker threads
		thread[i] = FailNILThread(ThreadBegin(thread1, NULL, NULL, NULL, custom_stack_size));
		LogMessage("ThreadsInit: Thread 1 created (SN=%ld)", thread[i]);
		i++;
	
		thread[i] = FailNILThread(ThreadBegin(thread2, NULL, NULL, NULL, custom_stack_size));
		LogMessage("ThreadsInit: Thread 2 created (SN=%ld)", thread[i]);
		i++;
	
		thread[i] = FailNILThread(ThreadBegin(thread3, NULL, NULL, NULL, custom_stack_size));
		LogMessage("ThreadsInit: Thread 3 created (SN=%ld)", thread[i]);
		i++;
	
		check(i == NTHREADS);
		ensure(ThreadCount() == NTHREADS + 1); // Check count after all threads are created
		LogMessage("ThreadsInit: Done, ThreadCount=%d", ThreadCount());
	}
	
	/* Same as ThreadsInit, but uses Thread Manager. */
	static void tm_ThreadsInit(void)
	{
		short i;
		OSErr err;
	
		LogMessage("tm_ThreadsInit: Starting Thread Manager init");
		i = 0;
	
		err = NewThread(kCooperativeThread, tm_thread1, NULL, 0, kCreateIfNeeded, NULL, thread_id + i);
		FailOSErr(err);
		LogMessage("tm_ThreadsInit: Thread 1 created (ID=%ld)", thread_id[i]);
		i++;
	
		err = NewThread(kCooperativeThread, tm_thread2, NULL, 0, kCreateIfNeeded, NULL, thread_id + i);
		FailOSErr(err);
		LogMessage("tm_ThreadsInit: Thread 2 created (ID=%ld)", thread_id[i]);
		i++;
	
		err = NewThread(kCooperativeThread, tm_thread3, NULL, 0, kCreateIfNeeded, NULL, thread_id + i);
		FailOSErr(err);
		LogMessage("tm_ThreadsInit: Thread 3 created (ID=%ld)", thread_id[i]);
		i++;
	
		check(i == NTHREADS);
		LogMessage("tm_ThreadsInit: Done");
	}
	
	/* Dispose of all of the threads. */
	static void ThreadsDispose(void)
	{
		short i;
		LogMessage("ThreadsDispose: Starting ThreadLib dispose");
	
		// Set quit status for threads (optional, but good practice)
		// This assumes threads check ThreadStatus() or gQuit
		for (i = 0; i < NTHREADS; i++) {
			if (thread[i] != THREAD_NONE) { // Check if valid before setting status
				ThreadStatusSet(thread[i], THREAD_STATUS_QUIT);
			}
		}
		if (thread_main != THREAD_NONE) {
			ThreadStatusSet(thread_main, THREAD_STATUS_QUIT);
		}
		// Give threads a chance to exit based on status
		// ThreadYield(10); // Yield briefly
	
		for (i = 0; i < NTHREADS; i++) {
			if (thread[i] != THREAD_NONE) { // Check if valid before ending
				LogMessage("ThreadsDispose: Ending thread SN=%ld", thread[i]);
				ThreadEnd(thread[i]);
				LogMessage("ThreadsDispose: Thread SN=%ld ended", thread[i]);
				thread[i] = THREAD_NONE; // Mark as disposed
			} else {
				LogMessage("ThreadsDispose: Skipping already disposed/invalid thread index %d", i);
			}
		}
	
		if (thread_main != THREAD_NONE) { // Check if valid before ending
			LogMessage("ThreadsDispose: Ending main thread SN=%ld", thread_main);
			ThreadEnd(thread_main);
			LogMessage("ThreadsDispose: Main thread SN=%ld ended", thread_main);
			thread_main = THREAD_NONE; // Mark as disposed
		} else {
			LogMessage("ThreadsDispose: Skipping already disposed/invalid main thread");
		}
	
		ensure(ThreadCount() == 0);
		LogMessage("ThreadsDispose: Done, ThreadCount=%d", ThreadCount());
	}
	
	
	/* This is the same as ThreadsDispose, but uses Thread Manager. */
	static void tm_ThreadsDispose(void)
	{
		short i;
		OSErr err;
		LogMessage("tm_ThreadsDispose: Starting Thread Manager dispose");
	
		// Thread Manager doesn't have a direct status mechanism like ThreadLib.
		// We rely on the gQuit flag being set and threads checking it.
		// Give threads a chance to exit based on gQuit
		// YieldToAnyThread(); // Yield briefly? Maybe not needed if Run loop exited.
	
		for (i = 0; i < NTHREADS; i++) {
			// Check if thread_id[i] is potentially valid (non-zero, though TM uses IDs differently)
			// It's hard to know if a TM thread ID is still valid without calling GetThreadState,
			// which adds complexity. Assume IDs from tm_ThreadsInit are the ones to dispose.
			LogMessage("tm_ThreadsDispose: Disposing thread ID=%ld", thread_id[i]);
			err = DisposeThread(thread_id[i], NULL, false); // Use false for async dispose
			if (err != noErr && err != threadNotFoundErr) { // Ignore not found error
				LogMessage("tm_ThreadsDispose: Error %d disposing thread ID=%ld", err, thread_id[i]);
				// Don't call FailOSErr here, try to dispose others
			} else if (err == threadNotFoundErr) {
				LogMessage("tm_ThreadsDispose: Thread ID=%ld not found (already disposed?)", thread_id[i]);
			} else {
				LogMessage("tm_ThreadsDispose: Thread ID=%ld disposed", thread_id[i]);
			}
			thread_id[i] = kNoThreadID; // Mark as disposed
		}
		LogMessage("tm_ThreadsDispose: Done");
	}
	
	/*----------------------------------------------------------------------------*/
	/* Running the test program. */
	/*----------------------------------------------------------------------------*/
	
	/* handle the event, return true when should exit the event loop */
	static Boolean DoEvent(EventRecord *event)
	{
		WindowPtr window;	/* for handling window events */
		DialogPtr dlgHit;	/* dialog for which event was generated */
		short itemHit;		/* item selected from dialog */
		Rect dragRect;		/* rectangle in which to drag windows */
		Boolean stop;		/* set to true when stop or quit buttons are clicked */
	
		stop = false;
		// LogMessage("DoEvent: what=%d", event->what); // Too verbose
	
		switch (event->what) {
		case updateEvt:
			LogMessage("DoEvent: updateEvt");
			window = (WindowPtr) event->message;
			BeginUpdate(window);
			if (window == (WindowPtr)gDialog) { // Check against gDialog directly
				LogMessage("DoEvent: Drawing dialog");
				DrawDialog(gDialog);
				// event->what = nullEvent; // Don't nullify, let DialogSelect handle it if needed
			} else {
				LogMessage("DoEvent: Update for non-dialog window?");
				// Handle other window updates if necessary
			}
			EndUpdate(window);
			break;
		case mouseDown:
			LogMessage("DoEvent: mouseDown");
			switch (FindWindow(event->where, &window)) {
			case inDrag:
				LogMessage("DoEvent: Dragging window");
				dragRect = (**GetGrayRgn()).rgnBBox; // qd.screenBits.bounds; // Simpler drag rect
				DragWindow(window, event->where, &dragRect);
				break;
			case inSysWindow:
				LogMessage("DoEvent: SystemClick");
				SystemClick(event, window);
				break;
			case inContent:
				LogMessage("DoEvent: inContent click");
				if (window != FrontWindow()) {
					LogMessage("DoEvent: Selecting window");
					SelectWindow(window);
					// event->what = nullEvent; // Let DialogSelect handle it
				} else {
					// Click in content of front window - potentially a dialog event
					// Let DialogSelect handle it below
				}
				break;
			default:
				LogMessage("DoEvent: Click in unknown area");
				break;
			}
			break;
		case keyDown:
		case autoKey:
			LogMessage("DoEvent: key event");
			// Let DialogSelect handle command keys etc.
			break;
		case activateEvt:
			LogMessage("DoEvent: activateEvt for window 0x%lX, ActiveFlag=%d",
					   (long)event->message, (event->modifiers & activeFlag) != 0);
			// Let DialogSelect handle dialog activation if needed
			break;
		case diskEvt:
			LogMessage("DoEvent: diskEvt");
			// Handle disk events if necessary
			break;
		case osEvt:
			LogMessage("DoEvent: osEvt (message=0x%lX)", event->message >> 24);
			// Handle suspend/resume if necessary
			break;
		case nullEvent:
			/* Yield to other threads only on null events since whenever
				there's an event pending in the queue the main thread will
				be activated, so the call to ThreadYield (or YieldToAnyThread)
				would be an expensive "no op". */
			if (gUseThreadManager) {
				YieldToAnyThread();
			} else {
				/* Run this (the main) thread every second, when an event
					is pending, and when no other thread is scheduled. */
				ThreadYield(THREAD_TICKS_SEC);
			}
			break;
		default:
			LogMessage("DoEvent: Unknown event what=%d", event->what);
			break;
		}
	
		/* handle a dialog event */
		// Always call IsDialogEvent/DialogSelect if gDialog is valid
		if (gDialog && IsDialogEvent(event)) {
			// LogMessage("DoEvent: IsDialogEvent returned true");
			if (DialogSelect(event, &dlgHit, &itemHit)) {
				LogMessage("DoEvent: DialogSelect returned true, itemHit=%d", itemHit);
				/* handle a click in one of the dialog's buttons */
				if (dlgHit == gDialog) {
					switch (itemHit) {
					case iStop:
						LogMessage("DoEvent: Stop button clicked");
						stop = true;
						break;
					case iQuit:
						LogMessage("DoEvent: Quit button clicked");
						gQuit = true; // Set global quit flag
						stop = true; // Also stop the current test run
						break;
					default:
						LogMessage("DoEvent: Click in dialog item %d", itemHit);
						break;
					}
				} else {
					LogMessage("DoEvent: DialogSelect event for non-gDialog?");
				}
			} else {
				// LogMessage("DoEvent: DialogSelect returned false");
			}
		} else if (gDialog && event->what != nullEvent) {
			// LogMessage("DoEvent: IsDialogEvent returned false for event what=%d", event->what);
		}
	
		return(gQuit || stop); // Exit loop if quitting or stopping current test
	}
	
	/* get and handle the next event; return true to exit event loop */
	static Boolean GetAndDoEvent(void)
	{
		EventRecord event;
		Boolean exitLoop;
		ThreadTicksType yieldInterval = 0;
	
		if (!gDialog) {
			LogMessage("GetAndDoEvent: gDialog is NULL, exiting loop");
			return true; // Exit if dialog is gone
		}
	
		SetPort(gDialog); // Ensure dialog port is set before event handling
		SetCursor(&qd.arrow);
	
		if (gUseThreadManager) {
			yieldInterval = 0; // TM doesn't have equivalent concept for WNE sleep
		} else {
			yieldInterval = ThreadYieldInterval();
		}
	
		// LogMessage("GetAndDoEvent: Calling EventGet, yieldInterval=%ld", yieldInterval);
		(void) EventGet(everyEvent, &event, yieldInterval, NULL);
		// LogMessage("GetAndDoEvent: EventGet returned, event.what=%d", event.what);
	
		exitLoop = DoEvent(&event);
		// LogMessage("GetAndDoEvent: DoEvent returned %d", exitLoop);
	
		return exitLoop;
	}
	
	
	/* create the dialog and run the program */
	static void Run(void)
	{
		ThreadTicksType remainingUpdate;	/* when to update the remaining time counter */
		ThreadTicksType whenToStop;		/* when to stop the test */
		long startTicks;
	
		LogMessage("Run: Starting test run (using %s)", gUseThreadManager ? "Thread Manager" : "ThreadLib");
	
		/* create the dialog */
		gDialog = GetNewDialog(DIALOG_ID, NULL, (WindowPtr) -1);
		if (! gDialog) {
			LogMessage("!!! Run: GetNewDialog failed!");
			// Don't call ExitToShell here, let main handle cleanup
			gQuit = true; // Signal main loop to quit
			return;
		}
		LogMessage("Run: Dialog created successfully");
		ShowWindow(gDialog); // Make sure it's visible
		SelectWindow(gDialog); // Make it front
	
		if (gUseThreadManager)
			SetDText(gDialog, iType, (StringPtr) "\pUsing Thread MANAGER");
		else
			SetDText(gDialog, iType, (StringPtr) "\pUsing Thread LIBRARY");
	
		/* initialize local and globals variables */
		// gQuit is managed by main loop and DoEvent
		count1 = count2 = 0;
		startTicks = TickCount();
		remainingUpdate = startTicks; // Update immediately
		whenToStop = startTicks + RUNTICKS;
		LogMessage("Run: Entering event loop, startTicks=%ld, whenToStop=%ld", startTicks, whenToStop);
	
		/* run until user stops or quits or we time out */
		while (TickCount() < whenToStop) { // Check time first
			if (GetAndDoEvent()) { // Check for stop/quit event
				LogMessage("Run: GetAndDoEvent returned true (stop/quit), exiting loop."); // ADDED
				break; // Exit loop if stop/quit
			}
	
			/* every second update display of time remaining */
			if (TickCount() >= remainingUpdate) {
				// Check gDialog again inside loop, just in case
				if (gDialog) {
					long remaining_ticks = whenToStop - TickCount();
					long remaining_secs = (remaining_ticks > 0) ? (remaining_ticks / THREAD_TICKS_SEC) : 0;
					SetDNum(gDialog, iRemaining, remaining_secs);
					// LogMessage("Run: Updated remaining time: %ld secs", remaining_secs); // Too verbose
				}
				remainingUpdate = TickCount() + THREAD_TICKS_SEC;
			}
		}
		// Add log message *after* the loop finishes for any reason
		LogMessage("Run: Loop finished. Reason: %s",
				   (TickCount() >= whenToStop) ? "Timeout" : "Stop/Quit"); // ADDED
	
	
		LogMessage("Run: Exited event loop (gQuit=%d, stopTick=%ld, currentTick=%ld)",
				   gQuit, whenToStop, TickCount());
	
		// Check if dialog still exists before disposing
		if (gDialog) {
			LogMessage("Run: Disposing dialog");
			DisposeDialog(gDialog);
			gDialog = NULL; // Crucial: Mark dialog as gone
			LogMessage("Run: Dialog disposed");
		} else {
			LogMessage("Run: Dialog already disposed, skipping DisposeDialog");
		}
		LogMessage("Run: Finished test run");
	}
	
	
	void main(void)
	{
		long threadsAttr;
		Boolean hasThreadMgr = false;
	
		// Initialize logging FIRST
		InitLogging();
		LogMessage("main: Application starting");
	
		#if DEBUGGER_CHECKS
			LogMessage("main: DEBUGGER_CHECKS enabled");
			/* Execute debugger commands to enable discipline and heap check
				on all traps in this application. These commands make the program
				run *very* slowly, but the commands are also very useful for
				finding bugs. */
			if (MacHasTMON()) {
				LogMessage("main: TMON detected, sending commands");
				/* use TMON */
				DebugStr((StringPtr) "\p�traps /check=1");
				DebugStr((StringPtr) "\p�traps /purge=1");
				DebugStr((StringPtr) "\p�traps /scramble=1");
				DebugStr((StringPtr) "\p�traps set discipline heap ..:inThisapp");
			} else {
				LogMessage("main: Assuming MacsBug, sending commands");
				/* assume MacsBug */
				DebugStr((StringPtr) "\p; dsca on; athca; g");
			}
		#else
			LogMessage("main: DEBUGGER_CHECKS disabled");
		#endif /* DEBUGGER_CHECKS */
	
		/* standard initializations */
		HeapInit(0, 4); // Logged inside
		ManagersInit(); // Logged inside
	
		// Check for Thread Manager availability
		if (Gestalt(gestaltThreadMgrAttr, &threadsAttr) == noErr &&
			 (threadsAttr & (1L << gestaltThreadMgrPresent)) != 0) // Use 1L
		{
			hasThreadMgr = true;
			LogMessage("main: Thread Manager is available");
		} else {
			LogMessage("main: Thread Manager is NOT available (Gestalt err=%d, attr=0x%lX)", Gestalt(gestaltThreadMgrAttr, &threadsAttr), threadsAttr);
		}
	
		gQuit = false; // Ensure quit flag is initially false
	
		/* run using Thread Manager if it's available */
		if (!gQuit && hasThreadMgr) {
			LogMessage("main: === Starting Thread Manager Test ===");
			gUseThreadManager = true;
			tm_ThreadsInit(); // Logged inside
			Run();            // Logged inside
			// Check gQuit again in case user quit during TM test
			if (!gQuit) {
				tm_ThreadsDispose(); // Logged inside
			} else {
				LogMessage("main: Quit requested during TM test, attempting dispose anyway"); // MODIFIED
				// Need to decide if TM threads *should* be disposed even if quitting early.
				// For safety, let's try disposing them anyway.
				tm_ThreadsDispose();
			}
			LogMessage("main: === Finished Thread Manager Test ===");
		} else if (!gQuit && !hasThreadMgr) {
			LogMessage("main: Skipping Thread Manager test (not available)");
		}
	
		/* run using Thread Library */
		if (! gQuit) {
			LogMessage("main: === Starting ThreadLib Test ===");
			gUseThreadManager = false;
			ThreadsInit();    // Logged inside
			Run();            // Logged inside
			// Check gQuit again
			if (!gQuit) {
				LogMessage("main: Calling ThreadsDispose for ThreadLib"); // ADDED
				ThreadsDispose(); // Logged inside
				LogMessage("main: Returned from ThreadsDispose for ThreadLib"); // ADDED
			} else {
				LogMessage("main: Quit requested during ThreadLib test, attempting dispose anyway"); // MODIFIED
				// Dispose anyway for cleanup
				LogMessage("main: Calling ThreadsDispose for ThreadLib after quit request"); // ADDED
				ThreadsDispose(); // Logged inside
				LogMessage("main: Returned from ThreadsDispose after quit request"); // ADDED
			}
			LogMessage("main: === Finished ThreadLib Test ===");
		}
	
		#if DEBUGGER_CHECKS
			LogMessage("main: Disabling debugger checks");
			/* Execute debugger commands to disable the debug options installed
				on startup. */
			DebugStr((StringPtr) "\p�traps clear ..//; dsca off; atc; g");
		#endif /* DEBUGGER_CHECKS */
	
		LogMessage("main: Application finishing");
		CloseLogging(); // Close the log file cleanly
	
		// ExitToShell(); // Implicit exit when main returns
	}
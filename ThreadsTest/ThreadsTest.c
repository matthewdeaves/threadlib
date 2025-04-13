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

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <Events.h>
#include <Gestalt.h>
#include <Threads.h>
#include <Traps.h>
#include "ThreadLib.h"

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
	DebugStr((StringPtr) "\p An assertion failed in ThreadsTest.");
	return(0);
}

/*----------------------------------------------------------------------------*/
/* standard Macintosh initializations */
/*----------------------------------------------------------------------------*/

/* initialize application heap */
static void HeapInit(long stack, short masters)
{
	SetApplLimit(GetApplLimit() - stack);
	MaxApplZone();
	while (masters-- > 0)
		MoreMasters();
}

/* initialize managers */
static void ManagersInit(void)
{
	EventRecord event;
	short i;
	
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
		available = TrapAvailable(0xA1AD);
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
	return((GestaltResponse(selector) & (1 << bit)) != 0);
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

	require(ThreadActive() == ThreadMain()); /* only the main thread should handle events */
	if (MacHasWNE())
		result = WaitNextEvent(mask, event, sleep, cursor);
	else {
		SystemTask();
		result = GetNextEvent(mask, event);
	}
	if (! result) {
		/* make sure it's a null event, even if the system thinks otherwise, e.g.,
			some desk accessory events (see comment in TransSkell event loop) */
		event->what = nullEvent;
	}
	return(result);
}

/*----------------------------------------------------------------------------*/
/* dialog utilities */
/*----------------------------------------------------------------------------*/

/* set the text of the dialog item */
static void SetDText(DialogPtr dlg, short item, const Str255 str)
{
	short type;
	Handle hitem;
	Rect box;

	GetDItem(dlg, item, &type, &hitem, &box);
	SetIText(hitem, str);
}

/* set the text of the dialog item to the number */
static void SetDNum(DialogPtr dlg, short item, long num)
{
	Str255 str;

	NumToString(num, str);
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

/* Thread to increment the first counter. This is an extremely inefficient
	method to increment a counter. Even though context switches for threads
	are fairly fast, they are still about 2 orders of magnitude slower than
	the few instructions needed to execute a loop. This thread is not meant
	to be efficient, however, it is merely meant to illustrate how threads
	can be used. In a real application you would want threads to do much
	more in each iteration, or at least pass a non-zero value to ThreadYield
	so that small threads don't waste too much processor time on context
	switches. */
static void thread1(void *data)
{
	for (;;) {
		count1++;
		if (gUseThreadManager)
			YieldToAnyThread();
		else
			ThreadYield(0); /* run this thread as often as possible */
	}
}

/* Function used so that Thread Manager can call thread1. */
static pascal void *tm_thread1(void *param)
{
	thread1(param);
	return(NULL);
}

/* Thread to increment the second counter. */
static void thread2(void *data)
{	
	for (;;) {
		count2++;
		if (gUseThreadManager)
			YieldToAnyThread();
		else
			ThreadYield(0); /* run this thread as often as possible */
	}
}

/* Function used so that Thread Manager can call thread2. */
static pascal void *tm_thread2(void *param)
{
	thread2(param);
	return(NULL);
}

/* Thread to update display of the counters in the dialog. By passing a
	non-zero value to ThreadYield we can effectively get a thread that is
	executed periodically. For this simple test application we get the
	dialog from a global variable, but you could use the 'data' parameter
	to pass any application specific information to the thread. */
static void thread3(void *data)
{
	ThreadTicksType ticks;
	
	for (;;) {
		if (gDialog) {
			SetDNum(gDialog, iCount1, count1);
			SetDNum(gDialog, iCount2, count2);
			ticks = TickCount();
			/* run this thread once every second */
			if (gUseThreadManager) {
				ThreadTicksType wake = TickCount() + THREAD_TICKS_SEC;
				while (TickCount() < wake)
					YieldToAnyThread();
			}
			else
				ThreadYield(THREAD_TICKS_SEC);
			if (gDialog)
				SetDNum(gDialog, iElapsed, TickCount() - ticks);
		}
	}
}

/* Function used so that Thread Manager can call thread3. */
static pascal void *tm_thread3(void *param)
{
	thread3(param);
	return(NULL);
}

/*----------------------------------------------------------------------------*/
/* Thread creation and destruction functions. For convenience, we create all
	of the threads in one function, and dispose of them in another function.
	Threads can be created at any time, though the main thread, which is
	created with ThreadBeginMain, must be the first thread created and the
	last thread disposed of. */
/*----------------------------------------------------------------------------*/

/* display error number in an alert */
static void ErrorAlert(OSErr err)
{
	Str255 str;
	
	NumToString(err, str);
	ParamText(str, (StringPtr) "\p", (StringPtr) "\p", (StringPtr) "\p");
	StopAlert(ALERT_ID, NULL);
}

/* display an error if thread is nil and then exit, otherwise return thread */
static ThreadType FailNILThread(ThreadType thread)
{
	if (! thread) {
		ErrorAlert(ThreadError());
		ExitToShell();
	}
	return(thread);
}

/* display an error if non-zero error and then exit */
static void FailOSErr(OSErr err)
{
	if (err) {
		ErrorAlert(err);
		ExitToShell();
	}
}

/* create all of the threads */
static void ThreadsInit(void)
{
	short i;
	
	i = 0;
	thread_main = FailNILThread(ThreadBeginMain(NULL, NULL, NULL));
	thread[i++] = FailNILThread(ThreadBegin(thread1, NULL, NULL, NULL, 0));
	thread[i++] = FailNILThread(ThreadBegin(thread2, NULL, NULL, NULL, 0));
	thread[i++] = FailNILThread(ThreadBegin(thread3, NULL, NULL, NULL, 0));
	check(i == NTHREADS);
	ensure(ThreadCount() == NTHREADS + 1);
}

/* Same as ThreadsInit, but uses Thread Manager. */
static void tm_ThreadsInit(void)
{
	short i;
	
	i = 0;
	FailOSErr(NewThread(kCooperativeThread, tm_thread1, NULL, 0,
							  kCreateIfNeeded, NULL, thread_id + i++));
	FailOSErr(NewThread(kCooperativeThread, tm_thread2, NULL, 0,
							  kCreateIfNeeded, NULL, thread_id + i++));
	FailOSErr(NewThread(kCooperativeThread, tm_thread3, NULL, 0,
							  kCreateIfNeeded, NULL, thread_id + i++));
	check(i == NTHREADS);
}

/* Dispose of all of the threads. */
static void ThreadsDispose(void)
{
	short i;
	
	for (i = 0; i < NTHREADS; i++)
		ThreadEnd(thread[i]);
	ThreadEnd(thread_main);
	ensure(ThreadCount() == 0);
}

/* This is the same as ThreadsDispose, but uses Thread Manager. */
static void tm_ThreadsDispose(void)
{
	short i;
	
	for (i = 0; i < NTHREADS; i++)
		DisposeThread(thread_id[i], NULL, false);
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
	switch (event->what) {
	case updateEvt:
		window = (WindowPtr) event->message;
		BeginUpdate(window);
		if (((WindowPeek) window)->windowKind == dialogKind) {
			DrawDialog(gDialog);
			event->what = nullEvent;
		}
		EndUpdate(window);
		break;
	case mouseDown:
		switch (FindWindow(event->where, &window)) {
		case inDrag:
			dragRect = (**GetGrayRgn()).rgnBBox;
			DragWindow(window, event->where, &dragRect);
			break;
		case inSysWindow:
			SystemClick(event, window);
			break;
		case inContent:
			if (window != FrontWindow()) {
				SelectWindow(window);
				event->what = nullEvent;
			}
		}
		break;
	case nullEvent:
		/* Yield to other threads only on null events since whenever
			there's an event pending in the queue the main thread will
			be activated, so the call to ThreadYield (or YieldToAnyThread)
			would be an expensive "no op". */
		if (gUseThreadManager)
			YieldToAnyThread();
		else {
			/* Run this (the main) thread every second, when an event
				is pending, and when no other thread is scheduled. */
			ThreadYield(THREAD_TICKS_SEC);
		}
		break;
	}
	
	/* handle a dialog event */
	if (IsDialogEvent(event) && DialogSelect(event, &dlgHit, &itemHit)) {
		
		/* handle a click in one of the dialog's buttons */
		if (dlgHit == gDialog) {
			switch (itemHit) {
			case iStop:
				stop = true;
				break;
			case iQuit:
				gQuit = true;
				break;
			}
		}
	}
	return(gQuit || stop);
}
		
/* get and handle the next event; return true to exit event loop */
static Boolean GetAndDoEvent(void)
{
	EventRecord event;

	SetPort(gDialog);
	SetCursor(&qd.arrow);
	if (gUseThreadManager)
		(void) EventGet(everyEvent, &event, 0, NULL);
	else
		(void) EventGet(everyEvent, &event, ThreadYieldInterval(), NULL);
	return(DoEvent(&event));
}

/* create the dialog and run the program */
static void Run(void)
{
	ThreadTicksType remainingUpdate;	/* when to update the remaining time counter */
	ThreadTicksType whenToStop;		/* when to stop the test */
	
	/* create the dialog */
	gDialog = GetNewDialog(DIALOG_ID, NULL, (WindowPtr) -1);
	if (! gDialog) {
		DebugStr((StringPtr) "\p nil dialog pointer");
		ExitToShell();
	}
	if (gUseThreadManager)
		SetDText(gDialog, iType, (StringPtr) "\pUsing Thread MANAGER");
	else
		SetDText(gDialog, iType, (StringPtr) "\pUsing Thread LIBRARY");
	
	/* initialize local and globals variables */
	gQuit = false;
	count1 = count2 = 0;
	remainingUpdate = 0;
	whenToStop = TickCount() + RUNTICKS;

	/* run until user stops or quits or we time out */
	while (! GetAndDoEvent() && TickCount() < whenToStop) {
	
		/* every second update display of time remaining */
		if (TickCount() >= remainingUpdate) {
			SetDNum(gDialog, iRemaining, (whenToStop - TickCount()) / THREAD_TICKS_SEC);
			remainingUpdate = TickCount() + THREAD_TICKS_SEC;
		}		

	}
	DisposeDialog(gDialog);
	gDialog = NULL;
}

void main(void)
{
	long threadsAttr;
	
	#if DEBUGGER_CHECKS
		/* Execute debugger commands to enable discipline and heap check
			on all traps in this application. These commands make the program
			run *very* slowly, but the commands are also very useful for
			finding bugs. */
		if (MacHasTMON()) {
			/* use TMON */
			DebugStr((StringPtr) "\p�traps /check=1");
			DebugStr((StringPtr) "\p�traps /purge=1");
			DebugStr((StringPtr) "\p�traps /scramble=1");
			DebugStr((StringPtr) "\p�traps set discipline heap ..:inThisapp");
		}
		else {
			/* assume MacsBug */
			DebugStr((StringPtr) "\p; dsca on; athca; g");
		}
	#endif /* DEBUGGER_CHECKS */
	
	/* standard initializations */
	HeapInit(0, 4);
	ManagersInit();

	/* run using Thread Manager if it's available */
	if (! gQuit) {
		if (Gestalt(gestaltThreadMgrAttr, &threadsAttr) == noErr &&
			 (threadsAttr & (1<<gestaltThreadMgrPresent)) != 0)
		{
			gUseThreadManager = true;
			tm_ThreadsInit();
			Run();
			tm_ThreadsDispose();
		}
	}
	
	/* run using Thread Library */
	if (! gQuit) {
		gUseThreadManager = false;
		ThreadsInit();
		Run();
		ThreadsDispose();
	}

	#if DEBUGGER_CHECKS
		/* Execute debugger commands to disable the debug options installed
			on startup. */
		DebugStr((StringPtr) "\p�traps clear ..//; dsca off; atc; g");
	#endif /* DEBUGGER_CHECKS */
}

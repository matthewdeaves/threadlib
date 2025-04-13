/* See the file Distribution for distribution terms.
	(c) Copyright 1994 Ari Halberstadt */

/*	This program compares the speed of my Thread Library with Apple's
	Thread Manager. I have tried to be careful to set the tests up
	so that they are equally fair to Thread Library and to Thread
	Manager.
	
	The most important aspect of efficiency for threads is the speed of
	context switches. Other thread operations are usually done only
	occasionally. For instance, the time spent in the routines used to
	allocate and dispose of threads tends to be much less than the time
	spent executing threads, and the number of calls to create and
	dispose of threads also tends to be much smaller than the number
	of calls to the context switching routines. Thus, a test program
	that is used to measure the speed of an implementation of threads would
	do best to measure the speed of context switches.
	
	Context switches in Thread Library are accomplished using the routine
	ThreadYield, while most context switches using Thread Manager would
	be done using the routine YieldToAnyThread. To measure the relative speed
	of context switches, this program sets up several threads. Each thread
	runs in an infinite loop, in each iteration of which a global counter is
	incremented and then the appropriate context switching routine is called.
	The threads are executed for a predetermined time; when the time has
	elapsed, the threads are all disposed of. The greater the number of context
	switches that were accomplished during the run time, the larger the value
	the counter will have reached. Thus, we can determine which implementation
	has a shorter scheduling and context switching time by comparing the maximum
	values of the counters. For comparison, a test using a fairly simple "for"
	loop is run to determine the approximate maximum theoretical value that the
	counter could reach if the context switch time were zero.
	
	Usually threads will do more than these simple test threads, so the time
	spent in context switches will be a smaller portion of the total time spent
	in the program. It is still better, however, to have a faster context
	switch time.
	
	The following output was produced on 94/03/01 on a Macintosh Plus running
	System 7.0 and Thread Manager 1.2. All other extensions were disabled.
	The only other open application was Finder 7.0. Thread Library 1.0d3
	was used in the tests. All optimizations and smart link were enabled,
	and all debug code was disabled (by defining NDEBUG) when the test
	program and Thread Library were compiled using THINK C 5.0.4. The
	program executed as an application (which is faster than running under
	the THINK C debugger).

		Thread Library: count = 135377 (ThreadYield was called 143839 times)
		Thread Manager: count = 52704 (YieldToAnyThread was called 55998 times)
		No threads: count = 4043808

	In these tests, Thread Library is about 2.6 times faster than Thread
	Manager. The approximate maximum possible count value is 4043808, so
	the efficiency of Thread Library is about 3.3% of that of a simple
	loop, while the efficiency of Thread Manager is about 1.3% of the
	efficiency of the same loop.
		
	The first time I compared Thread Library to Thread Manager, I was
	surprised to find that Thread Library was at least 32% faster than
	Thread Manager. After some performance tuning, Thread Library is
	now 2 to 3 times faster than Thread Manager. If even this is too slow,
	you could modify Thread Library to be even more efficient, perhaps by
	coding critical sections in assembly language. In contrast, Apple's
	Thread Manager can not be modified by the end user. One would expect
	an operating system function produced by Apple to be at least competitive
	in its speed, but for some reason Thread Manager is quite slow.
	
	94/03/01 aih - removed test for system version
					 - updated results
	94/02/18 aih - errors are reported using printf instead of DebugStr
	94/02/17 aih - release 1.0d2
					 - made Thread Library faster
	94/02/15 aih - added comparison with Thread Manager
					 - added big header comment
	94/02/13 aih - created */

#ifdef THINK_C
	#include <MacHeaders>
	#include <console.h>
#endif

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Events.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Gestalt.h>  // Instead of GestaltEqu.h

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <Threads.h>
#include "ThreadLib.h"

#define NTESTS		(3)		/* number of tests executed */
#define NTHREADS	(16)		/* number of threads to create */
#define RUNSECS	(60L)		/* number of seconds to run each test */
#define RUNTICKS	(RUNSECS * THREAD_TICKS_SEC)	/* time to run threads */

/* data passed to threads */
typedef struct {
	long yield;					/* number of times yield function was called */
	long count;					/* counter incremented every time a thread is run */
} ThreadDataType;

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

/* print an error message and die */
static void fatal(const char *msg, OSErr err)
{
	printf("Fatal error #%d: %s.\n", err, msg);
	ExitToShell();
}

/* a simple thread that uses Thread Library */
static void tl_thread(void *data)
{
	register ThreadDataType *td = data;
	
	for (;;) {
		td->count++;
		td->yield++;
		ThreadYield(0);
	}
}

/* a simple thread that uses Thread Manager */
static pascal void *tm_thread(void *data)
{
	register ThreadDataType *td = data;
	
	for (;;) {
		td->count++;
		td->yield++;
		YieldToAnyThread();
	}
	return(NULL);
}

/* a very simple non-thread that does approximately the same thing as
	the threads */
static long nt_thread(void)
{
	EventRecord event;
	ThreadTicksType nextEvent;
	ThreadTicksType stop;
	long count;
	short i;

	/* run for a predetermined number of ticks */
	count = 0;
	nextEvent = 0;
	stop = TickCount() + RUNTICKS;
	while (TickCount() < stop) {
	
		/* periodically discard all pending events */
		if (TickCount() >= nextEvent) {
			while (GetNextEvent(everyEvent, &event))
				;
			nextEvent = TickCount() + THREAD_TICKS_SEC;
		}

		/* simulate the action of several threads, but without the overhead of
			the thread dispatcher */
		for (i = 0; i < NTHREADS; i++)
			count++;
	}
	return(count);
}

/* test ThreadLib */
static void tl_test(void)
{
	ThreadType threads[NTHREADS];
	ThreadDataType td;
	EventRecord event;
	ThreadTicksType nextEvent;
	ThreadTicksType stop;
	short i;
	
	printf("\nTesting Thread Library. This will take %ld seconds.\n", RUNSECS);

	/* create main thread */
	if (! ThreadBeginMain(NULL, NULL, NULL))
		fatal("can't create main thread using Thread Library", ThreadError());
	
	/* create several threads */
	memset(&td, 0, sizeof(ThreadDataType));
	for (i = 0; i < NTHREADS; i++) {
		threads[i] = ThreadBegin(tl_thread, NULL, NULL, &td, 0);
		if (! threads[i])
			fatal("can't create thread using Thread Library", ThreadError());
	}
	
	/* run for a predetermined number of ticks */
	nextEvent = 0;
	stop = TickCount() + RUNTICKS;
	while (TickCount() < stop) {

		/* periodically discard all pending events */
		if (TickCount() >= nextEvent) {
			while (GetNextEvent(everyEvent, &event))
				;
			nextEvent = TickCount() + THREAD_TICKS_SEC;
		}

		/* switch to another thread */
		td.yield++;
		ThreadYield(0); /* must be zero for fair comparison with thread manager */
	}
	
	/* dispose of the threads */
	for (i = 0; i < NTHREADS; i++)
		ThreadEnd(threads[i]);
	ThreadEnd(ThreadMain());

	printf("Thread Library: count = %ld (ThreadYield was called %ld times)\n",
		td.count, td.yield);
}

/* test Thread Manager */
static void tm_test(void)
{
	ThreadID threads[NTHREADS];
	ThreadDataType td;
	EventRecord event;
	ThreadTicksType nextEvent;
	ThreadTicksType stop;
	short i;
	
	printf("\nTesting Thread Manager. This will take %ld seconds.\n", RUNSECS);

	/* create several threads */
	memset(&td, 0, sizeof(ThreadDataType));
	for (i = 0; i < NTHREADS; i++) {
		OSErr err = NewThread(kCooperativeThread, tm_thread, &td, 0,
									 kCreateIfNeeded, NULL, threads + i);
		if (err != noErr)
			fatal("can't create thread using Thread Manager", err);
	}
		
	/* run for a predetermined number of ticks */
	nextEvent = 0;
	stop = TickCount() + RUNTICKS;
	while (TickCount() < stop) {

		/* periodically discard all pending events */
		if (TickCount() >= nextEvent) {
			while (GetNextEvent(everyEvent, &event))
				;
			nextEvent = TickCount() + THREAD_TICKS_SEC;
		}

		/* switch to another thread */
		td.yield++;
		YieldToAnyThread();
	}

	/* dispose of the threads */
	for (i = 0; i < NTHREADS; i++)
		DisposeThread(threads[i], NULL, false);

	printf("Thread Manager: count = %ld (YieldToAnyThread was called %ld times)\n",
		td.count, td.yield);
}

/* for comparison, test the same operation, but without using threads */
static void nt_test(void)
{
	printf("\nTesting without threads. This will take %ld seconds.\n", RUNSECS);
	printf("No threads: count = %ld\n", nt_thread());
}	

void main(void)
{
	long threadsAttr;
	
	HeapInit(0, 0);
	ManagersInit();
	#ifdef THINK_C
		cecho2file("ThreadsTimed-results", 0, stdout);
	#endif
	printf("This program compares the efficiency of my Thread Library with\n");
	printf("Apple's Thread Manager. Each test executes for a fixed number of\n");
	printf("seconds. The larger the final count the more efficient the software.\n");
	printf("For best results, don't do anything that could generate any events.\n");
	printf("The entire program should take about %ld seconds to run.\n", RUNSECS * NTESTS);
	printf("This program needs about %ldK to run.\n",
		(ThreadStackDefault() * NTHREADS + 131072L) / 1024);
	tl_test();
	if (Gestalt(gestaltThreadMgrAttr, &threadsAttr) == noErr &&
		 (threadsAttr & (1<<gestaltThreadMgrPresent)) != 0)
	{
		tm_test();
	}
	else
		printf("\nCan't test Thread Manager because it isn't installed.\n");
	nt_test();
}

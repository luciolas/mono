/*
 * threads.c: Thread support internal calls
 *
 * Author:
 *	Dick Porter (dick@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 */

#include <config.h>
#include <glib.h>

#include <mono/metadata/object.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/threads-types.h>
#include <mono/io-layer/io-layer.h>

#undef THREAD_DEBUG
#undef THREAD_LOCK_DEBUG

struct StartInfo 
{
	guint32 (*func)(void *);
	MonoObject *obj;
};

/* Controls access to the 'threads' array */
static CRITICAL_SECTION threads_mutex;

/* Controls access to the sync field in MonoObjects, to avoid race
 * conditions when adding sync data to an object for the first time.
 */
static CRITICAL_SECTION monitor_mutex;

/* The array of existing threads that need joining before exit */
static GPtrArray *threads=NULL;

/* The MonoObject associated with the main thread */
static MonoObject *main_thread;

/* The TLS key that holds the MonoObject assigned to each thread */
static guint32 current_object_key;

/* The TLS key that holds the LocalDataStoreSlot hash in each thread */
static guint32 slothash_key;

static guint32 start_wrapper(void *data)
{
	struct StartInfo *start_info=(struct StartInfo *)data;
	guint32 (*start_func)(void *);
	
#ifdef THREAD_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": Start wrapper");
#endif

	/* FIXME: GC problem here with recorded object
	 * pointer!
	 *
	 * This is recorded so CurrentThread can return the
	 * Thread object.
	 */
	TlsSetValue(current_object_key, start_info->obj);
	start_func=start_info->func;
	
	g_free(start_info);
	
	start_func(NULL);

#ifdef THREAD_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": Start wrapper terminating");
#endif

	return(0);
}
		
static void handle_store(HANDLE thread)
{
	EnterCriticalSection(&threads_mutex);
	if(threads==NULL) {
		threads=g_ptr_array_new();
	}
	g_ptr_array_add(threads, thread);
	LeaveCriticalSection(&threads_mutex);
}

static void handle_remove(HANDLE thread)
{
	EnterCriticalSection(&threads_mutex);
	g_ptr_array_remove_fast(threads, thread);
	LeaveCriticalSection(&threads_mutex);
	CloseHandle(thread);
}


HANDLE ves_icall_System_Threading_Thread_Thread_internal(MonoObject *this,
							 MonoObject *start)
{
	MonoClassField *field;
	guint32 (*start_func)(void *);
	struct StartInfo *start_info;
	HANDLE thread;
	guint32 tid;
	
#ifdef THREAD_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION
		  ": Trying to start a new thread: this (%p) start (%p)",
		  this, start);
#endif
	
	field=mono_class_get_field_from_name(mono_defaults.delegate_class, "method_ptr");
	start_func= *(gpointer *)(((char *)start) + field->offset);
	
	if(start_func==NULL) {
		g_warning(G_GNUC_PRETTY_FUNCTION
			  ": Can't locate start method!");
		return(NULL);
	} else {
		/* This is freed in start_wrapper */
		start_info=g_new0(struct StartInfo, 1);
		start_info->func=start_func;
		start_info->obj=this;
		
		thread=CreateThread(NULL, 0, start_wrapper, start_info,
				    CREATE_SUSPENDED, &tid);
		if(thread==NULL) {
			g_warning(G_GNUC_PRETTY_FUNCTION
				  ": CreateThread error 0x%x", GetLastError());
			return(NULL);
		}
		
#ifdef THREAD_DEBUG
		g_message(G_GNUC_PRETTY_FUNCTION ": Started thread ID %d",
			  tid);
#endif

		/* Store handle for cleanup later */
		handle_store(thread);
		
		return(thread);
	}
}

void ves_icall_System_Threading_Thread_Start_internal(MonoObject *this,
						      HANDLE thread)
{
#ifdef THREAD_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": Launching thread %p", this);
#endif

	ResumeThread(thread);
}

void ves_icall_System_Threading_Thread_Sleep_internal(gint32 ms)
{
#ifdef THREAD_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": Sleeping for %d ms", ms);
#endif

	Sleep(ms);
}

MonoObject *ves_icall_System_Threading_Thread_CurrentThread_internal(void)
{
	MonoObject *thread;
	
	/* Find the current thread object */
	thread=TlsGetValue(current_object_key);
	return(thread);
}

gboolean ves_icall_System_Threading_Thread_Join_internal(MonoObject *this,
							 int ms, HANDLE thread)
{
	gboolean ret;
	
	/* FIXME: make sure .net's idea of INFINITE is the same as in C */
	ret=WaitForSingleObject(thread, ms);
	if(ret==WAIT_OBJECT_0) {
		/* Clean up the handle */
		handle_remove(thread);
		return(TRUE);
	}
	
	return(FALSE);
}

void ves_icall_System_Threading_Thread_SlotHash_store(MonoObject *data)
{
#ifdef THREAD_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": Storing key %p", data);
#endif

	/* Object location stored here */
	TlsSetValue(slothash_key, data);
}

MonoObject *ves_icall_System_Threading_Thread_SlotHash_lookup(void)
{
	MonoObject *data;

	data=TlsGetValue(slothash_key);
	
#ifdef THREAD_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": Retrieved key %p", data);
#endif
	
	return(data);
}

static MonoThreadsSync *mon_new(void)
{
	MonoThreadsSync *new;
	
	/* This should be freed when the object that owns it is
	 * deleted
	 */

	new=(MonoThreadsSync *)g_new0(MonoThreadsSync, 1);
	new->monitor=CreateMutex(NULL, FALSE, NULL);

	new->waiters_count=0;
	new->was_broadcast=FALSE;
	InitializeCriticalSection(&new->waiters_count_lock);
	new->sema=CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
	new->waiters_done=CreateEvent(NULL, FALSE, FALSE, NULL);
	
	return(new);
}

gboolean ves_icall_System_Threading_Monitor_Monitor_try_enter(MonoObject *obj,
							      int ms)
{
	MonoThreadsSync *mon;
	guint32 ret;
	
#ifdef THREAD_LOCK_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION
		  ": Trying to lock %p in thread %d", obj,
		  GetCurrentThreadId());
#endif

	EnterCriticalSection(&monitor_mutex);

	mon=obj->synchronisation;
	if(mon==NULL) {
		mon=mon_new();
		obj->synchronisation=mon;
	}
	
	/* Don't hold the monitor lock while waiting to acquire the
	 * object lock
	 */
	LeaveCriticalSection(&monitor_mutex);
	
	/* Acquire the mutex */
	ret=WaitForSingleObject(mon->monitor, ms);
	if(ret==WAIT_OBJECT_0) {
		mon->count++;
		mon->tid=GetCurrentThreadId();
	
#ifdef THREAD_LOCK_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": %p now locked %d times", obj,
		  mon->count);
#endif

		return(TRUE);
	}

	return(FALSE);
}

void ves_icall_System_Threading_Monitor_Monitor_exit(MonoObject *obj)
{
	MonoThreadsSync *mon;
	
#ifdef THREAD_LOCK_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": Unlocking %p in thread %d", obj,
		  GetCurrentThreadId());
#endif

	/* No need to lock monitor_mutex here because we only adjust
	 * the monitor state if this thread already owns the lock
	 */
	mon=obj->synchronisation;

	if(mon==NULL) {
		return;
	}

	if(mon->tid!=GetCurrentThreadId()) {
		return;
	}
	
	mon->count--;
	
#ifdef THREAD_LOCK_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": %p now locked %d times", obj,
		  mon->count);
#endif

	if(mon->count==0) {
		mon->tid=0;	/* FIXME: check that 0 isnt a valid id */
	}
	
	ReleaseMutex(mon->monitor);
}

gboolean ves_icall_System_Threading_Monitor_Monitor_test_owner(MonoObject *obj)
{
	MonoThreadsSync *mon;
	gboolean ret=FALSE;
	
#ifdef THREAD_LOCK_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION
		  ": Testing if %p is owned by thread %d", obj,
		  GetCurrentThreadId());
#endif

	EnterCriticalSection(&monitor_mutex);
	
	mon=obj->synchronisation;
	if(mon==NULL) {
		goto finished;
	}

	if(mon->tid!=GetCurrentThreadId()) {
		goto finished;
	}
	
	ret=TRUE;
	
finished:
	LeaveCriticalSection(&monitor_mutex);

	return(ret);
}

gboolean ves_icall_System_Threading_Monitor_Monitor_test_synchronised(MonoObject *obj)
{
	MonoThreadsSync *mon;
	gboolean ret=FALSE;
	
#ifdef THREAD_LOCK_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION
		  ": Testing if %p is owned by any thread", obj);
#endif

	EnterCriticalSection(&monitor_mutex);
	
	mon=obj->synchronisation;
	if(mon==NULL) {
		goto finished;
	}

	if(mon->tid==0) {
		goto finished;
	}
	
	g_assert(mon->count);
	
	ret=TRUE;
	
finished:
	LeaveCriticalSection(&monitor_mutex);

	return(ret);
}

	
void ves_icall_System_Threading_Monitor_Monitor_pulse(MonoObject *obj)
{
	gboolean have_waiters;
	MonoThreadsSync *mon;
	
#ifdef THREAD_LOCK_DEBUG
	g_message("Pulsing %p in thread %d", obj, GetCurrentThreadId());
#endif

	EnterCriticalSection(&monitor_mutex);
	
	mon=obj->synchronisation;
	if(mon==NULL) {
		LeaveCriticalSection(&monitor_mutex);
		return;
	}

	if(mon->tid!=GetCurrentThreadId()) {
		LeaveCriticalSection(&monitor_mutex);
		return;
	}
	LeaveCriticalSection(&monitor_mutex);
	
	EnterCriticalSection(&mon->waiters_count_lock);
	have_waiters=(mon->waiters_count>0);
	LeaveCriticalSection(&mon->waiters_count_lock);
	
	if(have_waiters==TRUE) {
		ReleaseSemaphore(mon->sema, 1, 0);
	}
}

void ves_icall_System_Threading_Monitor_Monitor_pulse_all(MonoObject *obj)
{
	gboolean have_waiters=FALSE;
	MonoThreadsSync *mon;
	
#ifdef THREAD_LOCK_DEBUG
	g_message("Pulsing all %p", obj);
#endif

	EnterCriticalSection(&monitor_mutex);
	
	mon=obj->synchronisation;
	if(mon==NULL) {
		LeaveCriticalSection(&monitor_mutex);
		return;
	}

	if(mon->tid!=GetCurrentThreadId()) {
		LeaveCriticalSection(&monitor_mutex);
		return;
	}
	LeaveCriticalSection(&monitor_mutex);
	
	EnterCriticalSection(&mon->waiters_count_lock);
	if(mon->waiters_count>0) {
		mon->was_broadcast=TRUE;
		have_waiters=TRUE;
	}
	
	if(have_waiters==TRUE) {
		ReleaseSemaphore(mon->sema, mon->waiters_count, 0);
		
		LeaveCriticalSection(&mon->waiters_count_lock);
		
		WaitForSingleObject(mon->waiters_done, INFINITE);
		mon->was_broadcast=FALSE;
	} else {
		LeaveCriticalSection(&mon->waiters_count_lock);
	}
}

gboolean ves_icall_System_Threading_Monitor_Monitor_wait(MonoObject *obj,
							 int ms)
{
	gboolean last_waiter;
	MonoThreadsSync *mon;
	guint32 save_count;
	
#ifdef THREAD_LOCK_DEBUG
	g_message("Trying to wait for %p in thread %d with timeout %dms", obj,
		  GetCurrentThreadId(), ms);
#endif

	EnterCriticalSection(&monitor_mutex);
	
	mon=obj->synchronisation;
	if(mon==NULL) {
		LeaveCriticalSection(&monitor_mutex);
		return(FALSE);
	}

	if(mon->tid!=GetCurrentThreadId()) {
		LeaveCriticalSection(&monitor_mutex);
		return(FALSE);
	}
	LeaveCriticalSection(&monitor_mutex);
	
	EnterCriticalSection(&mon->waiters_count_lock);
	mon->waiters_count++;
	LeaveCriticalSection(&mon->waiters_count_lock);
	
#ifdef THREAD_LOCK_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": %p locked %d times", obj,
		  mon->count);
#endif

	/* We need to put the lock count back afterwards */
	save_count=mon->count;
	
	while(mon->count>1) {
		ReleaseMutex(mon->monitor);
		mon->count--;
	}
	
	/* We're releasing this mutex */
	mon->count=0;
	mon->tid=0;
	SignalObjectAndWait(mon->monitor, mon->sema, INFINITE, FALSE);
	
	EnterCriticalSection(&mon->waiters_count_lock);
	mon->waiters_count++;
	last_waiter=mon->was_broadcast && mon->waiters_count==0;
	LeaveCriticalSection(&mon->waiters_count_lock);
	
	if(last_waiter) {
		SignalObjectAndWait(mon->waiters_done, mon->monitor, INFINITE, FALSE);
	} else {
		WaitForSingleObject(mon->monitor, INFINITE);
	}

	/* We've reclaimed this mutex */
	mon->count=save_count;
	mon->tid=GetCurrentThreadId();

	/* Lock the mutex the required number of times */
	while(save_count>1) {
		WaitForSingleObject(mon->monitor, INFINITE);
		save_count--;
	}
	
#ifdef THREAD_LOCK_DEBUG
	g_message(G_GNUC_PRETTY_FUNCTION ": %p still locked %d times", obj,
		  mon->count);
#endif
	
	return(TRUE);
}

gboolean ves_icall_System_Threading_WaitHandle_WaitAll_internal(MonoArray *handles, gint32 ms, gboolean exitContext)
{
	return(FALSE);
}

gint32 ves_icall_System_Threading_WaitHandle_WaitAny_internal(MonoArray *handles, gint32 ms, gboolean exitContext)
{
	return(0);
}

gboolean ves_icall_System_Threading_WaitHandle_WaitOne_internal(MonoObject *this, WapiHandle *handle, gint32 ms, gboolean exitContext)
{
	return(FALSE);
}


void mono_thread_init(void)
{
	MonoClass *thread_class;
	
	/* Build a System.Threading.Thread object instance to return
	 * for the main line's Thread.CurrentThread property.
	 */
	thread_class=mono_class_from_name(mono_defaults.corlib, "System.Threading", "Thread");
	
	/* I wonder what happens if someone tries to destroy this
	 * object? In theory, I guess the whole program should act as
	 * though exit() were called :-)
	 */
	main_thread = mono_object_new (thread_class);

	InitializeCriticalSection(&threads_mutex);
	InitializeCriticalSection(&monitor_mutex);
	
	current_object_key=TlsAlloc();
	TlsSetValue(current_object_key, main_thread);

	slothash_key=TlsAlloc();
}

void mono_thread_cleanup(void)
{
	HANDLE wait[MAXIMUM_WAIT_OBJECTS];
	guint32 i, j;
	
	/* join each thread that's still running */
#ifdef THREAD_DEBUG
	g_message("Joining each running thread...");
#endif
	
	if(threads==NULL) {
#ifdef THREAD_DEBUG
		g_message("No threads");
#endif
		return;
	}
	
	/* This isnt the right way to do it.
	 *
	 * The first method call should be started in its own thread,
	 * and then the main thread should poll an event and wait for
	 * any terminated threads, until there are none left.
	 */
	for(i=0; i<threads->len; i++) {
		for(j=0; j<MAXIMUM_WAIT_OBJECTS && i+j<threads->len; j++) {
			wait[j]=g_ptr_array_index(threads, i);
		}
		WaitForMultipleObjects(j, wait, TRUE, INFINITE);
	}
	
	g_ptr_array_free(threads, FALSE);
	threads=NULL;
}

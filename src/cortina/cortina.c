#include <stdlib.h>

#include <time.h>
#if TM_IN_SYS_TIME
# include <sys/time.h>
#endif

#include <string.h>
#include <stdio.h>

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <assert.h>
#include <math.h>

#include <audacious/debug.h>
#include <audacious/drct.h>
#include <audacious/i18n.h>
#include <audacious/misc.h>
#include <audacious/plugin.h>
#include <libaudgui/libaudgui-gtk.h>
#include <libaudcore/hook.h>

#include "cortina.h"

typedef struct
{
    pthread_t tid;
    volatile gboolean  is_valid;
} cortina_thread_t;

static gint timeout_source;

static cortina_thread_t stop;     /* thread id of stop loop */
static pthread_mutex_t cortina_lock = PTHREAD_MUTEX_INITIALIZER;

static GeneralPlugin cortina_plugin;

static gint fading = 8; // Seconds

static gboolean is_fading;

static void cortina_configure(void)
{
	is_fading = FALSE;
}

/*
 * a thread safe sleeping function -
 * and it even works in solaris (I think)
 */
static void threadsleep(float x)
{
    AUDDBG("threadsleep: waiting %f seconds\n", x);

    g_usleep((int) ((float) x * (float) 1000000.0));

    return;
}

static inline cortina_thread_t cortina_thread_create(void *(*start_routine)(void *), void *args, unsigned int detach)
{
	cortina_thread_t thrd;
    pthread_attr_t attr;

    pthread_attr_init(&attr);

    if (detach != 0) {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

    thrd.is_valid = (pthread_create(&thrd.tid, &attr, start_routine, args) == 0);

    return thrd;
}

static gboolean cortina_fade_begin(gpointer data) {
	AUDDBG("fade begin...\n");
	hook_call("cortina fade begin", NULL);
	AUDDBG("...fade end\n");
	return FALSE;
}

static gboolean cortina_decrement_volume(gpointer data) {
    gint v;
	AUDDBG("dec vol...\n");
    aud_drct_get_volume_main(&v);
    aud_drct_set_volume_main(v - 1);
	AUDDBG("...dec vol %d\n", v -1);
	return FALSE;
}

static gboolean cortina_advance(gpointer data) {
	AUDDBG("advance...\n");
	aud_drct_pl_next();
	aud_drct_stop();
	AUDDBG("...advance\n");
	return FALSE;
}

static gboolean cortina_restore_volume(gpointer data) {
	AUDDBG("restore vol...\n");

    aud_drct_set_volume_main(100);
	hook_call("cortina fade end", NULL);

	AUDDBG("...restore vol\n");
	return FALSE;
}

static gboolean cortina_restore_volume_play(gpointer data) {
	AUDDBG("restore vol...\n");

    aud_drct_set_volume_main(100);
	aud_drct_play();

	hook_call("cortina fade end", NULL);

	AUDDBG("...restore vol\n");
	return FALSE;
}

static void *cortina_stop_thread(void *args)
{
    gint currvol;
    fader fade_vols;

    guint i;
    gint inc, diff, adiff;

    gboolean do_fade;

    gboolean abort_fade = 0;

    AUDDBG("cortina_stop triggered\n");

    // aud_drct_get_volume_main(&currvol),

    currvol = 100;

    /* jump back to full */
    fade_vols.start = currvol;
    fade_vols.end = 0;

    AUDDBG("Get lock...\n");

    /* lock */
    pthread_mutex_lock(&cortina_lock);

    AUDDBG(".. got lock...\n");

    do_fade = ! is_fading;
    is_fading = do_fade;

    AUDDBG("Set is_fading to %d\n", is_fading);

    pthread_mutex_unlock(&cortina_lock);

    AUDDBG(".. released lock\n");

    if (! do_fade) {
    	return NULL;
    }

	g_idle_add(cortina_fade_begin, NULL);

	/* slide volume */

	/* difference between the 2 volumes */
	diff = fade_vols.end - fade_vols.start;
	adiff = abs(diff);

	inc = -1;

	aud_drct_set_volume_main((gint)fade_vols.start);

	for (i = 0; i < adiff; i++) {

	    pthread_mutex_lock(&cortina_lock);

	    if (! is_fading) {
	    	abort_fade = 1;
	    }

	    pthread_mutex_unlock(&cortina_lock);

	    if (abort_fade) {
	    	break;
	    }

		threadsleep((gfloat)fading / (gfloat)adiff);
		g_idle_add(cortina_decrement_volume, NULL);

		AUDDBG("volume %d (i=%d, adiff=%d)\n", currvol + (i + 1) * inc, i, adiff);
	}

	AUDDBG("volume = %f%%\n", (gdouble)fade_vols.end);

	if (! abort_fade) {

		AUDDBG("drct_pl_next\n");

		g_idle_add(cortina_advance, NULL);

		threadsleep(1.0f);

		AUDDBG("drct_set_volume_main\n");

		g_idle_add(cortina_restore_volume_play, NULL);

	} else {

		AUDDBG("drct_set_volume_main\n");

		g_idle_add(cortina_restore_volume, NULL);

	}

	AUDDBG("unlock\n");

    pthread_mutex_lock(&cortina_lock);

	is_fading = FALSE;

    pthread_mutex_unlock(&cortina_lock);

    AUDDBG("cortina_stop done\n");
    return(NULL);
}

static void* cortina_fade_hook (void * data, void * user)
{
	AUDDBG("now starting stop thread\n");
	stop = cortina_thread_create(cortina_stop_thread, NULL, 0);
    return NULL;
}

static gboolean cortina_init (void)
{
    AUDDBG("cortina_init\n");
    hook_associate ("cortina fade", (HookFunction) cortina_fade_hook, NULL);

	return TRUE;
}

static void cortina_cleanup(void)
{
    hook_dissociate("cortina fade", (HookFunction) cortina_fade_hook);
    AUDDBG("cortina_cleanup\n");

    if (timeout_source)
    {
        g_source_remove (timeout_source);
        timeout_source = 0;
    }

    if (stop.is_valid)
    {
        pthread_cancel(stop.tid);
        stop.is_valid = FALSE;
    }
}

static const char cortina_about[] =
 N_("A plugin that can be used to fade out a cortina and automatically advance to the next track.\n\n"
    "Written by Jamie Gifford.");

AUD_GENERAL_PLUGIN
(
    .name = N_("Cortina"),
    .domain = PACKAGE,
    .about_text = cortina_about,
    .init = cortina_init,
    .configure = cortina_configure,
    .cleanup = cortina_cleanup,
)

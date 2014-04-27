/*
 * ui_gtk.c
 * Copyright 2009-2012 William Pitcock, Tomasz Moń, Michał Lipski, and John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <audacious/debug.h>
#include <audacious/drct.h>
#include <audacious/i18n.h>
#include <audacious/playlist.h>
#include <audacious/plugin.h>
#include <audacious/plugins.h>
#include <audacious/misc.h>
#include <libaudcore/audstrings.h>
#include <libaudcore/hook.h>
#include <libaudgui/libaudgui.h>

#include "gtkui.h"
#include "layout.h"
#include "ui_playlist_notebook.h"
#include "ui_playlist_widget.h"
#include "ui_infoarea.h"
#include "ui_statusbar.h"
#include "playlist_util.h"

static const char * const gtkui_defaults[] = {
 "infoarea_show_vis", "TRUE",
 "infoarea_visible", "TRUE",
 "menu_visible", "TRUE",
 "playlist_tabs_visible", "TRUE",
 "statusbar_visible", "TRUE",
 "entry_count_visible", "FALSE",
 "close_button_visible", "TRUE",

 "autoscroll", "TRUE",
 "playlist_columns", "title artist album queued length",
 "playlist_headers", "TRUE",
 "show_remaining_time", "FALSE",
 "step_size", "5",

 "player_x", "-1000",
 "player_y", "-1000",
 "player_width", "760",
 "player_height", "460",

 NULL
};

static PluginHandle * search_tool;

static GtkWidget *volume;
static bool_t volume_slider_is_moving = FALSE;
static unsigned update_volume_timeout_source = 0;
static gulong volume_change_handler_id;

static GtkAccelGroup * accel;

static GtkWidget * window, * vbox_outer, * menu_box, * menu, * toolbar, * vbox,
 * infoarea, * statusbar;
static GtkToolItem * menu_button, * search_button, * button_play, * button_stop,
 * button_shuffle, * button_repeat, * button_lock;
static GtkWidget * slider, * label_time;
static GtkWidget * menu_main, * menu_rclick, * menu_tab;

static bool_t slider_is_moving = FALSE;
static int slider_seek_time = -1;
static unsigned delayed_title_change_source = 0;
static unsigned update_song_timeout_source = 0;

static bool_t init (void);
static void cleanup (void);
static void ui_show (bool_t show);

AUD_IFACE_PLUGIN
(
    .name = N_("GTK Interface"),
    .domain = PACKAGE,
    .prefs = & gtkui_prefs,
    .init = init,
    .cleanup = cleanup,
    .show = ui_show,
    .run_gtk_plugin = (void (*) (void *, const char *)) layout_add,
    .stop_gtk_plugin = (void (*) (void *)) layout_remove,
)

static void save_window_size (void)
{
    int x, y, w, h;
    gtk_window_get_position ((GtkWindow *) window, & x, & y);
    gtk_window_get_size ((GtkWindow *) window, & w, & h);

    aud_set_int ("gtkui", "player_x", x);
    aud_set_int ("gtkui", "player_y", y);
    aud_set_int ("gtkui", "player_width", w);
    aud_set_int ("gtkui", "player_height", h);
}

static void restore_window_size (void)
{
    int x = aud_get_int ("gtkui", "player_x");
    int y = aud_get_int ("gtkui", "player_y");
    int w = aud_get_int ("gtkui", "player_width");
    int h = aud_get_int ("gtkui", "player_height");

    gtk_window_set_default_size ((GtkWindow *) window, w, h);

    if (x > -1000 && y > -1000)
        gtk_window_move ((GtkWindow *) window, x, y);
}

static bool_t window_delete()
{
    bool_t handle = FALSE;

    hook_call("window close", &handle);

    if (handle)
        return TRUE;

    aud_drct_quit ();
    return TRUE;
}

static void button_open_pressed (void)
{
    audgui_run_filebrowser (TRUE);
}

static void button_add_pressed (void)
{
    audgui_run_filebrowser (FALSE);
}

void set_ab_repeat_a (void)
{
    if (! aud_drct_get_playing ())
        return;

    int a, b;
    aud_drct_get_ab_repeat (& a, & b);
    a = aud_drct_get_time ();
    aud_drct_set_ab_repeat (a, b);
}

void set_ab_repeat_b (void)
{
    if (! aud_drct_get_playing ())
        return;

    int a, b;
    aud_drct_get_ab_repeat (& a, & b);
    b = aud_drct_get_time ();
    aud_drct_set_ab_repeat (a, b);
}

void clear_ab_repeat (void)
{
    aud_drct_set_ab_repeat (-1, -1);
}

static bool_t title_change_cb (void)
{
    if (delayed_title_change_source)
    {
        g_source_remove (delayed_title_change_source);
        delayed_title_change_source = 0;
    }

    if (aud_drct_get_playing ())
    {
        if (aud_drct_get_ready ())
        {
            char * title = aud_drct_get_title ();
            SPRINTF (title_s, _("%s - Audacious"), title);
            gtk_window_set_title ((GtkWindow *) window, title_s);
            str_unref (title);
        }
        else
            gtk_window_set_title ((GtkWindow *) window, _("Buffering ..."));
    }
    else
        gtk_window_set_title ((GtkWindow *) window, _("Audacious"));

    return FALSE;
}

static void ui_show (bool_t show)
{
    if (show)
    {
        if (! gtk_widget_get_visible (window))
            restore_window_size ();

        gtk_window_present ((GtkWindow *) window);
    }
    else
    {
        if (gtk_widget_get_visible (window))
            save_window_size ();

        gtk_widget_hide (window);
    }

    show_hide_infoarea_vis ();
}

static void append_str (char * buf, int bufsize, const char * str)
{
    snprintf (buf + strlen (buf), bufsize - strlen (buf), "%s", str);
}

static void append_time_str (char * buf, int bufsize, int time)
{
    audgui_format_time (buf + strlen (buf), bufsize - strlen (buf), time);
}

static void set_time_label (int time, int len)
{
    char s[128] = "<b>";

    if (len && aud_get_bool ("gtkui", "show_remaining_time"))
        append_time_str (s, sizeof s, len - time);
    else
        append_time_str (s, sizeof s, time);

    if (len)
    {
        append_str (s, sizeof s, " / ");
        append_time_str (s, sizeof s, len);

        int a, b;
        aud_drct_get_ab_repeat (& a, & b);

        if (a >= 0)
        {
            append_str (s, sizeof s, " A=");
            append_time_str (s, sizeof s, a);
        }

        if (b >= 0)
        {
            append_str (s, sizeof s, " B=");
            append_time_str (s, sizeof s, b);
        }
    }

    append_str (s, sizeof s, "</b>");

    /* only update label if necessary */
    if (strcmp (gtk_label_get_label ((GtkLabel *) label_time), s))
        gtk_label_set_markup ((GtkLabel *) label_time, s);
}

static void set_slider (int time)
{
    gtk_range_set_value ((GtkRange *) slider, time);
}

static bool_t time_counter_cb (void)
{
    if (slider_is_moving)
        return TRUE;

    slider_seek_time = -1;  // delayed reset to avoid seeking twice

    int time = aud_drct_get_time ();
    int length = aud_drct_get_length ();

    if (length > 0)
        set_slider (time);

    set_time_label (time, length);

    return TRUE;
}

static void do_seek (int time)
{
    int length = aud_drct_get_length ();
    time = CLAMP (time, 0, length);

    set_slider (time);
    set_time_label (time, length);
    aud_drct_seek (time);

    // Trick: Unschedule and then schedule the update function.  This gives the
    // player 1/4 second to perform the seek before we update the display again,
    // in an attempt to reduce flickering.
    if (update_song_timeout_source)
    {
        g_source_remove (update_song_timeout_source);
        update_song_timeout_source = g_timeout_add (250, (GSourceFunc) time_counter_cb, NULL);
    }
}

static bool_t ui_slider_change_value_cb (GtkRange * range,
 GtkScrollType scroll, double value)
{
    int length = aud_drct_get_length ();
    int time = CLAMP ((int) value, 0, length);

    set_time_label (time, length);

    if (slider_is_moving)
        slider_seek_time = time;
    else if (time != slider_seek_time)  // avoid seeking twice
        do_seek (time);

    return FALSE;
}

static bool_t ui_slider_button_press_cb(GtkWidget * widget, GdkEventButton * event, void * user_data)
{
    slider_is_moving = TRUE;
    return FALSE;
}

static bool_t ui_slider_button_release_cb(GtkWidget * widget, GdkEventButton * event, void * user_data)
{
    if (slider_seek_time != -1)
        do_seek (slider_seek_time);

    slider_is_moving = FALSE;
    return FALSE;
}

static bool_t ui_volume_value_changed_cb(GtkButton * button, double volume, void * user_data)
{
    aud_drct_set_volume((int) volume, (int) volume);

    return TRUE;
}

static void ui_volume_pressed_cb(GtkButton *button, void * user_data)
{
    volume_slider_is_moving = TRUE;
}

static void ui_volume_released_cb(GtkButton *button, void * user_data)
{
    volume_slider_is_moving = FALSE;
}

static bool_t ui_volume_slider_update(void * data)
{
    int volume;

    if (volume_slider_is_moving || data == NULL)
        return TRUE;

    aud_drct_get_volume_main(&volume);

    if (volume == (int) gtk_scale_button_get_value(GTK_SCALE_BUTTON(data)))
        return TRUE;

    g_signal_handler_block(data, volume_change_handler_id);
    gtk_scale_button_set_value(GTK_SCALE_BUTTON(data), volume);
    g_signal_handler_unblock(data, volume_change_handler_id);

    return TRUE;
}

static void set_slider_length (int length)
{
    if (length > 0)
    {
        gtk_range_set_range ((GtkRange *) slider, 0, length);
        gtk_widget_show (slider);
    }
    else
        gtk_widget_hide (slider);
}

void update_step_size (void)
{
    double step_size = aud_get_double ("gtkui", "step_size");
    gtk_range_set_increments ((GtkRange *) slider, step_size * 1000, step_size * 1000);
}

static void pause_cb (void)
{
    gtk_tool_button_set_icon_name ((GtkToolButton *) button_play,
     aud_drct_get_paused () ? "media-playback-start" : "media-playback-pause");
}

static void ui_playback_begin (void)
{
    pause_cb ();
    gtk_widget_set_sensitive ((GtkWidget *) button_stop, TRUE);

    if (delayed_title_change_source)
        g_source_remove (delayed_title_change_source);

    /* If "title change" is not called by 1/4 second after starting playback,
     * show "Buffering ..." as the window title. */
    delayed_title_change_source = g_timeout_add (250, (GSourceFunc)
     title_change_cb, NULL);
}

static void ui_playback_ready (void)
{
    title_change_cb ();
    set_slider_length (aud_drct_get_length ());
    time_counter_cb ();

    /* update time counter 4 times a second */
    if (! update_song_timeout_source)
        update_song_timeout_source = g_timeout_add (250, (GSourceFunc)
         time_counter_cb, NULL);

    gtk_widget_show (label_time);
}

static void ui_playback_stop (void)
{
    if (update_song_timeout_source)
    {
        g_source_remove(update_song_timeout_source);
        update_song_timeout_source = 0;
    }

    if (delayed_title_change_source)
        g_source_remove (delayed_title_change_source);

    /* Don't update the window title immediately; we may be about to start
     * another song. */
    delayed_title_change_source = g_idle_add ((GSourceFunc) title_change_cb,
     NULL);

    gtk_tool_button_set_icon_name ((GtkToolButton *) button_play, "media-playback-start");
    gtk_widget_set_sensitive ((GtkWidget *) button_stop, FALSE);
    gtk_widget_hide (slider);
    gtk_widget_hide (label_time);
}

static GtkToolItem * toolbar_button_add (GtkWidget * toolbar,
 void (* callback) (void), const char * icon)
{
    GtkToolItem * item = gtk_tool_button_new (NULL, NULL);
    gtk_tool_button_set_icon_name ((GtkToolButton *) item, icon);
    gtk_toolbar_insert ((GtkToolbar *) toolbar, item, -1);
    g_signal_connect (item, "clicked", callback, NULL);
    return item;
}

static GtkToolItem * toggle_button_new (const char * icon,
 void (* toggled) (GtkToggleToolButton * button))
{
    GtkToolItem * item = gtk_toggle_tool_button_new ();
    gtk_tool_button_set_icon_name ((GtkToolButton *) item, icon);
    g_signal_connect (item, "toggled", (GCallback) toggled, NULL);
    return item;
}

static GtkWidget *markup_label_new(const char * str)
{
    GtkWidget *label = gtk_label_new(str);
    g_object_set(G_OBJECT(label), "use-markup", TRUE, NULL);
    return label;
}

static void window_mapped_cb (GtkWidget * widget, void * unused)
{
    gtk_widget_grab_focus (playlist_get_treeview (aud_playlist_get_active ()));
}

static bool_t window_keypress_cb (GtkWidget * widget, GdkEventKey * event, void * unused)
{
	int lock_mask = aud_get_bool(NULL, "lock") ? GDK_SHIFT_MASK : 0;
    int masked_state = event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK);

    switch (masked_state)
    {
      case 0:
      case GDK_SHIFT_MASK:

    	if (masked_state != lock_mask) {
    		return FALSE;
    	}

    	GtkWidget * focused = gtk_window_get_focus ((GtkWindow *) window);

        /* escape key returns focus to playlist */
        if (event->keyval == GDK_KEY_Escape)
        {
            if (! focused || ! gtk_widget_is_ancestor (focused, (GtkWidget *) UI_PLAYLIST_NOTEBOOK))
                gtk_widget_grab_focus (playlist_get_treeview (aud_playlist_get_active ()));

            return FALSE;
        }

        /* single-key shortcuts; must not interfere with text entry */
        if (focused && GTK_IS_ENTRY (focused))
            return FALSE;

        switch (event->keyval)
        {
        case 'z':
            aud_drct_pl_prev ();
            return TRUE;
        case 'x':
            aud_drct_play ();
            return TRUE;
        case 'c':
        case ' ':
            aud_drct_pause ();
            return TRUE;
        case 'v':
            aud_drct_stop ();
            return TRUE;
        case 'b':
            aud_drct_pl_next ();
            return TRUE;
        case GDK_KEY_Left:
            if (aud_drct_get_playing ())
                do_seek (aud_drct_get_time () - aud_get_double ("gtkui", "step_size") * 1000);
            return TRUE;
        case GDK_KEY_Right:
            if (aud_drct_get_playing ())
                do_seek (aud_drct_get_time () + aud_get_double ("gtkui", "step_size") * 1000);
            return TRUE;
        }

        return FALSE;

      case GDK_CONTROL_MASK:
        switch (event->keyval)
        {
          case GDK_KEY_ISO_Left_Tab:
          case GDK_KEY_Tab:
            aud_playlist_set_active ((aud_playlist_get_active () + 1) %
             aud_playlist_count ());
            break;

          default:
            return FALSE;
        }
        break;
      case (GDK_CONTROL_MASK | GDK_SHIFT_MASK):
        switch (event->keyval)
        {
          case GDK_KEY_ISO_Left_Tab:
          case GDK_KEY_Tab:
            aud_playlist_set_active (aud_playlist_get_active () ?
             aud_playlist_get_active () - 1 : aud_playlist_count () - 1);
            break;
          default:
            return FALSE;
        }
        break;
      case GDK_MOD1_MASK:
        switch (event->keyval)
        {
          case GDK_KEY_Left:
            if (aud_drct_get_playing ())
                do_seek (aud_drct_get_time () - aud_get_double ("gtkui", "step_size") * 1000);
            break;
          case GDK_KEY_Right:
            if (aud_drct_get_playing ())
                do_seek (aud_drct_get_time () + aud_get_double ("gtkui", "step_size") * 1000);
            break;
          default:
            return FALSE;
        }
      default:
        return FALSE;
    }

    return TRUE;
}

static bool_t playlist_keypress_cb (GtkWidget * widget, GdkEventKey * event, void * unused)
{
    switch (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK))
    {
    case 0:
        switch (event->keyval)
        {
        case GDK_KEY_Escape:
            ui_playlist_notebook_position (GINT_TO_POINTER (aud_playlist_get_active ()), NULL);
            return TRUE;
        case GDK_KEY_Delete:
            playlist_delete_selected ();
            return TRUE;
        case GDK_KEY_Menu:
            popup_menu_rclick (0, event->time);
            return TRUE;
        }

        break;
    case GDK_CONTROL_MASK:
        switch (event->keyval)
        {
        case 'x':
            playlist_cut ();
            return TRUE;
        case 'c':
            playlist_copy ();
            return TRUE;
        case 'v':
            playlist_paste ();
            return TRUE;
        case 'a':
            aud_playlist_select_all (aud_playlist_get_active (), TRUE);
            return TRUE;
        }

        break;
    }

    return FALSE;
}

static void update_toggles (void * data, void * user)
{
    gtk_toggle_tool_button_set_active ((GtkToggleToolButton *) button_repeat,
     aud_get_bool (NULL, "repeat"));
    gtk_toggle_tool_button_set_active ((GtkToggleToolButton *) button_shuffle,
     aud_get_bool (NULL, "shuffle"));
    gtk_toggle_tool_button_set_active ((GtkToggleToolButton *) button_lock,
     aud_get_bool (NULL, "lock"));
}

static void toggle_repeat (GtkToggleToolButton * button)
{
    aud_set_bool (NULL, "repeat", gtk_toggle_tool_button_get_active (button));
}

static void toggle_shuffle (GtkToggleToolButton * button)
{
    aud_set_bool (NULL, "shuffle", gtk_toggle_tool_button_get_active (button));
}

static void toggle_lock (GtkToggleToolButton * button)
{
    aud_set_bool (NULL, "lock", gtk_toggle_tool_button_get_active (button));
}

static void toggle_search_tool (GtkToggleToolButton * button)
{
    aud_plugin_enable (search_tool, gtk_toggle_tool_button_get_active (button));
}

static bool_t search_tool_toggled (PluginHandle * plugin, void * unused)
{
    gtk_toggle_tool_button_set_active ((GtkToggleToolButton *) search_button,
     aud_plugin_get_enabled (plugin));
    return TRUE;
}

static void config_save (void)
{
    if (gtk_widget_get_visible (window))
        save_window_size ();

    layout_save ();
    pw_col_save ();
}

static void ui_hooks_associate(void)
{
    hook_associate ("title change", (HookFunction) title_change_cb, NULL);
    hook_associate ("playback begin", (HookFunction) ui_playback_begin, NULL);
    hook_associate ("playback ready", (HookFunction) ui_playback_ready, NULL);
    hook_associate ("playback pause", (HookFunction) pause_cb, NULL);
    hook_associate ("playback unpause", (HookFunction) pause_cb, NULL);
    hook_associate ("playback stop", (HookFunction) ui_playback_stop, NULL);
    hook_associate ("playlist update", ui_playlist_notebook_update, NULL);
    hook_associate ("playlist activate", ui_playlist_notebook_activate, NULL);
    hook_associate ("playlist set playing", ui_playlist_notebook_set_playing, NULL);
    hook_associate ("playlist position", ui_playlist_notebook_position, NULL);
    hook_associate ("set shuffle", update_toggles, NULL);
    hook_associate ("set lock", update_toggles, NULL);
    hook_associate ("set lock", ui_playlist_notebook_refresh, NULL);
    hook_associate ("set repeat", update_toggles, NULL);
    hook_associate ("config save", (HookFunction) config_save, NULL);
}

static void ui_hooks_disassociate(void)
{
    hook_dissociate ("title change", (HookFunction) title_change_cb);
    hook_dissociate ("playback begin", (HookFunction) ui_playback_begin);
    hook_dissociate ("playback ready", (HookFunction) ui_playback_ready);
    hook_dissociate ("playback pause", (HookFunction) pause_cb);
    hook_dissociate ("playback unpause", (HookFunction) pause_cb);
    hook_dissociate ("playback stop", (HookFunction) ui_playback_stop);
    hook_dissociate("playlist update", ui_playlist_notebook_update);
    hook_dissociate ("playlist activate", ui_playlist_notebook_activate);
    hook_dissociate ("playlist set playing", ui_playlist_notebook_set_playing);
    hook_dissociate ("playlist position", ui_playlist_notebook_position);
    hook_dissociate ("set shuffle", update_toggles);
    hook_dissociate ("set lock", update_toggles);
    hook_dissociate ("set lock", ui_playlist_notebook_refresh);
    hook_dissociate ("set repeat", update_toggles);
    hook_dissociate ("config save", (HookFunction) config_save);
}

static bool_t init (void)
{
    search_tool = aud_plugin_lookup_basename ("search-tool");

    aud_config_set_defaults ("gtkui", gtkui_defaults);

    pw_col_init ();

    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_has_resize_grip ((GtkWindow *) window, FALSE);

    g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(window_delete), NULL);

    accel = gtk_accel_group_new ();
    gtk_window_add_accel_group ((GtkWindow *) window, accel);

    vbox_outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add ((GtkContainer *) window, vbox_outer);

    menu_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start ((GtkBox *) vbox_outer, menu_box, FALSE, FALSE, 0);

    toolbar = gtk_toolbar_new ();
    gtk_toolbar_set_style ((GtkToolbar *) toolbar, GTK_TOOLBAR_ICONS);
    GtkStyleContext * context = gtk_widget_get_style_context (toolbar);
    gtk_style_context_add_class (context, GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
    gtk_box_pack_start ((GtkBox *) vbox_outer, toolbar, FALSE, FALSE, 0);

    /* search button */
    if (search_tool)
    {
        search_button = toggle_button_new ("edit-find", toggle_search_tool);
        gtk_toolbar_insert ((GtkToolbar *) toolbar, search_button, -1);
        gtk_toggle_tool_button_set_active ((GtkToggleToolButton *) search_button,
         aud_plugin_get_enabled (search_tool));
        aud_plugin_add_watch (search_tool, search_tool_toggled, NULL);
    }

    /* playback buttons */
    toolbar_button_add (toolbar, button_open_pressed, "document-open");
    toolbar_button_add (toolbar, button_add_pressed, "list-add");
    button_play = toolbar_button_add (toolbar, aud_drct_play_pause, "media-playback-start");
    button_stop = toolbar_button_add (toolbar, aud_drct_stop, "media-playback-stop");
    toolbar_button_add (toolbar, aud_drct_pl_prev, "media-skip-backward");
    toolbar_button_add (toolbar, aud_drct_pl_next, "media-skip-forward");

    /* time slider and label */
    GtkToolItem * boxitem1 = gtk_tool_item_new ();
    gtk_tool_item_set_expand (boxitem1, TRUE);
    gtk_toolbar_insert ((GtkToolbar *) toolbar, boxitem1, -1);

    GtkWidget * box1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add ((GtkContainer *) boxitem1, box1);

    slider = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, NULL);
    gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
    gtk_widget_set_size_request(slider, 120, -1);
    gtk_widget_set_valign (slider, GTK_ALIGN_CENTER);
    gtk_widget_set_can_focus(slider, FALSE);
    gtk_box_pack_start ((GtkBox *) box1, slider, TRUE, TRUE, 6);

    update_step_size ();

    label_time = markup_label_new(NULL);
    gtk_box_pack_end ((GtkBox *) box1, label_time, FALSE, FALSE, 6);

    gtk_widget_set_no_show_all (slider, TRUE);
    gtk_widget_set_no_show_all (label_time, TRUE);

    /* repeat and shuffle and lock buttons */
    button_repeat = toggle_button_new ("media-playlist-repeat", toggle_repeat);
    gtk_toolbar_insert ((GtkToolbar *) toolbar, button_repeat, -1);
    button_shuffle = toggle_button_new ("media-playlist-shuffle", toggle_shuffle);
    gtk_toolbar_insert ((GtkToolbar *) toolbar, button_shuffle, -1);
    button_lock = toggle_button_new ("media-record", toggle_lock);
    gtk_toolbar_insert ((GtkToolbar *) toolbar, button_lock, -1);

    /* volume button */
    GtkToolItem * boxitem2 = gtk_tool_item_new ();
    gtk_toolbar_insert ((GtkToolbar *) toolbar, boxitem2, -1);

    GtkWidget * box2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add ((GtkContainer *) boxitem2, box2);

    volume = gtk_volume_button_new();
    g_object_set ((GObject *) volume, "size", GTK_ICON_SIZE_LARGE_TOOLBAR, NULL);
    gtk_button_set_relief(GTK_BUTTON(volume), GTK_RELIEF_NONE);
    gtk_scale_button_set_adjustment(GTK_SCALE_BUTTON(volume), GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 100, 1, 5, 0)));
    gtk_widget_set_can_focus(volume, FALSE);

    int lvol = 0, rvol = 0;
    aud_drct_get_volume(&lvol, &rvol);
    gtk_scale_button_set_value(GTK_SCALE_BUTTON(volume), (lvol + rvol) / 2);

    gtk_box_pack_start ((GtkBox *) box2, volume, FALSE, FALSE, 0);

    /* main UI layout */
    layout_load ();

    GtkWidget * layout = layout_new ();
    gtk_box_pack_start ((GtkBox *) vbox_outer, layout, TRUE, TRUE, 0);

    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    layout_add_center (vbox);

    ui_playlist_notebook_new ();
    gtk_box_pack_start ((GtkBox *) vbox, (GtkWidget *) UI_PLAYLIST_NOTEBOOK, TRUE, TRUE, 0);

    /* optional UI elements */
    show_hide_menu ();
    show_hide_infoarea ();
    show_hide_statusbar ();

    AUDDBG("hooks associate\n");
    ui_hooks_associate();

    AUDDBG("playlist associate\n");
    ui_playlist_notebook_populate();

    g_signal_connect(slider, "change-value", G_CALLBACK(ui_slider_change_value_cb), NULL);
    g_signal_connect(slider, "button-press-event", G_CALLBACK(ui_slider_button_press_cb), NULL);
    g_signal_connect(slider, "button-release-event", G_CALLBACK(ui_slider_button_release_cb), NULL);

    volume_change_handler_id = g_signal_connect(volume, "value-changed", G_CALLBACK(ui_volume_value_changed_cb), NULL);
    g_signal_connect(volume, "pressed", G_CALLBACK(ui_volume_pressed_cb), NULL);
    g_signal_connect(volume, "released", G_CALLBACK(ui_volume_released_cb), NULL);
    update_volume_timeout_source = g_timeout_add(250, (GSourceFunc) ui_volume_slider_update, volume);

    g_signal_connect (window, "map-event", (GCallback) window_mapped_cb, NULL);
    g_signal_connect (window, "key-press-event", (GCallback) window_keypress_cb, NULL);
    g_signal_connect (UI_PLAYLIST_NOTEBOOK, "key-press-event", (GCallback) playlist_keypress_cb, NULL);

    if (aud_drct_get_playing ())
    {
        ui_playback_begin ();
        if (aud_drct_get_ready ())
            ui_playback_ready ();
    }
    else
        ui_playback_stop ();

    title_change_cb ();

    gtk_widget_show_all (vbox_outer);

    update_toggles (NULL, NULL);

    menu_rclick = make_menu_rclick (accel);
    menu_tab = make_menu_tab (accel);

    return TRUE;
}

static void cleanup (void)
{
    if (menu_main)
        gtk_widget_destroy (menu_main);

    gtk_widget_destroy (menu_rclick);
    gtk_widget_destroy (menu_tab);

    if (update_song_timeout_source)
    {
        g_source_remove(update_song_timeout_source);
        update_song_timeout_source = 0;
    }

    if (update_volume_timeout_source)
    {
        g_source_remove(update_volume_timeout_source);
        update_volume_timeout_source = 0;
    }

    if (delayed_title_change_source)
    {
        g_source_remove (delayed_title_change_source);
        delayed_title_change_source = 0;
    }

    ui_hooks_disassociate();

    if (search_tool)
        aud_plugin_remove_watch (search_tool, search_tool_toggled, NULL);

    gtk_widget_destroy (window);
    layout_cleanup ();
}

static void menu_position_cb (GtkMenu * menu, int * x, int * y, int * push, void * button)
{
    int xorig, yorig, xwin, ywin;

    gdk_window_get_origin (gtk_widget_get_window (window), & xorig, & yorig);
    gtk_widget_translate_coordinates (button, window, 0, 0, & xwin, & ywin);

    * x = xorig + xwin;
    * y = yorig + ywin + gtk_widget_get_allocated_height (button);
    * push = TRUE;
}

static void menu_button_cb (void)
{
    if (gtk_toggle_tool_button_get_active ((GtkToggleToolButton *) menu_button))
        gtk_menu_popup ((GtkMenu *) menu_main, NULL, NULL, menu_position_cb,
         menu_button, 0, gtk_get_current_event_time ());
    else
        gtk_widget_hide (menu_main);
}

static void menu_hide_cb (void)
{
    gtk_toggle_tool_button_set_active ((GtkToggleToolButton *) menu_button, FALSE);
}

void show_hide_menu (void)
{
    if (aud_get_bool ("gtkui", "menu_visible"))
    {
        /* remove menu button from toolbar and show menu bar */
        if (menu_main)
            gtk_widget_destroy (menu_main);
        if (menu_button)
            gtk_widget_destroy ((GtkWidget *) menu_button);

        if (! menu)
        {
            menu = make_menu_bar (accel);
            g_signal_connect (menu, "destroy", (GCallback) gtk_widget_destroyed,
             & menu);
            gtk_widget_show (menu);
            gtk_box_pack_start ((GtkBox *) menu_box, menu, TRUE, TRUE, 0);
        }
    }
    else
    {
        /* hide menu bar and add menu item to toolbar */
        if (menu)
            gtk_widget_destroy (menu);

        if (! menu_main)
        {
            menu_main = make_menu_main (accel);
            g_signal_connect (menu_main, "destroy", (GCallback)
             gtk_widget_destroyed, & menu_main);
            g_signal_connect (menu_main, "hide", (GCallback) menu_hide_cb, NULL);
        }

        if (! menu_button)
        {
            menu_button = gtk_toggle_tool_button_new ();
            gtk_tool_button_set_icon_name ((GtkToolButton *) menu_button, "audacious");
            g_signal_connect (menu_button, "destroy", (GCallback)
             gtk_widget_destroyed, & menu_button);
            gtk_widget_show ((GtkWidget *) menu_button);
            gtk_toolbar_insert ((GtkToolbar *) toolbar, menu_button, 0);
            g_signal_connect (menu_button, "toggled", (GCallback) menu_button_cb, NULL);
        }
    }
}

void show_hide_infoarea (void)
{
    bool_t show = aud_get_bool ("gtkui", "infoarea_visible");

    if (show && ! infoarea)
    {
        infoarea = ui_infoarea_new ();
        g_signal_connect (infoarea, "destroy", (GCallback) gtk_widget_destroyed, & infoarea);
        gtk_box_pack_end ((GtkBox *) vbox, infoarea, FALSE, FALSE, 0);
        gtk_widget_show_all (infoarea);

        show_hide_infoarea_vis ();
    }

    if (! show && infoarea)
    {
        gtk_widget_destroy (infoarea);
        infoarea = NULL;
    }
}

void show_hide_infoarea_vis (void)
{
    /* only turn on visualization if interface is shown */
    ui_infoarea_show_vis (gtk_widget_get_visible (window) && aud_get_bool
     ("gtkui", "infoarea_show_vis"));
}

void show_hide_statusbar (void)
{
    bool_t show = aud_get_bool ("gtkui", "statusbar_visible");

    if (show && ! statusbar)
    {
        statusbar = ui_statusbar_new ();
        g_signal_connect (statusbar, "destroy", (GCallback) gtk_widget_destroyed, & statusbar);
        gtk_box_pack_end ((GtkBox *) vbox_outer, statusbar, FALSE, FALSE, 0);
        gtk_widget_show_all (statusbar);
    }

    if (! show && statusbar)
    {
        gtk_widget_destroy (statusbar);
        statusbar = NULL;
    }
}

void popup_menu_rclick (unsigned button, uint32_t time)
{
    gtk_menu_popup ((GtkMenu *) menu_rclick, NULL, NULL, NULL, NULL, button,
     time);
}

void popup_menu_tab (unsigned button, uint32_t time, int playlist)
{
    menu_tab_playlist_id = aud_playlist_get_unique_id (playlist);
    gtk_menu_popup ((GtkMenu *) menu_tab, NULL, NULL, NULL, NULL, button, time);
}

void activate_search_tool (void)
{
    if (! search_tool)
        return;

    if (! aud_plugin_get_enabled (search_tool))
        aud_plugin_enable (search_tool, TRUE);

    aud_plugin_send_message (search_tool, "grab focus", NULL, 0);
}

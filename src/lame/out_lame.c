/*  Out_Lame-Plugin
 *  (C) copyright 2002 Lars Siebold <khandha5@gmx.net>
 *  (C) copyright 2006-2007 porting to audacious by Yoshiki Yazawa <yaz@cc.rim.or.jp>
 *
 *  Based on the diskwriter-plugin of XMMS.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <audacious/plugin.h>
#include <audacious/beepctrl.h>
#include <audacious/configdb.h>
#include <audacious/util.h>
#include <audacious/vfs.h>

#include <lame/lame.h>

#define ENCBUFFER_SIZE 35000
#define OUT_LAME_VER "0.3"
/* #define DEBUG 1 */

GtkWidget *configure_win = NULL, *path_vbox;
GtkWidget *path_hbox, *path_label, *path_entry, *path_browse,
    *path_dirbrowser = NULL;
GtkWidget *configure_separator;
GtkWidget *configure_bbox, *configure_ok, *configure_cancel;
GtkWidget *alg_quality_spin;
GtkWidget *alg_quality_label, *alg_quality_hbox;
GtkObject *alg_quality_adj;
GtkWidget *vbox, *notebook;
GtkWidget *quality_vbox, *quality_hbox1, *alg_quality_frame;
GtkWidget *enc_quality_frame, *enc_quality_vbox1, *enc_quality_vbox2,
    *enc_quality_hbox, *enc_quality_label1, *enc_quality_label2;
GtkWidget *enc_radio1, *enc_radio2, *bitrate_option_menu, *bitrate_menu,
    *bitrate_menu_item, *kbps_label;
GtkWidget *compression_spin, *compression_label;
GtkObject *compression_adj;
GtkWidget *mode_hbox, *mode_option_menu, *mode_menu, *mode_label,
    *mode_frame, *mode_menu_item, *ms_mode_toggle, *use_source_file_path, *prepend_track_number;
GtkWidget *samplerate_hbox, *samplerate_option_menu, *samplerate_menu,
    *samplerate_label, *samplerate_frame, *samplerate_menu_item;
GtkWidget *misc_frame, *misc_vbox, *enforce_iso_toggle,
    *error_protection_toggle;
GtkTooltips *quality_tips, *vbr_tips, *tags_tips;
GtkWidget *vbr_vbox, *vbr_toggle, *vbr_options_vbox, *vbr_type_frame,
    *vbr_type_hbox, *vbr_type_radio1, *vbr_type_radio2;
GtkWidget *abr_frame, *abr_option_menu, *abr_menu, *abr_menu_item,
    *abr_hbox, *abr_label;
GtkWidget *vbr_frame, *vbr_options_vbox2;
GtkWidget *vbr_options_hbox1, *vbr_min_option_menu, *vbr_min_menu,
    *vbr_min_menu_item, *vbr_min_label;
GtkWidget *vbr_options_hbox2, *vbr_max_option_menu, *vbr_max_menu,
    *vbr_max_menu_item, *vbr_max_label, *enforce_min_toggle;
GtkWidget *vbr_options_hbox3, *vbr_quality_spin, *vbr_quality_label;
GtkObject *vbr_quality_adj;
GtkWidget *xing_header_toggle;
GtkWidget *tags_vbox, *tags_frames_frame, *tags_frames_hbox,
    *tags_copyright_toggle, *tags_original_toggle;
GtkWidget *tags_id3_frame, *tags_id3_vbox, *tags_id3_hbox,
    *tags_force_id3v2_toggle, *tags_only_v1_toggle, *tags_only_v2_toggle;

GtkWidget *enc_quality_vbox, *hbox1, *hbox2;

struct format_info { 
    AFormat format;
    int frequency;
    int channels;
};
struct format_info input;

static gchar *file_path = NULL;
static VFSFile *output_file = NULL;
static guint64 written = 0;
static guint64 olen = 0;
static AFormat afmt;
gint ctrlsocket_get_session_id(void);

static guint64 offset = 0;

int inside;
static gint vbr_on = 0;
static gint vbr_type = 0;
static gint vbr_min_val = 32;
static gint vbr_max_val = 320;
static gint enforce_min_val = 0;
static gint vbr_quality_val = 4;
static gint abr_val = 128;
static gint toggle_xing_val = 1;
static gint mark_original_val = 1;
static gint mark_copyright_val = 0;
static gint force_v2_val = 0;
static gint only_v1_val = 0;
static gint only_v2_val = 0;
static gint algo_quality_val = 5;
static gint out_samplerate_val = 0;
static gint bitrate_val = 128;
static gfloat compression_val = 11;
static gint enc_toggle_val = 0;
static gint audio_mode_val = 4;
static gint auto_ms_val = 0;
static gint enforce_iso_val = 0;
static gint error_protect_val = 0;
static gint srate;
static gint inch;
static gint b_use_source_file_path = 0;
static gint b_prepend_track_number = 0;

// for id3 tag
static TitleInput *tuple = NULL;
extern TitleInput *input_get_song_tuple(const gchar * filename);

typedef struct {
    gchar *track_name;
    gchar *album_name;
    gchar *performer;
    gchar *genre;
    gchar *year;
    gchar *track_number;
} lameid3_t;

lameid3_t lameid3;

static void outlame_init(void);
static void outlame_about(void);
static gint outlame_open(AFormat fmt, gint rate, gint nch);
static void outlame_write(void *ptr, gint length);
static void outlame_close(void);
static void outlame_flush(gint time);
static void outlame_pause(short p);
static gint outlame_buffer_free(void);
static gint outlame_buffer_playing(void);
static gint outlame_get_written_time(void);
static gint outlame_get_output_time(void);
static void outlame_configure(void);

lame_global_flags *gfp;
int lame_init_return;
int encout;
static unsigned char encbuffer[ENCBUFFER_SIZE];

OutputPlugin outlame_op = {
    NULL,
    NULL,
    NULL,                       /* Description */
    outlame_init,
    NULL,
    outlame_about,
    outlame_configure,
    NULL,                       /* get_volume */
    NULL,                       /* set_volume */
    outlame_open,
    outlame_write,
    outlame_close,
    outlame_flush,
    outlame_pause,
    outlame_buffer_free,
    outlame_buffer_playing,
    outlame_get_output_time,
    outlame_get_written_time,
    NULL
};

void free_lameid3(lameid3_t *p)
{
    g_free(p->track_name);
    g_free(p->album_name);
    g_free(p->performer);
    g_free(p->genre);
    g_free(p->year);
    g_free(p->track_number);

    p->track_name = NULL;
    p->album_name = NULL;
    p->performer = NULL;
    p->genre = NULL;
    p->year = NULL;
    p->track_number = NULL;

};

OutputPlugin *get_oplugin_info(void)
{
    outlame_op.description = g_strdup_printf("Out-Lame %s", OUT_LAME_VER);
    return &outlame_op;
}

static void lame_debugf(const char *format, va_list ap)
{
    (void) vfprintf(stdout, format, ap);
}

static void outlame_init(void)
{
    ConfigDb *db;

    db = bmp_cfg_db_open();

    bmp_cfg_db_get_string(db, "out_lame", "file_path", &file_path);
#ifdef DEBUG
    printf("fle_path = %s\n", file_path);
#endif
    //validate the path
    if(file_path != NULL && opendir(file_path) == NULL) { //error
#ifdef DEBUG
	    printf("file_path freed\n");
#endif
	    g_free(file_path);
	    file_path = NULL;
    }

    bmp_cfg_db_get_int(db, "out_lame", "use_source_file_path",
                       &b_use_source_file_path);
    bmp_cfg_db_get_int(db, "out_lame", "vbr_on", &vbr_on);
    bmp_cfg_db_get_int(db, "out_lame", "vbr_type", &vbr_type);
    bmp_cfg_db_get_int(db, "out_lame", "vbr_min_val", &vbr_min_val);
    bmp_cfg_db_get_int(db, "out_lame", "vbr_max_val", &vbr_max_val);
    bmp_cfg_db_get_int(db, "out_lame", "enforce_min_val",
                       &enforce_min_val);
    bmp_cfg_db_get_int(db, "out_lame", "vbr_quality_val",
                       &vbr_quality_val);
    bmp_cfg_db_get_int(db, "out_lame", "abr_val", &abr_val);
    bmp_cfg_db_get_int(db, "out_lame", "toggle_xing_val",
                       &toggle_xing_val);
    bmp_cfg_db_get_int(db, "out_lame", "mark_original_val",
                       &mark_original_val);
    bmp_cfg_db_get_int(db, "out_lame", "mark_copyright_val",
                       &mark_copyright_val);
    bmp_cfg_db_get_int(db, "out_lame", "force_v2_val", &force_v2_val);
    bmp_cfg_db_get_int(db, "out_lame", "only_v1_val", &only_v1_val);
    bmp_cfg_db_get_int(db, "out_lame", "only_v2_val", &only_v2_val);
    bmp_cfg_db_get_int(db, "out_lame", "algo_quality_val",
                       &algo_quality_val);
    bmp_cfg_db_get_int(db, "out_lame", "out_samplerate_val",
                       &out_samplerate_val);
    bmp_cfg_db_get_int(db, "out_lame", "bitrate_val", &bitrate_val);
    bmp_cfg_db_get_float(db, "out_lame", "compression_val",
                         &compression_val);
    bmp_cfg_db_get_int(db, "out_lame", "enc_toggle_val", &enc_toggle_val);
    bmp_cfg_db_get_int(db, "out_lame", "audio_mode_val", &audio_mode_val);
    bmp_cfg_db_get_int(db, "out_lame", "auto_ms_val", &auto_ms_val);
    bmp_cfg_db_get_int(db, "out_lame", "enforce_iso_val",
                       &enforce_iso_val);
    bmp_cfg_db_get_int(db, "out_lame", "error_protect_val",
                       &error_protect_val);
    bmp_cfg_db_get_int(db, "out_lame", "prepend_track_number",
                       &b_prepend_track_number);

    bmp_cfg_db_close(db);

    if (!file_path)
        file_path = g_strdup(g_get_home_dir());

}


void outlame_about(void)
{
    static GtkWidget *dialog;

    if (dialog != NULL)
        return;

    dialog = xmms_show_message("About Lame-Output-Plugin",
                               "Lame-Output-Plugin\n\n "
                               "This program is free software; you can redistribute it and/or modify\n"
                               "it under the terms of the GNU General Public License as published by\n"
                               "the Free Software Foundation; either version 2 of the License, or\n"
                               "(at your option) any later version.\n"
                               "\n"
                               "This program is distributed in the hope that it will be useful,\n"
                               "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                               "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
                               "GNU General Public License for more details.\n"
                               "\n"
                               "You should have received a copy of the GNU General Public License\n"
                               "along with this program; if not, write to the Free Software\n"
                               "Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,\n"
                               "USA.", "Ok", FALSE, NULL, NULL);
    gtk_signal_connect(GTK_OBJECT(dialog), "destroy",
                       GTK_SIGNAL_FUNC(gtk_widget_destroyed), &dialog);
}


static gint outlame_open(AFormat fmt, gint rate, gint nch)
{

    gchar *filename, *title = NULL, *temp;
    gint pos;
    int b_use_path_anyway = 0;
    gchar *tmpfilename = NULL;

    /* store open paramators */
    input.format = fmt;
    input.frequency = rate;
    input.channels = nch;

    /* So all the values will be reset to the ones saved */
    /* Easier than to implement a tmp variable for every value */
    outlame_init();

    written = 0;
    afmt = fmt;

    if (xmms_check_realtime_priority()) {
        xmms_show_message("Error",
                          "You cannot use the Lame-Output plugin\n"
                          "when you're running in realtime mode.",
                          "Ok", FALSE, NULL, NULL);
        return 0;
    }

    pos = xmms_remote_get_playlist_pos(ctrlsocket_get_session_id());

    if (title == NULL || strlen(g_basename(title)) == 0) {
        gchar *utf8 = NULL;
        g_free(title);
        /* No filename, lets try title instead */
        utf8 = xmms_remote_get_playlist_title(ctrlsocket_get_session_id(), pos);
        g_strchomp(utf8); //chop trailing ^J --yaz

        title = g_locale_from_utf8(utf8, -1, NULL, NULL, NULL);
        g_free(utf8);
        while (title != NULL && (temp = strchr(title, '/')) != NULL)
            *temp = '-';

        if (title == NULL || strlen(g_basename(title)) == 0) {
            g_free(title);
            /* No title either.  Just set it to something. */
            title = g_strdup_printf("aud-%d", pos);
        }
    }
    /* That's a stream, save it to the default path anyway ... */
    if (strstr(title, "//") != NULL)
        b_use_path_anyway = 1;


    // get file_path from tuple and regrad as the destination path.
    tmpfilename = xmms_remote_get_playlist_file(ctrlsocket_get_session_id(), pos);
    tuple = input_get_song_tuple(tmpfilename);
    g_free(tmpfilename);


#ifdef DEBUG
    if(tuple) {
        printf("tuple->file_path = %s\n", tuple->file_path);
        printf("tuple->track_number = %d\n", tuple->track_number);
    }
    printf("file_path = %s\n", file_path);
    printf("anyway = %d\n", b_use_path_anyway);
#endif

    if (tuple && !b_use_path_anyway) {
        if (b_prepend_track_number && tuple->track_number) {
            filename = g_strdup_printf("%s/%.02d %s.mp3",
                                       b_use_source_file_path ? tuple->file_path : file_path,
                                       tuple->track_number, title);
        }
        else {
            filename = g_strdup_printf("%s/%s.mp3",
                                       b_use_source_file_path ? tuple->file_path : file_path, title);
        }
    }
    else { // no tuple || b_use_path_anyway
        filename = g_strdup_printf("%s/%s.mp3", file_path, g_basename(title));
        g_free(title);
    }

#ifdef DEBUG
    printf("filename = %s\n", filename);
#endif
    output_file = vfs_fopen(filename, "w");
    g_free(filename);

    if (!output_file)
        return 0;

    if ((gfp = lame_init()) == (void *)-1)
        return 0;

    srate = rate;
    inch = nch;

    /* setup id3 data */
    id3tag_init(gfp);

    if (tuple) {
        /* XXX write UTF-8 even though libmp3lame does id3v2.3. --yaz */
#ifdef DEBUG
        g_print("track_name = %s\n", tuple->track_name);
#endif
        lameid3.track_name = g_strdup(tuple->track_name);
        id3tag_set_title(gfp, lameid3.track_name);

        lameid3.performer = g_strdup(tuple->performer);
        id3tag_set_artist(gfp, lameid3.performer);

        lameid3.album_name = g_strdup(tuple->album_name);
        id3tag_set_album(gfp, lameid3.album_name);
    
        lameid3.genre = g_strdup(tuple->genre);
        id3tag_set_genre(gfp, lameid3.genre);

        lameid3.year = g_strdup_printf("%d", tuple->year);
        id3tag_set_year(gfp, lameid3.year);

        lameid3.track_number = g_strdup_printf("%d", tuple->track_number);
        id3tag_set_track(gfp, lameid3.track_number);

//        id3tag_write_v1(gfp);
        id3tag_add_v2(gfp);

        bmp_title_input_free(tuple);
    }


/* input stream description */

    lame_set_in_samplerate(gfp, rate);
    lame_set_num_channels(gfp, nch);
    /* Maybe implement this? */
    /* lame_set_scale(lame_global_flags *, float); */
    lame_set_out_samplerate(gfp, out_samplerate_val);

/* general control parameters */

    lame_set_bWriteVbrTag(gfp, toggle_xing_val);
    lame_set_quality(gfp, algo_quality_val);
    if (audio_mode_val != 4) {
#ifdef DEBUG
        printf("set mode to %d\n", audio_mode_val);
#endif
        lame_set_mode(gfp, audio_mode_val);
    }
    if(auto_ms_val)
        lame_set_mode_automs(gfp, auto_ms_val); // this forces to use joint stereo!! --yaz.

    lame_set_errorf(gfp, lame_debugf);
    lame_set_debugf(gfp, lame_debugf);
    lame_set_msgf(gfp, lame_debugf);

    if (enc_toggle_val == 0 && vbr_on == 0)
        lame_set_brate(gfp, bitrate_val);
    else if (vbr_on == 0)
        lame_set_compression_ratio(gfp, compression_val);

/* frame params */

    lame_set_copyright(gfp, mark_copyright_val);
    lame_set_original(gfp, mark_original_val);
    lame_set_error_protection(gfp, error_protect_val);
    lame_set_strict_ISO(gfp, enforce_iso_val);

    if (vbr_on != 0) {
        if (vbr_type == 0)
            lame_set_VBR(gfp, 2);
        else
            lame_set_VBR(gfp, 3);
        lame_set_VBR_q(gfp, vbr_quality_val);
        lame_set_VBR_mean_bitrate_kbps(gfp, abr_val);
        lame_set_VBR_min_bitrate_kbps(gfp, vbr_min_val);
        lame_set_VBR_max_bitrate_kbps(gfp, vbr_max_val);
        lame_set_VBR_hard_min(gfp, enforce_min_val);
    }

    if (lame_init_params(gfp) == -1)
        return 0;

    return 1;
}

static void convert_buffer(gpointer buffer, gint length)
{
    gint i;

    if (afmt == FMT_S8)
    {
        guint8 *ptr1 = buffer;
        gint8 *ptr2 = buffer;

        for (i = 0; i < length; i++)
            *(ptr1++) = *(ptr2++) ^ 128;
    }
    if (afmt == FMT_S16_BE)
    {
        gint16 *ptr = buffer;

        for (i = 0; i < length >> 1; i++, ptr++)
            *ptr = GUINT16_SWAP_LE_BE(*ptr);
    }
    if (afmt == FMT_S16_NE)
    {
        gint16 *ptr = buffer;

        for (i = 0; i < length >> 1; i++, ptr++)
            *ptr = GINT16_TO_LE(*ptr);
    }
    if (afmt == FMT_U16_BE)
    {
        gint16 *ptr1 = buffer;
        guint16 *ptr2 = buffer;

        for (i = 0; i < length >> 1; i++, ptr2++)
            *(ptr1++) = GINT16_TO_LE(GUINT16_FROM_BE(*ptr2) ^ 32768);
    }
    if (afmt == FMT_U16_LE)
    {
        gint16 *ptr1 = buffer;
        guint16 *ptr2 = buffer;

        for (i = 0; i < length >> 1; i++, ptr2++)
            *(ptr1++) = GINT16_TO_LE(GUINT16_FROM_LE(*ptr2) ^ 32768);
    }
    if (afmt == FMT_U16_NE)
    {
        gint16 *ptr1 = buffer;
        guint16 *ptr2 = buffer;

        for (i = 0; i < length >> 1; i++, ptr2++)
            *(ptr1++) = GINT16_TO_LE((*ptr2) ^ 32768);
    }
}

static void outlame_write(void *ptr, gint length)
{
    AFormat new_format;
    int new_frequency, new_channels;
    EffectPlugin *ep;

    new_format = input.format;
    new_frequency = input.frequency;
    new_channels = input.channels;

    ep = get_current_effect_plugin();
    if ( effects_enabled() && ep && ep->query_format ) { 
        ep->query_format(&new_format,&new_frequency,&new_channels);
    }

    if ( effects_enabled() && ep && ep->mod_samples ) { 
        length = ep->mod_samples(&ptr,length,
                                 input.format,
                                 input.frequency,
                                 input.channels );
    }

    if (afmt == FMT_S8 || afmt == FMT_S16_BE ||
        afmt == FMT_U16_LE || afmt == FMT_U16_BE || afmt == FMT_U16_NE)
        convert_buffer(ptr, length);
#ifdef WORDS_BIGENDIAN
    if (afmt == FMT_S16_NE)
        convert_buffer(ptr, length);
#endif

    if (inch == 1) {
        encout =
            lame_encode_buffer(gfp, ptr, ptr, length / 2, encbuffer,
                               ENCBUFFER_SIZE);
    }
    else {
        encout =
            lame_encode_buffer_interleaved(gfp, ptr, length / 4, encbuffer,
                                           ENCBUFFER_SIZE);
    }

    vfs_fwrite(encbuffer, 1, encout, output_file);
    written += encout;
    olen += length;
}

static void outlame_close(void)
{
    if (output_file) {

        encout = lame_encode_flush_nogap(gfp, encbuffer, ENCBUFFER_SIZE);
        vfs_fwrite(encbuffer, 1, encout, output_file);

//        lame_mp3_tags_fid(gfp, output_file); // will erase id3v2 tag?? 

        vfs_fclose(output_file);
        lame_close(gfp);

        free_lameid3(&lameid3);

        written = 0;
        olen = 0;
    }

#ifdef DEBUG
    printf("close called\n");
#endif
    output_file = NULL;

}

static void outlame_flush(gint time)
{
    // some input plugins (typically aac for libmp4) call flush() with an
    // argument like -1000 every end of playing a song.
    // it would break lame context.
    if (time < 0) {
        return;
    }
    outlame_close();
    outlame_open(input.format, input.frequency, input.channels);
#ifdef DEBUG
    printf("flush %d\n", time);
#endif
    offset = time;
}

static void outlame_pause(short p)
{
}

static gint outlame_buffer_free(void)
{
    return ENCBUFFER_SIZE - encout;
}

static gint outlame_buffer_playing(void)
{
#ifdef DEBUG    
    printf("lame: buffer_playing = %d\n", encout ? 1 : 0);
#endif
    return encout ? 1 : 0;
}

static gint outlame_get_written_time(void)
{
    gint time;
    if (srate && inch) {
        time = (gint) ((olen * 1000) / (srate * 2 * inch));
        return time + offset;
    }

    return 0;
}

static gint outlame_get_output_time(void)
{
    return outlame_get_written_time();
}

/*****************/
/* Configuration */
/*****************/

/* Various Singal-Fuctions */

static void algo_qual(GtkAdjustment * adjustment, gpointer user_data)
{

    algo_quality_val =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (alg_quality_spin));

}

static void samplerate_activate(GtkMenuItem * menuitem, gpointer user_data)
{

    out_samplerate_val = GPOINTER_TO_INT(user_data);

}

static void bitrate_activate(GtkMenuItem * menuitem, gpointer user_data)
{

    bitrate_val = GPOINTER_TO_INT(user_data);

}

static void compression_change(GtkAdjustment * adjustment,
                               gpointer user_data)
{

    compression_val =
        gtk_spin_button_get_value_as_float(GTK_SPIN_BUTTON
                                           (compression_spin));

}

static void encoding_toggle(GtkToggleButton * togglebutton,
                            gpointer user_data)
{

    enc_toggle_val = GPOINTER_TO_INT(user_data);

}

static void mode_activate(GtkMenuItem * menuitem, gpointer user_data)
{

    audio_mode_val = GPOINTER_TO_INT(user_data);

}

static void toggle_auto_ms(GtkToggleButton * togglebutton,
                           gpointer user_data)
{

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ms_mode_toggle)) ==
        TRUE)
        auto_ms_val = 1;
    else
        auto_ms_val = 0;

}

static void toggle_enforce_iso(GtkToggleButton * togglebutton,
                               gpointer user_data)
{

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enforce_iso_toggle))
        == TRUE)
        enforce_iso_val = 1;
    else
        enforce_iso_val = 0;

}

static void toggle_error_protect(GtkToggleButton * togglebutton,
                                 gpointer user_data)
{

    if (gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON(error_protection_toggle)) == TRUE)
        error_protect_val = 1;
    else
        error_protect_val = 0;

}

static void toggle_vbr(GtkToggleButton * togglebutton, gpointer user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vbr_toggle)) ==
        TRUE) {
        gtk_widget_set_sensitive(vbr_options_vbox, TRUE);
        gtk_widget_set_sensitive(enc_quality_frame, FALSE);
        vbr_on = 1;

        if (vbr_type == 0) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (vbr_type_radio1), TRUE);
            gtk_widget_set_sensitive(abr_frame, FALSE);
            gtk_widget_set_sensitive(vbr_frame, TRUE);
        }
        else if (vbr_type == 1) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (vbr_type_radio2), TRUE);
            gtk_widget_set_sensitive(abr_frame, TRUE);
            gtk_widget_set_sensitive(vbr_frame, FALSE);
        }

    }
    else {
        gtk_widget_set_sensitive(vbr_options_vbox, FALSE);
        gtk_widget_set_sensitive(enc_quality_frame, TRUE);
        vbr_on = 0;
    }
}

static void vbr_abr_toggle(GtkToggleButton * togglebutton,
                           gpointer user_data)
{
    if (user_data == "VBR") {
        gtk_widget_set_sensitive(abr_frame, FALSE);
        gtk_widget_set_sensitive(vbr_frame, TRUE);
        vbr_type = 0;
    }
    else if (user_data == "ABR") {
        gtk_widget_set_sensitive(abr_frame, TRUE);
        gtk_widget_set_sensitive(vbr_frame, FALSE);
        vbr_type = 1;
    }
}

static void vbr_min_activate(GtkMenuItem * menuitem, gpointer user_data)
{

    vbr_min_val = GPOINTER_TO_INT(user_data);

}

static void vbr_max_activate(GtkMenuItem * menuitem, gpointer user_data)
{

    vbr_max_val = GPOINTER_TO_INT(user_data);

}

static void toggle_enforce_min(GtkToggleButton * togglebutton,
                               gpointer user_data)
{

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enforce_min_toggle))
        == TRUE)
        enforce_min_val = 1;
    else
        enforce_min_val = 0;

}

static void vbr_qual(GtkAdjustment * adjustment, gpointer user_data)
{

    vbr_quality_val =
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
                                         (vbr_quality_spin));

}

static void abr_activate(GtkMenuItem * menuitem, gpointer user_data)
{

    abr_val = GPOINTER_TO_INT(user_data);

}

static void toggle_xing(GtkToggleButton * togglebutton, gpointer user_data)
{

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(xing_header_toggle)) == TRUE)
        toggle_xing_val = 0;
    else
        toggle_xing_val = 1;

}

static void toggle_original(GtkToggleButton * togglebutton,
                            gpointer user_data)
{

    if (gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON(tags_original_toggle)) == TRUE)
        mark_original_val = 1;
    else
        mark_original_val = 0;

}

static void toggle_copyright(GtkToggleButton * togglebutton,
                             gpointer user_data)
{

    if (gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON(tags_copyright_toggle)) == TRUE)
        mark_copyright_val = 1;
    else
        mark_copyright_val = 0;

}

static void force_v2_toggle(GtkToggleButton * togglebutton,
                            gpointer user_data)
{

    if (gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON(tags_force_id3v2_toggle)) == TRUE) {
        force_v2_val = 1;
        if (gtk_toggle_button_get_active
            (GTK_TOGGLE_BUTTON(tags_only_v1_toggle)) == TRUE) {
            inside = 1;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_only_v1_toggle), FALSE);
            only_v1_val = 0;
            inside = 0;
        }
    }
    else
        force_v2_val = 0;

}

static void id3_only_version(GtkToggleButton * togglebutton,
                             gpointer user_data)
{
    if (user_data == "v1" && inside != 1) {
        if (gtk_toggle_button_get_active
            (GTK_TOGGLE_BUTTON(tags_only_v1_toggle)) == TRUE);
        {
            inside = 1;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_only_v2_toggle), FALSE);
            only_v1_val = 1;
            only_v2_val = 0;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_force_id3v2_toggle), FALSE);
            inside = 0;
        }
    }
    else if (user_data == "v2" && inside != 1) {
        if (gtk_toggle_button_get_active
            (GTK_TOGGLE_BUTTON(tags_only_v2_toggle)) == TRUE);
        {
            inside = 1;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_only_v1_toggle), FALSE);
            only_v1_val = 0;
            only_v2_val = 1;
            inside = 0;
        }
    }

}

static void use_source_file_path_cb(GtkToggleButton * togglebutton,
                                    gpointer user_data)
{
    if (gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON(use_source_file_path)) == TRUE) {
        b_use_source_file_path = 1;
        gtk_widget_set_sensitive(path_dirbrowser, FALSE);
    }
    else {
        b_use_source_file_path = 0;
        gtk_widget_set_sensitive(path_dirbrowser, TRUE);
    }
}

static void prepend_track_number_cb(GtkToggleButton * togglebutton,
                                    gpointer user_data)
{
    if (gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON(prepend_track_number)) == TRUE) {
        b_prepend_track_number = 1;
    }
    else {
        b_prepend_track_number = 0;
    }
}


/* Save Configuration */

static void configure_ok_cb(gpointer data)
{
    ConfigDb *db;

    if (file_path)
        g_free(file_path);
    file_path = g_strdup(gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(path_dirbrowser)));

#ifdef DEBUG
    printf("ok_cb: fle_path = %s\n", file_path);
#endif

    if (vbr_min_val > vbr_max_val)
        vbr_max_val = vbr_min_val;

    db = bmp_cfg_db_open();

    bmp_cfg_db_set_string(db, "out_lame", "file_path", file_path);
    bmp_cfg_db_set_int(db, "out_lame", "use_source_file_path",
                       b_use_source_file_path);
    bmp_cfg_db_set_int(db, "out_lame", "vbr_on", vbr_on);
    bmp_cfg_db_set_int(db, "out_lame", "vbr_type", vbr_type);
    bmp_cfg_db_set_int(db, "out_lame", "vbr_min_val", vbr_min_val);
    bmp_cfg_db_set_int(db, "out_lame", "vbr_max_val", vbr_max_val);
    bmp_cfg_db_set_int(db, "out_lame", "enforce_min_val", enforce_min_val);
    bmp_cfg_db_set_int(db, "out_lame", "vbr_quality_val", vbr_quality_val);
    bmp_cfg_db_set_int(db, "out_lame", "abr_val", abr_val);
    bmp_cfg_db_set_int(db, "out_lame", "toggle_xing_val", toggle_xing_val);
    bmp_cfg_db_set_int(db, "out_lame", "mark_original_val",
                       mark_original_val);
    bmp_cfg_db_set_int(db, "out_lame", "mark_copyright_val",
                       mark_copyright_val);
    bmp_cfg_db_set_int(db, "out_lame", "force_v2_val", force_v2_val);
    bmp_cfg_db_set_int(db, "out_lame", "only_v1_val", only_v1_val);
    bmp_cfg_db_set_int(db, "out_lame", "only_v2_val", only_v2_val);
    bmp_cfg_db_set_int(db, "out_lame", "algo_quality_val",
                       algo_quality_val);
    bmp_cfg_db_set_int(db, "out_lame", "out_samplerate_val",
                       out_samplerate_val);
    bmp_cfg_db_set_int(db, "out_lame", "bitrate_val", bitrate_val);
    bmp_cfg_db_set_float(db, "out_lame", "compression_val",
                         compression_val);
    bmp_cfg_db_set_int(db, "out_lame", "enc_toggle_val", enc_toggle_val);
    bmp_cfg_db_set_int(db, "out_lame", "audio_mode_val", audio_mode_val);
    bmp_cfg_db_set_int(db, "out_lame", "auto_ms_val", auto_ms_val);
    bmp_cfg_db_set_int(db, "out_lame", "enforce_iso_val", enforce_iso_val);
    bmp_cfg_db_set_int(db, "out_lame", "error_protect_val",
                       error_protect_val);
    bmp_cfg_db_set_int(db, "out_lame", "prepend_track_number",
                       b_prepend_track_number);
    bmp_cfg_db_close(db);


    gtk_widget_destroy(configure_win);
    if (path_dirbrowser)
        gtk_widget_destroy(path_dirbrowser);
}

static void configure_destroy(void)
{
    if (path_dirbrowser)
        gtk_widget_destroy(path_dirbrowser);
}


/************************/
/* Configuration Widget */
/************************/


static void outlame_configure(void)
{
    if (!configure_win) {
        configure_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

        gtk_signal_connect(GTK_OBJECT(configure_win), "destroy",
                           GTK_SIGNAL_FUNC(configure_destroy), NULL);
        gtk_signal_connect(GTK_OBJECT(configure_win), "destroy",
                           GTK_SIGNAL_FUNC(gtk_widget_destroyed),
                           &configure_win);
        gtk_window_set_title(GTK_WINDOW(configure_win),
                             "Out-Lame Configuration");
        gtk_window_set_position(GTK_WINDOW(configure_win),
                                GTK_WIN_POS_MOUSE);
        gtk_window_set_policy(GTK_WINDOW(configure_win), FALSE, TRUE,
                              FALSE);
        gtk_container_set_border_width(GTK_CONTAINER(configure_win), 5);

        vbox = gtk_vbox_new(FALSE, 5);
        gtk_container_add(GTK_CONTAINER(configure_win), vbox);

        notebook = gtk_notebook_new();
        gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);


        /* Quality */

        quality_vbox = gtk_vbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(quality_vbox), 5);

        quality_tips = gtk_tooltips_new();

        quality_hbox1 = gtk_hbox_new(FALSE, 5);
        gtk_box_pack_start(GTK_BOX(quality_vbox), quality_hbox1, FALSE,
                           FALSE, 0);

        /* Algorithm Quality */

        alg_quality_frame = gtk_frame_new("Algorithm Quality:");
        gtk_container_set_border_width(GTK_CONTAINER(alg_quality_frame),
                                       5);
        gtk_box_pack_start(GTK_BOX(quality_hbox1), alg_quality_frame,
                           FALSE, FALSE, 0);

        alg_quality_hbox = gtk_hbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(alg_quality_hbox),
                                       10);
        gtk_container_add(GTK_CONTAINER(alg_quality_frame),
                          alg_quality_hbox);

        alg_quality_adj = gtk_adjustment_new(5, 0, 9, 1, 1, 1);
        alg_quality_spin =
            gtk_spin_button_new(GTK_ADJUSTMENT(alg_quality_adj), 8, 0);
        gtk_widget_set_usize(alg_quality_spin, 20, 28);
        gtk_box_pack_start(GTK_BOX(alg_quality_hbox), alg_quality_spin,
                           TRUE, TRUE, 0);
        gtk_signal_connect(GTK_OBJECT(alg_quality_adj), "value-changed",
                           GTK_SIGNAL_FUNC(algo_qual), NULL);

        gtk_tooltips_set_tip(GTK_TOOLTIPS(quality_tips), alg_quality_spin,
                             "best/slowest:0;\nworst/fastest:9;\nrecommended:2;\ndefault:5;",
                             "");

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(alg_quality_spin),
                                  algo_quality_val);

        /* Output Samplerate */

        samplerate_frame = gtk_frame_new("Output Samplerate:");
        gtk_container_set_border_width(GTK_CONTAINER(samplerate_frame), 5);
        gtk_box_pack_start(GTK_BOX(quality_hbox1), samplerate_frame, FALSE,
                           FALSE, 0);

        samplerate_hbox = gtk_hbox_new(TRUE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(samplerate_hbox), 10);
        gtk_container_add(GTK_CONTAINER(samplerate_frame),
                          samplerate_hbox);
        samplerate_option_menu = gtk_option_menu_new();
        samplerate_menu = gtk_menu_new();
        samplerate_menu_item = gtk_menu_item_new_with_label("Auto");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(0));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("8");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(8000));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("11.025");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(11025));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("12");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(12000));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("16");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(16000));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("22.05");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(22050));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("24");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(24000));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("32");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(32000));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("44.1");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(44100));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        samplerate_menu_item = gtk_menu_item_new_with_label("48");
        gtk_signal_connect(GTK_OBJECT(samplerate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(samplerate_activate),
                           GINT_TO_POINTER(48000));
        gtk_menu_append(GTK_MENU(samplerate_menu), samplerate_menu_item);
        gtk_option_menu_set_menu(GTK_OPTION_MENU(samplerate_option_menu),
                                 samplerate_menu);
        gtk_widget_set_usize(samplerate_option_menu, 70, 28);
        gtk_box_pack_start(GTK_BOX(samplerate_hbox),
                           samplerate_option_menu, FALSE, FALSE, 0);
        samplerate_label = gtk_label_new("(kHz)");
        gtk_misc_set_alignment(GTK_MISC(samplerate_label), 0, 0.5);
        gtk_box_pack_start(GTK_BOX(samplerate_hbox), samplerate_label,
                           FALSE, FALSE, 0);

        switch (out_samplerate_val) {

        case 0:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 0);
            break;
        case 8000:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 1);
            break;
        case 11025:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 2);
            break;
        case 12000:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 3);
            break;
        case 16000:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 4);
            break;
        case 22050:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 5);
            break;
        case 24000:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 6);
            break;
        case 32000:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 7);
            break;
        case 44100:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 8);
            break;
        case 48000:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (samplerate_option_menu), 9);
            break;

        }

        /* Encoder Quality */

        enc_quality_frame = gtk_frame_new("Bitrate / Compression ratio:");
        gtk_container_set_border_width(GTK_CONTAINER(enc_quality_frame),
                                       5);
        gtk_box_pack_start(GTK_BOX(quality_vbox), enc_quality_frame, FALSE,
                           FALSE, 0);


        /* yaz new code */
        // vbox sorrounding hbox1 and hbox2
        enc_quality_vbox = gtk_vbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(enc_quality_vbox), 10);

        // pack vbox to frame
        gtk_container_add(GTK_CONTAINER(enc_quality_frame), enc_quality_vbox);

        // hbox1 for bitrate
        hbox1 = gtk_hbox_new(FALSE, 5);
        gtk_container_add(GTK_CONTAINER(enc_quality_vbox), hbox1);

        // radio 1
        enc_radio1 = gtk_radio_button_new(NULL);
        if (enc_toggle_val == 0)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enc_radio1), TRUE);
        gtk_box_pack_start(GTK_BOX(hbox1), enc_radio1, FALSE, FALSE, 0);

        // label 1
        enc_quality_label1 = gtk_label_new("Bitrate (kbps):");
        gtk_box_pack_start(GTK_BOX(hbox1), enc_quality_label1, FALSE, FALSE, 0);

        // bitrate menu
        bitrate_option_menu = gtk_option_menu_new();
        bitrate_menu = gtk_menu_new();
        bitrate_menu_item = gtk_menu_item_new_with_label("8");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(8));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("16");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(16));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("24");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(24));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("32");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(32));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("40");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(40));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("48");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(48));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("56");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(56));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("64");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(64));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("80");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(80));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("96");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(96));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("112");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(112));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("128");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(128));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("160");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(160));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("192");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(192));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("224");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(224));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("256");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(256));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        bitrate_menu_item = gtk_menu_item_new_with_label("320");
        gtk_signal_connect(GTK_OBJECT(bitrate_menu_item), "activate",
                           GTK_SIGNAL_FUNC(bitrate_activate),
                           GINT_TO_POINTER(320));
        gtk_menu_append(GTK_MENU(bitrate_menu), bitrate_menu_item);
        gtk_option_menu_set_menu(GTK_OPTION_MENU(bitrate_option_menu),
                                 bitrate_menu);
        gtk_widget_set_usize(bitrate_option_menu, 80, 28);
        gtk_box_pack_end(GTK_BOX(hbox1), bitrate_option_menu, FALSE, FALSE, 0);


        switch (bitrate_val) {

        case 8:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 0);
            break;
        case 16:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 1);
            break;
        case 24:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 2);
            break;
        case 32:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 3);
            break;
        case 40:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 4);
            break;
        case 48:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 5);
            break;
        case 56:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 6);
            break;
        case 64:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 7);
            break;
        case 80:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 8);
            break;
        case 96:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 9);
            break;
        case 112:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 10);
            break;
        case 128:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 11);
            break;
        case 160:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 12);
            break;
        case 192:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 13);
            break;
        case 224:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 14);
            break;
        case 256:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 15);
            break;
        case 320:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (bitrate_option_menu), 16);
            break;

        }

        // hbox2 for compression ratio
        hbox2 = gtk_hbox_new(FALSE, 5);
        gtk_container_add(GTK_CONTAINER(enc_quality_vbox), hbox2);        

        // radio 2
        enc_radio2 = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(enc_radio1));
        if (enc_toggle_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enc_radio2),
                                         TRUE);
        // pack radio 2
        gtk_box_pack_start(GTK_BOX(hbox2), enc_radio2, FALSE, FALSE, 0);

        // label
        enc_quality_label2 = gtk_label_new("Compression ratio:");
        gtk_box_pack_start(GTK_BOX(hbox2), enc_quality_label2, FALSE, FALSE, 0);

        // comp-ratio spin
        compression_adj = gtk_adjustment_new(11, 0, 100, 1, 1, 1);
        compression_spin =
            gtk_spin_button_new(GTK_ADJUSTMENT(compression_adj), 8, 0);
        gtk_widget_set_usize(compression_spin, 40, 28);
        gtk_container_add(GTK_CONTAINER(hbox2), compression_spin);
        gtk_box_pack_end(GTK_BOX(hbox2), compression_spin, FALSE, FALSE, 0);

        gtk_signal_connect(GTK_OBJECT(compression_adj), "value-changed",
                           GTK_SIGNAL_FUNC(compression_change), NULL);

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(compression_spin),
                                  compression_val);

        // radio button signale connect
        gtk_signal_connect(GTK_OBJECT(enc_radio1), "toggled",
                           GTK_SIGNAL_FUNC(encoding_toggle),
                           GINT_TO_POINTER(0));
        gtk_signal_connect(GTK_OBJECT(enc_radio2), "toggled",
                           GTK_SIGNAL_FUNC(encoding_toggle),
                           GINT_TO_POINTER(1));

	/* end of yaz new code */


        /* Audio Mode */

        mode_frame = gtk_frame_new("Audio Mode:");
        gtk_container_set_border_width(GTK_CONTAINER(mode_frame), 5);
        gtk_box_pack_start(GTK_BOX(quality_vbox), mode_frame, FALSE, FALSE,
                           0);

        mode_hbox = gtk_hbox_new(TRUE, 10);
        gtk_container_set_border_width(GTK_CONTAINER(mode_hbox), 10);
        gtk_container_add(GTK_CONTAINER(mode_frame), mode_hbox);
        mode_option_menu = gtk_option_menu_new();
        mode_menu = gtk_menu_new();
        mode_menu_item = gtk_menu_item_new_with_label("Auto");
        gtk_signal_connect(GTK_OBJECT(mode_menu_item), "activate",
                           GTK_SIGNAL_FUNC(mode_activate),
                           GINT_TO_POINTER(4));
        gtk_menu_append(GTK_MENU(mode_menu), mode_menu_item);
        mode_menu_item = gtk_menu_item_new_with_label("Joint-Stereo");
        gtk_signal_connect(GTK_OBJECT(mode_menu_item), "activate",
                           GTK_SIGNAL_FUNC(mode_activate),
                           GINT_TO_POINTER(1));
        gtk_menu_append(GTK_MENU(mode_menu), mode_menu_item);
        mode_menu_item = gtk_menu_item_new_with_label("Stereo");
        gtk_signal_connect(GTK_OBJECT(mode_menu_item), "activate",
                           GTK_SIGNAL_FUNC(mode_activate),
                           GINT_TO_POINTER(0));
        gtk_menu_append(GTK_MENU(mode_menu), mode_menu_item);
        mode_menu_item = gtk_menu_item_new_with_label("Mono");
        gtk_signal_connect(GTK_OBJECT(mode_menu_item), "activate",
                           GTK_SIGNAL_FUNC(mode_activate),
                           GINT_TO_POINTER(3));
        gtk_menu_append(GTK_MENU(mode_menu), mode_menu_item);
        gtk_option_menu_set_menu(GTK_OPTION_MENU(mode_option_menu),
                                 mode_menu);
        gtk_widget_set_usize(mode_option_menu, 50, 28);
        gtk_box_pack_start(GTK_BOX(mode_hbox), mode_option_menu, TRUE,
                           TRUE, 2);

        switch (audio_mode_val) {

        case 4:
            gtk_option_menu_set_history(GTK_OPTION_MENU(mode_option_menu),
                                        0);
            break;
        case 1:
            gtk_option_menu_set_history(GTK_OPTION_MENU(mode_option_menu),
                                        1);
            break;
        case 0:
            gtk_option_menu_set_history(GTK_OPTION_MENU(mode_option_menu),
                                        2);
            break;
        case 3:
            gtk_option_menu_set_history(GTK_OPTION_MENU(mode_option_menu),
                                        3);
            break;
        }

        ms_mode_toggle = gtk_check_button_new_with_label("auto-M/S mode");
        gtk_box_pack_start(GTK_BOX(mode_hbox), ms_mode_toggle, TRUE, TRUE,
                           5);
        gtk_signal_connect(GTK_OBJECT(ms_mode_toggle), "toggled",
                           GTK_SIGNAL_FUNC(toggle_auto_ms), NULL);

        if (auto_ms_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ms_mode_toggle),
                                         TRUE);

        /* Misc */

        misc_frame = gtk_frame_new("Misc:");
        gtk_container_set_border_width(GTK_CONTAINER(misc_frame), 5);
        gtk_box_pack_start(GTK_BOX(quality_vbox), misc_frame, FALSE, FALSE,
                           0);

        misc_vbox = gtk_vbox_new(TRUE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(misc_vbox), 5);
        gtk_container_add(GTK_CONTAINER(misc_frame), misc_vbox);

        enforce_iso_toggle =
            gtk_check_button_new_with_label
            ("Enforce strict ISO complience");
        gtk_box_pack_start(GTK_BOX(misc_vbox), enforce_iso_toggle, TRUE,
                           TRUE, 2);
        gtk_signal_connect(GTK_OBJECT(enforce_iso_toggle), "toggled",
                           GTK_SIGNAL_FUNC(toggle_enforce_iso), NULL);

        if (enforce_iso_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (enforce_iso_toggle), TRUE);

        error_protection_toggle =
            gtk_check_button_new_with_label("Error protection");
        gtk_box_pack_start(GTK_BOX(misc_vbox), error_protection_toggle,
                           TRUE, TRUE, 2);
        gtk_signal_connect(GTK_OBJECT(error_protection_toggle), "toggled",
                           GTK_SIGNAL_FUNC(toggle_error_protect), NULL);

        if (error_protect_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (error_protection_toggle), TRUE);

        gtk_tooltips_set_tip(GTK_TOOLTIPS(quality_tips),
                             error_protection_toggle,
                             "Adds 16 bit checksum to every frame", "");


        /* Add the Notebook */
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), quality_vbox,
                                 gtk_label_new("Quality"));


/* VBR/ABR */

        vbr_vbox = gtk_vbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_vbox), 5);

        vbr_tips = gtk_tooltips_new();

        /* Toggle VBR */

        vbr_toggle = gtk_check_button_new_with_label("Enable VBR/ABR");
        gtk_widget_set_usize(vbr_toggle, 60, 30);
        gtk_box_pack_start(GTK_BOX(vbr_vbox), vbr_toggle, FALSE, FALSE, 2);
        gtk_signal_connect(GTK_OBJECT(vbr_toggle), "toggled",
                           GTK_SIGNAL_FUNC(toggle_vbr), NULL);

        vbr_options_vbox = gtk_vbox_new(FALSE, 0);
        gtk_container_add(GTK_CONTAINER(vbr_vbox), vbr_options_vbox);
        gtk_widget_set_sensitive(vbr_options_vbox, FALSE);

        /* Choose VBR/ABR */

        vbr_type_frame = gtk_frame_new("Type:");
        gtk_container_set_border_width(GTK_CONTAINER(vbr_type_frame), 5);
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox), vbr_type_frame,
                           FALSE, FALSE, 2);

        vbr_type_hbox = gtk_hbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_type_hbox), 5);
        gtk_container_add(GTK_CONTAINER(vbr_type_frame), vbr_type_hbox);

        vbr_type_radio1 = gtk_radio_button_new_with_label(NULL, "VBR");
        gtk_tooltips_set_tip(GTK_TOOLTIPS(vbr_tips), vbr_type_radio1,
                             "Variable bitrate", "");
        gtk_box_pack_start(GTK_BOX(vbr_type_hbox), vbr_type_radio1, TRUE,
                           TRUE, 2);
        if (vbr_type == 0)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (vbr_type_radio1), TRUE);

        vbr_type_radio2 =
            gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON
                                                        (vbr_type_radio1),
                                                        "ABR");
        gtk_tooltips_set_tip(GTK_TOOLTIPS(vbr_tips), vbr_type_radio2,
                             "Average bitrate", "");
        gtk_box_pack_start(GTK_BOX(vbr_type_hbox), vbr_type_radio2, TRUE,
                           TRUE, 2);
        if (vbr_type == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (vbr_type_radio2), TRUE);

        gtk_signal_connect(GTK_OBJECT(vbr_type_radio1), "toggled",
                           GTK_SIGNAL_FUNC(vbr_abr_toggle), "VBR");
        gtk_signal_connect(GTK_OBJECT(vbr_type_radio2), "toggled",
                           GTK_SIGNAL_FUNC(vbr_abr_toggle), "ABR");

        /* VBR Options */

        vbr_frame = gtk_frame_new("VBR Options:");
        gtk_container_set_border_width(GTK_CONTAINER(vbr_frame), 5);
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox), vbr_frame, FALSE,
                           FALSE, 2);

        vbr_options_vbox2 = gtk_vbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_options_vbox2),
                                       5);
        gtk_container_add(GTK_CONTAINER(vbr_frame), vbr_options_vbox2);

        vbr_options_hbox1 = gtk_hbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_options_hbox1),
                                       5);
        gtk_container_add(GTK_CONTAINER(vbr_options_vbox2),
                          vbr_options_hbox1);

        vbr_min_label = gtk_label_new("Minimum bitrate (kbps):");
        gtk_misc_set_alignment(GTK_MISC(vbr_min_label), 0, 0.5);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox1), vbr_min_label, TRUE,
                           TRUE, 0);

        vbr_min_option_menu = gtk_option_menu_new();
        vbr_min_menu = gtk_menu_new();
        vbr_min_menu_item = gtk_menu_item_new_with_label("8");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(8));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("16");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(16));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("24");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(24));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("32");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(32));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("40");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(40));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("48");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(48));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("56");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(56));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("64");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(64));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("80");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(80));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("96");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(96));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("112");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(112));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("128");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(128));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("160");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(160));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("192");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(192));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("224");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(224));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("256");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(256));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        vbr_min_menu_item = gtk_menu_item_new_with_label("320");
        gtk_signal_connect(GTK_OBJECT(vbr_min_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_min_activate),
                           GINT_TO_POINTER(320));
        gtk_menu_append(GTK_MENU(vbr_min_menu), vbr_min_menu_item);
        gtk_option_menu_set_menu(GTK_OPTION_MENU(vbr_min_option_menu),
                                 vbr_min_menu);
        gtk_widget_set_usize(vbr_min_option_menu, 40, 25);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox1), vbr_min_option_menu,
                           TRUE, TRUE, 2);

        switch (vbr_min_val) {

        case 8:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 0);
            break;
        case 16:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 1);
            break;
        case 24:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 2);
            break;
        case 32:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 3);
            break;
        case 40:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 4);
            break;
        case 48:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 5);
            break;
        case 56:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 6);
            break;
        case 64:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 7);
            break;
        case 80:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 8);
            break;
        case 96:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 9);
            break;
        case 112:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 10);
            break;
        case 128:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 11);
            break;
        case 160:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 12);
            break;
        case 192:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 13);
            break;
        case 224:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 14);
            break;
        case 256:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 15);
            break;
        case 320:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_min_option_menu), 16);
            break;

        }

        vbr_options_hbox2 = gtk_hbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_options_hbox2),
                                       5);
        gtk_container_add(GTK_CONTAINER(vbr_options_vbox2),
                          vbr_options_hbox2);

        vbr_max_label = gtk_label_new("Maximum bitrate (kbps):");
        gtk_misc_set_alignment(GTK_MISC(vbr_max_label), 0, 0.5);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox2), vbr_max_label, TRUE,
                           TRUE, 0);

        vbr_max_option_menu = gtk_option_menu_new();
        vbr_max_menu = gtk_menu_new();
        vbr_max_menu_item = gtk_menu_item_new_with_label("8");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(8));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("16");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(16));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("24");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(24));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("32");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(32));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("40");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(40));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("48");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(48));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("56");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(56));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("64");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(64));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("80");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(80));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("96");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(96));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("112");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(112));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("128");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(128));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("160");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(160));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("192");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(192));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("224");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(224));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("256");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(256));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        vbr_max_menu_item = gtk_menu_item_new_with_label("320");
        gtk_signal_connect(GTK_OBJECT(vbr_max_menu_item), "activate",
                           GTK_SIGNAL_FUNC(vbr_max_activate),
                           GINT_TO_POINTER(320));
        gtk_menu_append(GTK_MENU(vbr_max_menu), vbr_max_menu_item);
        gtk_option_menu_set_menu(GTK_OPTION_MENU(vbr_max_option_menu),
                                 vbr_max_menu);
        gtk_widget_set_usize(vbr_max_option_menu, 40, 25);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox2), vbr_max_option_menu,
                           TRUE, TRUE, 2);

        switch (vbr_max_val) {

        case 8:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 0);
            break;
        case 16:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 1);
            break;
        case 24:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 2);
            break;
        case 32:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 3);
            break;
        case 40:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 4);
            break;
        case 48:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 5);
            break;
        case 56:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 6);
            break;
        case 64:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 7);
            break;
        case 80:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 8);
            break;
        case 96:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 9);
            break;
        case 112:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 10);
            break;
        case 128:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 11);
            break;
        case 160:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 12);
            break;
        case 192:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 13);
            break;
        case 224:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 14);
            break;
        case 256:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 15);
            break;
        case 320:
            gtk_option_menu_set_history(GTK_OPTION_MENU
                                        (vbr_max_option_menu), 16);
            break;

        }

        enforce_min_toggle =
            gtk_check_button_new_with_label
            ("Strictly enforce minimum bitrate");
        gtk_tooltips_set_tip(GTK_TOOLTIPS(vbr_tips), enforce_min_toggle,
                             "For use with players that do not support low bitrate mp3 (Apex AD600-A DVD/mp3 player)",
                             "");
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox2), enforce_min_toggle,
                           FALSE, FALSE, 2);
        gtk_signal_connect(GTK_OBJECT(enforce_min_toggle), "toggled",
                           GTK_SIGNAL_FUNC(toggle_enforce_min), NULL);

        if (enforce_min_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (enforce_min_toggle), TRUE);

        /* ABR Options */

        abr_frame = gtk_frame_new("ABR Options:");
        gtk_container_set_border_width(GTK_CONTAINER(abr_frame), 5);
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox), abr_frame, FALSE,
                           FALSE, 2);
        gtk_widget_set_sensitive(abr_frame, FALSE);

        abr_hbox = gtk_hbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(abr_hbox), 5);
        gtk_container_add(GTK_CONTAINER(abr_frame), abr_hbox);

        abr_label = gtk_label_new("Average bitrate (kbps):");
        gtk_misc_set_alignment(GTK_MISC(abr_label), 0, 0.5);
        gtk_box_pack_start(GTK_BOX(abr_hbox), abr_label, TRUE, TRUE, 0);

        abr_option_menu = gtk_option_menu_new();
        abr_menu = gtk_menu_new();
        abr_menu_item = gtk_menu_item_new_with_label("8");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(8));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("16");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(16));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("24");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(24));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("32");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(32));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("40");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(40));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("48");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(48));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("56");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(56));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("64");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(64));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("80");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(80));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("96");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(96));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("112");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(112));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("128");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(128));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("160");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(160));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("192");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(192));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("224");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(224));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("256");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(256));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        abr_menu_item = gtk_menu_item_new_with_label("320");
        gtk_signal_connect(GTK_OBJECT(abr_menu_item), "activate",
                           GTK_SIGNAL_FUNC(abr_activate),
                           GINT_TO_POINTER(320));
        gtk_menu_append(GTK_MENU(abr_menu), abr_menu_item);
        gtk_option_menu_set_menu(GTK_OPTION_MENU(abr_option_menu),
                                 abr_menu);
        gtk_widget_set_usize(abr_option_menu, 40, 25);
        gtk_box_pack_start(GTK_BOX(abr_hbox), abr_option_menu, TRUE, TRUE,
                           2);

        switch (abr_val) {

        case 8:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        0);
            break;
        case 16:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        1);
            break;
        case 24:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        2);
            break;
        case 32:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        3);
            break;
        case 40:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        4);
            break;
        case 48:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        5);
            break;
        case 56:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        6);
            break;
        case 64:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        7);
            break;
        case 80:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        8);
            break;
        case 96:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        9);
            break;
        case 112:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        10);
            break;
        case 128:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        11);
            break;
        case 160:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        12);
            break;
        case 192:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        13);
            break;
        case 224:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        14);
            break;
        case 256:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        15);
            break;
        case 320:
            gtk_option_menu_set_history(GTK_OPTION_MENU(abr_option_menu),
                                        16);
            break;

        }

        /* Quality Level */

        vbr_options_hbox3 = gtk_hbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbr_options_hbox3),
                                       5);
        gtk_container_add(GTK_CONTAINER(vbr_options_vbox),
                          vbr_options_hbox3);

        vbr_quality_label = gtk_label_new("VBR quality level:");
        gtk_misc_set_alignment(GTK_MISC(vbr_quality_label), 0, 0.5);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox3), vbr_quality_label,
                           TRUE, TRUE, 0);

        vbr_quality_adj = gtk_adjustment_new(4, 0, 9, 1, 1, 1);
        vbr_quality_spin =
            gtk_spin_button_new(GTK_ADJUSTMENT(vbr_quality_adj), 8, 0);
        gtk_widget_set_usize(vbr_quality_spin, 20, -1);
        gtk_box_pack_start(GTK_BOX(vbr_options_hbox3), vbr_quality_spin,
                           TRUE, TRUE, 0);
        gtk_signal_connect(GTK_OBJECT(vbr_quality_adj), "value-changed",
                           GTK_SIGNAL_FUNC(vbr_qual), NULL);

        gtk_tooltips_set_tip(GTK_TOOLTIPS(vbr_tips), vbr_quality_spin,
                             "highest:0;\nlowest:9;\ndefault:4;", "");

        gtk_spin_button_set_value(GTK_SPIN_BUTTON(vbr_quality_spin),
                                  vbr_quality_val);

        /* Xing Header */

        xing_header_toggle =
            gtk_check_button_new_with_label("Don't write Xing VBR header");
        gtk_box_pack_start(GTK_BOX(vbr_options_vbox), xing_header_toggle,
                           FALSE, FALSE, 2);
        gtk_signal_connect(GTK_OBJECT(xing_header_toggle), "toggled",
                           GTK_SIGNAL_FUNC(toggle_xing), NULL);

        if (toggle_xing_val == 0)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (xing_header_toggle), TRUE);



        /* Add the Notebook */

        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbr_vbox,
                                 gtk_label_new("VBR/ABR"));


/* Tags */

        tags_vbox = gtk_vbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(tags_vbox), 5);

        tags_tips = gtk_tooltips_new();

        /* Frame Params */

        tags_frames_frame = gtk_frame_new("Frame params:");
        gtk_container_set_border_width(GTK_CONTAINER(tags_frames_frame),
                                       5);
        gtk_box_pack_start(GTK_BOX(tags_vbox), tags_frames_frame, FALSE,
                           FALSE, 2);

        tags_frames_hbox = gtk_hbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(tags_frames_hbox), 5);
        gtk_container_add(GTK_CONTAINER(tags_frames_frame),
                          tags_frames_hbox);

        tags_copyright_toggle =
            gtk_check_button_new_with_label("Mark as copyright");
        gtk_box_pack_start(GTK_BOX(tags_frames_hbox),
                           tags_copyright_toggle, FALSE, FALSE, 2);
        gtk_signal_connect(GTK_OBJECT(tags_copyright_toggle), "toggled",
                           GTK_SIGNAL_FUNC(toggle_copyright), NULL);

        if (mark_copyright_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_copyright_toggle), TRUE);

        tags_original_toggle =
            gtk_check_button_new_with_label("Mark as original");
        gtk_box_pack_start(GTK_BOX(tags_frames_hbox), tags_original_toggle,
                           FALSE, FALSE, 2);
        gtk_signal_connect(GTK_OBJECT(tags_original_toggle), "toggled",
                           GTK_SIGNAL_FUNC(toggle_original), NULL);

        if (mark_original_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_original_toggle), TRUE);

        /* ID3 Params */

        tags_id3_frame = gtk_frame_new("ID3 params:");
        gtk_container_set_border_width(GTK_CONTAINER(tags_id3_frame), 5);
        gtk_box_pack_start(GTK_BOX(tags_vbox), tags_id3_frame, FALSE,
                           FALSE, 2);

        tags_id3_vbox = gtk_vbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(tags_id3_vbox), 5);
        gtk_container_add(GTK_CONTAINER(tags_id3_frame), tags_id3_vbox);

        tags_force_id3v2_toggle =
            gtk_check_button_new_with_label
            ("Force addition of version 2 tag");
        gtk_box_pack_start(GTK_BOX(tags_id3_vbox), tags_force_id3v2_toggle,
                           FALSE, FALSE, 2);
        gtk_signal_connect(GTK_OBJECT(tags_force_id3v2_toggle), "toggled",
                           GTK_SIGNAL_FUNC(force_v2_toggle), NULL);

        tags_id3_hbox = gtk_hbox_new(FALSE, 5);
        gtk_container_add(GTK_CONTAINER(tags_id3_vbox), tags_id3_hbox);

        tags_only_v1_toggle =
            gtk_check_button_new_with_label("Only add v1 tag");
        gtk_box_pack_start(GTK_BOX(tags_id3_hbox), tags_only_v1_toggle,
                           FALSE, FALSE, 2);
        gtk_signal_connect(GTK_OBJECT(tags_only_v1_toggle), "toggled",
                           GTK_SIGNAL_FUNC(id3_only_version), "v1");

        tags_only_v2_toggle =
            gtk_check_button_new_with_label("Only add v2 tag");
        gtk_box_pack_start(GTK_BOX(tags_id3_hbox), tags_only_v2_toggle,
                           FALSE, FALSE, 2);
        gtk_signal_connect(GTK_OBJECT(tags_only_v2_toggle), "toggled",
                           GTK_SIGNAL_FUNC(id3_only_version), "v2");

        if (force_v2_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_force_id3v2_toggle), TRUE);

        if (only_v1_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_only_v1_toggle), TRUE);

        if (only_v2_val == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (tags_only_v2_toggle), TRUE);

        /* Add the Notebook */

        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tags_vbox,
                                 gtk_label_new("Tags"));


/* Path Config */

        path_hbox = gtk_hbox_new(FALSE, 5);
        gtk_container_set_border_width(GTK_CONTAINER(path_hbox), 10);
        gtk_box_pack_start(GTK_BOX(vbox), path_hbox, FALSE, FALSE, 0);

        path_label = gtk_label_new("Path:");
        gtk_box_pack_start(GTK_BOX(path_hbox), path_label, FALSE, FALSE,
                           0);
        gtk_widget_show(path_label);

	path_dirbrowser =
		gtk_file_chooser_button_new("Pick a folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(path_dirbrowser), file_path);
	gtk_box_pack_start(GTK_BOX(path_hbox), path_dirbrowser, FALSE, FALSE, 0);

        use_source_file_path =
            gtk_check_button_new_with_label("Use source file dir");
        gtk_box_pack_start(GTK_BOX(GTK_BOX(path_hbox)),
                           use_source_file_path, TRUE, TRUE, 5);
        gtk_signal_connect(GTK_OBJECT(use_source_file_path), "toggled",
                           GTK_SIGNAL_FUNC(use_source_file_path_cb), NULL);

        if (b_use_source_file_path == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (use_source_file_path), TRUE);

        prepend_track_number =
            gtk_check_button_new_with_label("Prepend track number to filename");
        gtk_box_pack_start(GTK_BOX(GTK_BOX(path_hbox)),
                           prepend_track_number, TRUE, TRUE, 5);
        gtk_signal_connect(GTK_OBJECT(prepend_track_number), "toggled",
                           GTK_SIGNAL_FUNC(prepend_track_number_cb), NULL);

        if (b_prepend_track_number == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                         (prepend_track_number), TRUE);


/* The Rest */

        /* Seperator */

        configure_separator = gtk_hseparator_new();
        gtk_box_pack_start(GTK_BOX(vbox), configure_separator, FALSE,
                           FALSE, 0);
        gtk_widget_show(configure_separator);

        /* Buttons */

        configure_bbox = gtk_hbutton_box_new();
        gtk_button_box_set_layout(GTK_BUTTON_BOX(configure_bbox),
                                  GTK_BUTTONBOX_END);
        gtk_button_box_set_spacing(GTK_BUTTON_BOX(configure_bbox), 5);
        gtk_box_pack_start(GTK_BOX(vbox), configure_bbox, FALSE, FALSE, 0);

        configure_ok = gtk_button_new_with_label("Ok");
        gtk_signal_connect(GTK_OBJECT(configure_ok), "clicked",
                           GTK_SIGNAL_FUNC(configure_ok_cb), NULL);
        GTK_WIDGET_SET_FLAGS(configure_ok, GTK_CAN_DEFAULT);
        gtk_box_pack_start(GTK_BOX(configure_bbox), configure_ok, TRUE,
                           TRUE, 0);
        gtk_widget_show(configure_ok);
        gtk_widget_grab_default(configure_ok);

        configure_cancel = gtk_button_new_with_label("Cancel");
        gtk_signal_connect_object(GTK_OBJECT(configure_cancel), "clicked",
                                  GTK_SIGNAL_FUNC(gtk_widget_destroy),
                                  GTK_OBJECT(configure_win));
        GTK_WIDGET_SET_FLAGS(configure_cancel, GTK_CAN_DEFAULT);
        gtk_box_pack_start(GTK_BOX(configure_bbox), configure_cancel, TRUE,
                           TRUE, 0);

        /* Set States */

        if (vbr_on == 1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vbr_toggle),
                                         TRUE);
        else
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vbr_toggle),
                                         FALSE);

        /* Show it! */

        gtk_widget_show_all(configure_win);

    }
}

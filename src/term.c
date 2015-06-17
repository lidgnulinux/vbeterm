/* -*- mode: c; c-file-style: "openbsd" -*- */
/*
 * Copyright (c) 2014 Vincent Bernat <bernat@luffy.cx>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "term.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <gdk/gdkkeysyms-compat.h>

static void
set_font_size(VteTerminal *terminal, gint delta)
{
	PangoFontDescription *descr;
	descr = pango_font_description_copy(vte_terminal_get_font(terminal));
	if (!descr) return;

	gint current = pango_font_description_get_size(descr);
	pango_font_description_set_size(descr, current + delta * PANGO_SCALE);
	vte_terminal_set_font(terminal, descr);
	pango_font_description_free(descr);
}

static void
reset_font_size(VteTerminal *terminal)
{
	vte_terminal_set_font_from_string(terminal,
	    TERM_FONT);
	set_font_size(VTE_TERMINAL(terminal), 0);
}

static gboolean
on_dpi_changed(GtkSettings *settings,
    GParamSpec *pspec,
    gpointer user_data)
{
	VteTerminal *terminal = user_data;
	set_font_size(terminal, 0);
	return TRUE;
}

static gboolean
on_char_size_changed(GtkWidget *terminal, guint width, guint height, gpointer user_data)
{
	set_font_size(VTE_TERMINAL(terminal), 0);
	return TRUE;
}

static gboolean
on_title_changed(GtkWidget *terminal, gpointer user_data)
{
	GtkWindow *window = user_data;
	gtk_window_set_title(window,
	    vte_terminal_get_window_title(VTE_TERMINAL(terminal))?:PACKAGE_NAME);
	return TRUE;
}

static gboolean
on_child_exit(VteTerminal *term, gint status, gpointer user_data)
{
	GtkWindow *window = user_data;
	GApplicationCommandLine *cmdline = g_object_get_data(G_OBJECT(window), "cmdline");
	if (WIFEXITED(status)) {
		g_application_command_line_set_exit_status(cmdline,
		    WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		g_application_command_line_set_exit_status(cmdline,
		    128 + WTERMSIG(status));
	} else {
		g_application_command_line_set_exit_status(cmdline, 127);
	}
	g_object_unref(cmdline);
	gtk_widget_destroy(GTK_WIDGET(window));
	return TRUE;
}

static gboolean
on_key_press(GtkWidget *terminal, GdkEventKey *event, gpointer user_data)
{
	if (event->state & GDK_CONTROL_MASK) {
		switch (event->keyval) {
		case GDK_plus:
			set_font_size(VTE_TERMINAL(terminal), 1);
			return TRUE;
		case GDK_minus:
			set_font_size(VTE_TERMINAL(terminal), -1);
			return TRUE;
		case GDK_equal:
			reset_font_size(VTE_TERMINAL(terminal));
			return TRUE;
		}
	} else if (event->state & GDK_MOD1_MASK) {
		switch (event->keyval) {
		case GDK_slash:
			dabbrev_expand(GTK_WINDOW(user_data), VTE_TERMINAL(terminal));
			return TRUE;
		}
	}
	dabbrev_stop(VTE_TERMINAL(terminal));
	return FALSE;
}

static gchar**
get_child_environment(void)
{
	guint n;
	gchar **env, **result, **p;
	const gchar *value;

	/* Copy the current environment */
	env = g_listenv();
	n = g_strv_length (env);
	result = g_new (gchar *, n + 2);
	for (n = 0, p = env; *p != NULL; ++p) {
		if (g_strcmp0(*p, "COLORTERM") == 0) continue;
		value = g_getenv (*p);
		if (G_LIKELY(value != NULL))
			result[n++] = g_strconcat (*p, "=", value, NULL);
	}
	g_strfreev(env);

	/* Setup COLORTERM */
	result[n++] = g_strdup_printf("COLORTERM=%s", PACKAGE_NAME);
	result[n] = NULL;
	return result;
}

static void
command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data)
{
	/* Initialise GTK and the widgets */
	GtkWidget *window, *terminal;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(window));
	terminal = vte_terminal_new();
	gtk_window_set_title(GTK_WINDOW(window), PACKAGE_NAME);
	gtk_container_add(GTK_CONTAINER(window), terminal);
	g_object_set_data(G_OBJECT(window), "terminal", terminal);
	gtk_widget_set_visual(window, gdk_screen_get_rgba_visual(gtk_widget_get_screen(window)));
	gtk_widget_show_all(window);
	gtk_window_set_focus(GTK_WINDOW(window), terminal);

	/* Only return when the window is closed */
	g_application_hold(app);
	g_object_set_data_full(G_OBJECT(cmdline), "application", app,
	    (GDestroyNotify)g_application_release);
	g_object_set_data_full(G_OBJECT(window), "cmdline", cmdline, NULL);
	g_object_ref(cmdline);

	/* Connect some signals */
	g_signal_connect(terminal, "child-exited", G_CALLBACK(on_child_exit), GTK_WINDOW(window));
	g_signal_connect(terminal, "window-title-changed", G_CALLBACK(on_title_changed), GTK_WINDOW(window));
	g_signal_connect(terminal, "key-press-event", G_CALLBACK(on_key_press), GTK_WINDOW(window));
	g_signal_connect(terminal, "char-size-changed", G_CALLBACK(on_char_size_changed), NULL);
	g_signal_connect(gtk_settings_get_default(), "notify::gtk-xft-dpi",
	    G_CALLBACK(on_dpi_changed), VTE_TERMINAL(terminal));

	/* Configure terminal */
	vte_terminal_set_word_chars(VTE_TERMINAL(terminal),
	    TERM_WORD_CHARS);
	vte_terminal_set_scrollback_lines(VTE_TERMINAL(terminal),
	    0);
	vte_terminal_set_scroll_on_output(VTE_TERMINAL(terminal),
	    FALSE);
	vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(terminal),
	    TRUE);
	vte_terminal_set_rewrap_on_resize(VTE_TERMINAL(terminal),
	    TRUE);

#define CLR_R(x)   (((x) & 0xff0000) >> 16)
#define CLR_G(x)   (((x) & 0x00ff00) >>  8)
#define CLR_B(x)   (((x) & 0x0000ff) >>  0)
#define CLR_16(x)  (((x) << 8) | (x))
#define CLR_GDK(x) (const GdkColor){ 0, CLR_16(CLR_R(x)), CLR_16(CLR_G(x)), CLR_16(CLR_B(x)) }
	vte_terminal_set_colors(VTE_TERMINAL(terminal),
	    &CLR_GDK(0xffffff),
	    &CLR_GDK(0),
	    (const GdkColor[]){	CLR_GDK(0x111111),
			CLR_GDK(0xd36265),
			CLR_GDK(0xaece91),
			CLR_GDK(0xe7e18c),
			CLR_GDK(0x5297cf),
			CLR_GDK(0x963c59),
			CLR_GDK(0x5E7175),
			CLR_GDK(0xbebebe),
			CLR_GDK(0x666666),
			CLR_GDK(0xef8171),
			CLR_GDK(0xcfefb3),
			CLR_GDK(0xfff796),
			CLR_GDK(0x74b8ef),
			CLR_GDK(0xb85e7b),
			CLR_GDK(0xA3BABF),
			CLR_GDK(0xffffff)
 	    }, 16);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	vte_terminal_set_opacity(VTE_TERMINAL(terminal),
	    TERM_OPACITY * 65535);
#pragma GCC diagnostic pop
	vte_terminal_set_color_cursor(VTE_TERMINAL(terminal),
	    &CLR_GDK(0x008800));
	vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal),
	    VTE_CURSOR_BLINK_OFF);
	vte_terminal_set_allow_bold(VTE_TERMINAL(terminal),
	    TRUE);
	reset_font_size(VTE_TERMINAL(terminal));

	vte_terminal_set_audible_bell(VTE_TERMINAL(terminal),
	    FALSE);
	vte_terminal_set_visible_bell(VTE_TERMINAL(terminal),
	    FALSE);

	/* Start a new shell */
	gchar **env;
	env = get_child_environment();
	vte_terminal_fork_command_full(VTE_TERMINAL (terminal),
	    VTE_PTY_DEFAULT,
	    NULL,			/* working directory */
	    (gchar *[]){ g_strdup(g_getenv("SHELL")), 0 },
	    env,		/* envv */
	    0,			/* spawn flags */
	    NULL, NULL,		/* child setup */
	    NULL,			/* child pid */
	    NULL);
	g_strfreev(env);
}

int
main(int argc, char *argv[])
{
	GtkApplication *app;
	gint status;
	app = gtk_application_new("im.bernat.Terminal", G_APPLICATION_HANDLES_COMMAND_LINE);
	g_signal_connect(app, "command-line", G_CALLBACK(command_line), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}

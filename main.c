/* Tray slider for external monitor brightness over DDC/CI (via ddcutil). */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#define MARGIN 8
#define SLIDER_WIDTH 168

/* Mint's panel only shows XApp status icons (legacy GtkStatusIcon tray icons
 * are swallowed invisibly), so use libxapp. It ships on every Mint install but
 * its headers usually don't, hence the local prototypes. XAppStatusIcon falls
 * back to a GtkStatusIcon by itself on desktops without an XApp applet. */
typedef struct _XAppStatusIcon XAppStatusIcon;
XAppStatusIcon *xapp_status_icon_new(void);
void xapp_status_icon_set_name(XAppStatusIcon *icon, const gchar *name);
void xapp_status_icon_set_icon_name(XAppStatusIcon *icon, const gchar *icon_name);
void xapp_status_icon_set_tooltip_text(XAppStatusIcon *icon, const gchar *tooltip_text);
void xapp_status_icon_set_secondary_menu(XAppStatusIcon *icon, GtkMenu *menu);
enum { XAPP_SCROLL_UP, XAPP_SCROLL_DOWN, XAPP_SCROLL_LEFT, XAPP_SCROLL_RIGHT };

static XAppStatusIcon *icon;
/* Where the panel told us the icon is, from the last click; -1 if unknown. */
static int click_x = -1, click_y = -1, click_panel = GTK_POS_BOTTOM;
static GtkWidget *win, *scale, *label, *menu;
static gboolean updating;
static gint64 hidden_at;
static int grab_tries;

/* Shared with the worker thread. target < 0 means the user hasn't moved the slider yet. */
static GMutex lock;
static GCond cond;
static int target = -1;

/* ---- ddcutil worker ---------------------------------------------------- */

static char *run_ddcutil(const char *a1, const char *a2, const char *a3, const char *a4, const char *bus) {
	const char *argv[9] = {"ddcutil", a1};
	int n = 2;
	char *out = NULL;

	if (a2) argv[n++] = a2;
	if (a3) argv[n++] = a3;
	if (a4) argv[n++] = a4;
	if (bus) {
		argv[n++] = "--bus";
		argv[n++] = bus;
	}
	argv[n] = NULL;

	GError *err = NULL;
	if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
	                  NULL, NULL, &out, NULL, NULL, &err)) {
		g_warning("ddcutil %s: %s", a1, err->message);
		g_error_free(err);
		return NULL;
	}
	return out;
}

/* Finds the monitor's I2C bus so later calls can skip detection. */
static char *detect_bus(void) {
	char *out = run_ddcutil("detect", "--brief", NULL, NULL, NULL);
	char *bus = NULL;
	if (!out)
		return NULL;

	char **lines = g_strsplit(out, "\n", -1);
	for (int i = 0; lines[i] && lines[i + 1]; i++) {
		char *p;
		if (g_str_has_prefix(lines[i], "Display ") && (p = strstr(lines[i + 1], "/dev/i2c-"))) {
			bus = g_strdup(g_strstrip(p + strlen("/dev/i2c-")));
			break;
		}
	}
	g_strfreev(lines);
	g_free(out);
	if (!bus)
		g_warning("ddcutil detect: no DDC capable display found");
	return bus;
}

static int get_brightness(const char *bus) {
	char *out = run_ddcutil("getvcp", "10", "--brief", NULL, bus);
	int cur, max;
	if (!out)
		return -1;
	if (sscanf(out, "VCP 10 C %d %d", &cur, &max) != 2)
		cur = -1;
	g_free(out);
	return cur;
}

static gboolean show_value_idle(gpointer data) {
	updating = TRUE;
	gtk_range_set_value(GTK_RANGE(scale), GPOINTER_TO_INT(data));
	updating = FALSE;
	return G_SOURCE_REMOVE;
}

/* Owns all ddcutil calls. ddcutil is slow, so while dragging only the most
 * recent slider value is sent. */
static gpointer worker(gpointer data) {
	char *bus = detect_bus();
	int last = get_brightness(bus);

	g_mutex_lock(&lock);
	if (last >= 0 && target < 0)
		g_idle_add(show_value_idle, GINT_TO_POINTER(last));

	for (;;) {
		while (target < 0 || target == last)
			g_cond_wait(&cond, &lock);
		int v = target;
		g_mutex_unlock(&lock);

		char val[8];
		g_snprintf(val, sizeof val, "%d", v);
		g_free(run_ddcutil("setvcp", "10", val, "--noverify", bus));
		last = v;

		g_mutex_lock(&lock);
	}
	return NULL;
}

/* ---- popup ------------------------------------------------------------- */

static void hide_popup(void) {
	if (!gtk_widget_get_visible(win))
		return;
	gtk_grab_remove(win);
	gdk_seat_ungrab(gdk_display_get_default_seat(gdk_display_get_default()));
	gtk_widget_hide(win);
	hidden_at = g_get_monotonic_time();
}

static void place_popup(void) {
	GdkDisplay *dpy = gdk_display_get_default();
	GdkMonitor *mon;
	GdkRectangle wa;
	GtkRequisition size;
	int x, y;

	gtk_widget_get_preferred_size(win, NULL, &size);

	if (click_x >= 0) {
		mon = gdk_display_get_monitor_at_point(dpy, click_x, click_y);
	} else {
		mon = gdk_display_get_primary_monitor(dpy);
		if (!mon)
			mon = gdk_display_get_monitor(dpy, 0);
	}
	gdk_monitor_get_workarea(mon, &wa);

	if (click_x < 0) {
		/* Opened by relaunching rather than a click: bottom right corner. */
		x = wa.x + wa.width;
		y = wa.y + wa.height;
	} else if (click_panel == GTK_POS_LEFT || click_panel == GTK_POS_RIGHT) {
		x = click_panel == GTK_POS_LEFT ? click_x : click_x - size.width;
		y = click_y - size.height / 2;
	} else {
		x = click_x + 12 - size.width / 2;
		y = click_panel == GTK_POS_TOP ? click_y : click_y - size.height;
	}
	x = CLAMP(x, wa.x + MARGIN, wa.x + wa.width - size.width - MARGIN);
	y = CLAMP(y, wa.y + MARGIN, wa.y + wa.height - size.height - MARGIN);
	gtk_window_move(GTK_WINDOW(win), x, y);
}

/* The popup never takes window-manager focus (so the active window stays
 * active); like a menu, it grabs input instead to see clicks outside itself.
 * The grab can be refused while e.g. a keybinding grab is still held. */
static gboolean grab_popup(gpointer data) {
	if (!gtk_widget_get_visible(win))
		return G_SOURCE_REMOVE;

	GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
	if (gdk_seat_grab(seat, gtk_widget_get_window(win), GDK_SEAT_CAPABILITY_ALL, TRUE,
	                  NULL, NULL, NULL, NULL) == GDK_GRAB_SUCCESS) {
		gtk_grab_add(win);
		return G_SOURCE_REMOVE;
	}
	if (++grab_tries >= 20) {
		hide_popup();
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void toggle_popup(void) {
	if (gtk_widget_get_visible(win)) {
		hide_popup();
		return;
	}
	/* A click on the tray icon while open closes the popup first; don't reopen. */
	if (g_get_monotonic_time() - hidden_at < 250000)
		return;
	place_popup();
	gtk_widget_show(win);
	gtk_widget_grab_focus(scale);
	grab_tries = 0;
	if (grab_popup(NULL) == G_SOURCE_CONTINUE)
		g_timeout_add(50, grab_popup, NULL);
}

static void set_label(int v) {
	char buf[32];
	g_snprintf(buf, sizeof buf, "%d%%", v);
	gtk_label_set_text(GTK_LABEL(label), buf);
	g_snprintf(buf, sizeof buf, "Brightness: %d%%", v);
	xapp_status_icon_set_tooltip_text(icon, buf);
}

static void on_value_changed(GtkRange *range, gpointer data) {
	int v = (int)(gtk_range_get_value(range) + 0.5);
	set_label(v);
	if (updating)
		return;
	g_mutex_lock(&lock);
	target = v;
	g_cond_signal(&cond);
	g_mutex_unlock(&lock);
}

static gboolean on_button_press(GtkWidget *w, GdkEventButton *e, gpointer data) {
	GdkWindow *gw = gtk_widget_get_window(win);
	int x, y;
	gdk_window_get_origin(gw, &x, &y);
	if (e->x_root < x || e->x_root >= x + gdk_window_get_width(gw) ||
	    e->y_root < y || e->y_root >= y + gdk_window_get_height(gw))
		hide_popup();
	return FALSE;
}

static gboolean on_grab_broken(GtkWidget *w, GdkEvent *e, gpointer data) {
	hide_popup();
	return FALSE;
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer data) {
	if (e->keyval == GDK_KEY_Escape) {
		hide_popup();
		return TRUE;
	}
	return FALSE;
}

static void on_icon_release(XAppStatusIcon *i, int x, int y, guint button, guint time, int panel, gpointer data) {
	if (button != 1)
		return;
	click_x = x;
	click_y = y;
	click_panel = panel;
	toggle_popup();
}

static void on_icon_scroll(XAppStatusIcon *i, int amount, int direction, guint time, gpointer data) {
	double step = 0;
	if (direction == XAPP_SCROLL_UP)
		step = 5;
	else if (direction == XAPP_SCROLL_DOWN)
		step = -5;
	gtk_range_set_value(GTK_RANGE(scale), gtk_range_get_value(GTK_RANGE(scale)) + step);
}

static void on_startup(GApplication *app, gpointer data) {
	GtkWidget *frame, *box, *img, *quit;

	win = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
	gtk_container_add(GTK_CONTAINER(win), frame);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_container_add(GTK_CONTAINER(frame), box);

	img = gtk_image_new_from_icon_name("display-brightness-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 0);

	scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_range_set_increments(GTK_RANGE(scale), 5, 10);
	gtk_widget_set_size_request(scale, SLIDER_WIDTH, -1);
	gtk_box_pack_start(GTK_BOX(box), scale, TRUE, TRUE, 0);

	label = gtk_label_new("");
	gtk_label_set_width_chars(GTK_LABEL(label), 5);
	gtk_label_set_xalign(GTK_LABEL(label), 1.0);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	gtk_widget_show_all(frame);

	g_signal_connect(scale, "value-changed", G_CALLBACK(on_value_changed), NULL);
	g_signal_connect(win, "button-press-event", G_CALLBACK(on_button_press), NULL);
	g_signal_connect(win, "grab-broken-event", G_CALLBACK(on_grab_broken), NULL);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

	menu = gtk_menu_new();
	quit = gtk_menu_item_new_with_label("Quit");
	g_signal_connect_swapped(quit, "activate", G_CALLBACK(g_application_quit), app);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
	gtk_widget_show_all(menu);

	icon = xapp_status_icon_new();
	xapp_status_icon_set_name(icon, "brightness-control");
	xapp_status_icon_set_icon_name(icon, "display-brightness-symbolic");
	xapp_status_icon_set_secondary_menu(icon, GTK_MENU(menu));
	g_signal_connect(icon, "button-release-event", G_CALLBACK(on_icon_release), NULL);
	g_signal_connect(icon, "scroll-event", G_CALLBACK(on_icon_scroll), NULL);
	set_label(0);

	g_application_hold(app);
	g_thread_unref(g_thread_new("ddcutil", worker, NULL));
}

/* Fires on launch, and again in the running instance when launched a second
 * time (e.g. from a keyboard shortcut) - so a relaunch just opens the slider. */
static void on_activate(GApplication *app, gpointer data) {
	static gboolean first = TRUE;
	if (first) {
		first = FALSE;
		return;
	}
	click_x = click_y = -1;
	toggle_popup();
}

int main(int argc, char **argv) {
	GtkApplication *app = gtk_application_new("com.qxlabs.BrightnessControl", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}

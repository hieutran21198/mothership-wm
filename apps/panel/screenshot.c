//go:build linux && cgo && panel

#define _GNU_SOURCE
#include "screenshot.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "analog-button.h"
#include "home-panel.h"
#include "map-pointer.h"
#include "shell-type.h"

enum capture_mode { CAPTURE_AREA, CAPTURE_WINDOW, CAPTURE_DISPLAY };
enum export_action { EXPORT_NONE, EXPORT_COPY, EXPORT_SAVE };
enum area_drag { DRAW_AREA, MOVE_AREA, RESIZE_AREA };

struct rectangle { double x, y, width, height; };
struct image { GBytes *pixels; int width, height, stride; };
struct target { uint32_t hi, lo; struct rectangle bounds; char *title; };

static GtkWindow *owner;
static GtkWidget *surface, *picture, *canvas, *message, *controls;
static GtkWidget *modes[3], *copy_button, *save_button, *cancel_button, *entries[2];
static void (*prepare_session)(gboolean), (*update_region)(void), (*show_notice)(const char *);
static GWeakRef previous_focus;
static GPtrArray *targets;
static struct image frame;
static struct target *hovered, *selected;
static int logical_width, logical_height;
static gboolean available, pending, active;
static enum capture_mode mode;
static enum export_action waiting;
static struct rectangle area, initial_area;
static enum area_drag drag;
static gboolean dragging, moved, anchor_set;
static double start_x, start_y, opposite_x, opposite_y, anchor_x, anchor_y;

static void image_clear(struct image *image) {
    g_clear_pointer(&image->pixels, g_bytes_unref);
    *image = (struct image){0};
}

static struct image image_from_fd(int fd, int width, int height, int stride) {
    struct image image = {0};
    if (fd < 0) return image;
    // Limit the buffer to one 8K output, including any row alignment.
    const gsize limit = 128 * 1024 * 1024;
    if (width <= 0 || width > G_MAXINT / 4 || height <= 0 ||
        stride < width * 4 || (gsize)height > limit / (gsize)stride) goto done;

    gsize size = (gsize)stride * height;
    guint8 *data = g_try_malloc(size);
    if (!data) goto done;
    for (gsize offset = 0; offset < size;) {
        ssize_t count = pread(fd, data + offset, size - offset, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            g_free(data);
            goto done;
        }
        offset += (gsize)count;
    }
    image = (struct image){g_bytes_new_take(data, size), width, height, stride};
done:
    close(fd);
    return image;
}

static GdkTexture *image_texture(const struct image *image) {
    return gdk_memory_texture_new(image->width, image->height, GDK_MEMORY_R8G8B8A8,
        image->pixels, image->stride);
}

static void target_free(gpointer data) {
    struct target *target = data;
    g_free(target->title);
    g_free(target);
}

static void set_message(const char *text, gboolean error) {
    gtk_label_set_text(GTK_LABEL(message), text ? text : "");
    gtk_widget_set_visible(message, text && *text);
    if (error) gtk_widget_add_css_class(message, "screenshot-error");
    else gtk_widget_remove_css_class(message, "screenshot-error");
}

static gboolean valid(struct rectangle box) {
    return box.width >= 1.0 && box.height >= 1.0;
}

static struct rectangle output_bounds(void) {
    return (struct rectangle){0, 0, logical_width, logical_height};
}

static struct rectangle visible_box(void) {
    if (mode == CAPTURE_DISPLAY) return output_bounds();
    if (mode == CAPTURE_WINDOW)
        return selected ? selected->bounds : hovered ? hovered->bounds : (struct rectangle){0};
    return area;
}

static GdkRectangle pixel_box(struct rectangle box) {
    double sx = (double)frame.width / logical_width;
    double sy = (double)frame.height / logical_height;
    int left = CLAMP((int)floor(box.x * sx), 0, frame.width);
    int top = CLAMP((int)floor(box.y * sy), 0, frame.height);
    int right = CLAMP((int)ceil((box.x + box.width) * sx), left, frame.width);
    int bottom = CLAMP((int)ceil((box.y + box.height) * sy), top, frame.height);
    return (GdkRectangle){left, top, right - left, bottom - top};
}

static void update_controls(void) {
    gboolean ready = active && waiting == EXPORT_NONE && !dragging &&
        (mode == CAPTURE_WINDOW ? selected != NULL : valid(visible_box()));
    for (unsigned i = 0; i < G_N_ELEMENTS(modes); ++i) {
        analog_button_on(modes[i], i == (unsigned)mode);
        gtk_widget_set_sensitive(modes[i], waiting == EXPORT_NONE);
    }
    gtk_widget_set_sensitive(copy_button, ready);
    gtk_widget_set_sensitive(save_button, ready);
    gtk_widget_queue_draw(canvas);
}

static void guidance(void) {
    const char *text = NULL;
    if (mode == CAPTURE_AREA && !valid(area))
        text = anchor_set ? "Choose the opposite corner" : "Drag to select an area";
    if (mode == CAPTURE_WINDOW && !selected)
        text = targets->len ? "Choose a window" : "No visible windows";
    set_message(text, FALSE);
}

static void finish(gboolean notify_wm) {
    gboolean was_visible = active;
    active = pending = dragging = anchor_set = FALSE;
    waiting = EXPORT_NONE;
    hovered = selected = NULL;
    g_ptr_array_set_size(targets, 0);
    image_clear(&frame);
    gtk_widget_set_visible(surface, FALSE);
    gtk_picture_set_paintable(GTK_PICTURE(picture), NULL);
    if (was_visible) {
        g_autoptr(GtkWidget) previous = g_weak_ref_get(&previous_focus);
        gtk_window_set_focus(owner, previous && gtk_widget_get_mapped(previous) ? previous : NULL);
        update_region();
    }
    g_weak_ref_set(&previous_focus, NULL);
    if (notify_wm) map_pointer_screenshot_cancel();
}

static void set_color(cairo_t *cr, unsigned rgb, double alpha) {
    cairo_set_source_rgba(cr, (rgb >> 16) / 255., ((rgb >> 8) & 255) / 255., (rgb & 255) / 255., alpha);
}

static void corners(cairo_t *cr, double x, double y, double width, double height, double cut) {
    cairo_move_to(cr, x + cut, y);
    cairo_line_to(cr, x + width - cut, y);
    cairo_line_to(cr, x + width, y + cut);
    cairo_line_to(cr, x + width, y + height - cut);
    cairo_line_to(cr, x + width - cut, y + height);
    cairo_line_to(cr, x + cut, y + height);
    cairo_line_to(cr, x, y + height - cut);
    cairo_line_to(cr, x, y + cut);
    cairo_close_path(cr);
}

static void draw_toolbar(GtkDrawingArea *unused, cairo_t *cr, int width, int height, gpointer data) {
    (void)unused; (void)data;
    if (width < 14 || height < 14) return;
    corners(cr, 0.5, 0.5, width - 1, height - 1, 7);
    set_color(cr, 0x10212d, .98);
    cairo_fill_preserve(cr);
    set_color(cr, 0x547884, 1);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
}

static void draw_dimensions(cairo_t *cr, struct rectangle box, struct rectangle painted, int width, int height) {
    const char *title = mode == CAPTURE_WINDOW && (selected || hovered) ?
        (selected ? selected->title : hovered->title) : NULL;
    GdkRectangle crop = pixel_box(box);
    // A window may extend past an output edge. The title still identifies it.
    g_autofree char *label = title && *title ? g_strdup(title) :
        g_strdup_printf("%d × %d px", crop.width, crop.height);
    g_autoptr(PangoLayout) layout = pango_cairo_create_layout(cr);
    g_autoptr(PangoFontDescription) font = pango_font_description_new();
    pango_font_description_set_family(font, shell_font_family());
    pango_font_description_set_absolute_size(font, 13 * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, label, -1);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    int text_width, text_height;
    pango_layout_get_pixel_size(layout, &text_width, &text_height);
    int available_width = MAX(0, MIN(340, width - 26));
    if (text_width > available_width) {
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, available_width * PANGO_SCALE);
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
    }

    double w = text_width + 14, h = text_height + 6;
    double x = CLAMP(painted.x + 4, 2, MAX(2, width - w - 2));
    double y = painted.y >= h + 6 ? painted.y - h - 5 : painted.y + 5;
    y = CLAMP(y, 2, MAX(2, height - h - 2));
    corners(cr, x + .5, y + .5, w - 1, h - 1, 3);
    set_color(cr, 0x0e1d27, .98);
    cairo_fill_preserve(cr);
    set_color(cr, 0x5d8595, 1);
    cairo_stroke(cr);
    cairo_move_to(cr, x + 7, y + 3);
    set_color(cr, 0xe6f5f8, 1);
    pango_cairo_show_layout(cr, layout);
}

static void draw_selection(GtkDrawingArea *unused, cairo_t *cr, int width, int height, gpointer data) {
    (void)unused; (void)data;
    if (!active || !frame.pixels) return;
    double sx = (double)width / logical_width, sy = (double)height / logical_height;
    struct rectangle box = visible_box();
    struct rectangle painted = {box.x * sx, box.y * sy, box.width * sx, box.height * sy};
    cairo_rectangle(cr, 0, 0, width, height);
    if (valid(box)) cairo_rectangle(cr, painted.x, painted.y, painted.width, painted.height);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    set_color(cr, 0x030910, .59);
    cairo_fill(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    cairo_set_line_width(cr, 1);
    if (anchor_set && !dragging) {
        double x = anchor_x * sx, y = anchor_y * sy;
        cairo_move_to(cr, x - 6, y);
        cairo_line_to(cr, x + 6, y);
        cairo_move_to(cr, x, y - 6);
        cairo_line_to(cr, x, y + 6);
        set_color(cr, 0xb5e6ee, 1);
        cairo_stroke(cr);
    }
    if (!valid(box)) return;

    double inset = mode == CAPTURE_DISPLAY ? 1 : .5;
    cairo_rectangle(cr, painted.x + inset, painted.y + inset,
        MAX(0, painted.width - 2 * inset), MAX(0, painted.height - 2 * inset));
    if (mode == CAPTURE_WINDOW && !selected) {
        double dash[] = {4, 3};
        cairo_set_dash(cr, dash, 2, 0);
    }
    set_color(cr, 0xb5e6ee, 1);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    if (mode == CAPTURE_AREA && (!dragging || drag != DRAW_AREA)) {
        for (unsigned i = 0; i < 4; ++i) {
            double x = painted.x + (i & 1 ? painted.width : 0);
            double y = painted.y + (i & 2 ? painted.height : 0);
            cairo_rectangle(cr, x - 4, y - 4, 8, 8);
            set_color(cr, 0x193642, 1);
            cairo_fill_preserve(cr);
            set_color(cr, 0xbde5ee, 1);
            cairo_stroke(cr);
        }
    }
    draw_dimensions(cr, box, painted, width, height);
}

static struct rectangle span(double ax, double ay, double bx, double by) {
    return (struct rectangle){MIN(ax, bx), MIN(ay, by), fabs(bx - ax), fabs(by - ay)};
}

static struct rectangle move_box(struct rectangle box, double dx, double dy) {
    box.x = CLAMP(box.x + dx, 0, logical_width - box.width);
    box.y = CLAMP(box.y + dy, 0, logical_height - box.height);
    return box;
}

static gboolean contains(struct rectangle box, double x, double y) {
    return valid(box) && x >= box.x && y >= box.y && x < box.x + box.width && y < box.y + box.height;
}

static struct target *target_at(double x, double y) {
    for (guint i = targets->len; i > 0; --i) {
        struct target *item = g_ptr_array_index(targets, i - 1);
        if (contains(item->bounds, x, y)) return item;
    }
    return NULL;
}

static void logical_point(double *x, double *y) {
    int w = gtk_widget_get_width(canvas), h = gtk_widget_get_height(canvas);
    *x = CLAMP(*x * logical_width / MAX(1, w), 0, logical_width);
    *y = CLAMP(*y * logical_height / MAX(1, h), 0, logical_height);
}

static int nearest_corner(double x, double y) {
    if (!valid(area)) return -1;
    double tolerance_x = 9.0 * logical_width / MAX(1, gtk_widget_get_width(canvas));
    double tolerance_y = 9.0 * logical_height / MAX(1, gtk_widget_get_height(canvas));
    for (int i = 0; i < 4; ++i) {
        double cx = area.x + (i & 1 ? area.width : 0);
        double cy = area.y + (i & 2 ? area.height : 0);
        if (fabs(x - cx) <= tolerance_x && fabs(y - cy) <= tolerance_y) return i;
    }
    return -1;
}

static void selection_motion(GtkEventControllerMotion *unused, double x, double y, gpointer data) {
    (void)unused; (void)data;
    if (!active || dragging || waiting != EXPORT_NONE) return;
    logical_point(&x, &y);
    if (mode == CAPTURE_WINDOW) {
        struct target *hit = target_at(x, y);
        gtk_widget_set_cursor_from_name(canvas, hit ? "pointer" : "default");
        if (hovered != hit) {
            hovered = hit;
            gtk_widget_queue_draw(canvas);
        }
    } else if (mode == CAPTURE_AREA) {
        int corner = nearest_corner(x, y);
        const char *cursor = contains(area, x, y) ? "move" : "crosshair";
        if (corner >= 0) cursor = corner == 0 || corner == 3 ? "nwse-resize" : "nesw-resize";
        gtk_widget_set_cursor_from_name(canvas, cursor);
    }
}

static void selection_leave(GtkEventControllerMotion *unused, gpointer data) {
    (void)unused; (void)data;
    if (hovered) {
        hovered = NULL;
        gtk_widget_queue_draw(canvas);
    }
}

static void drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    (void)data;
    if (!active || mode == CAPTURE_DISPLAY || waiting != EXPORT_NONE) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_widget_grab_focus(canvas);
    logical_point(&x, &y);
    start_x = x;
    start_y = y;
    moved = FALSE;
    dragging = TRUE;
    initial_area = area;
    if (mode == CAPTURE_AREA) {
        int corner = nearest_corner(x, y);
        drag = corner >= 0 ? RESIZE_AREA : contains(area, x, y) ? MOVE_AREA : DRAW_AREA;
        if (corner >= 0) {
            opposite_x = area.x + (corner & 1 ? 0 : area.width);
            opposite_y = area.y + (corner & 2 ? 0 : area.height);
        }
    }
    update_controls();
}

static void drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    (void)gesture; (void)data;
    if (!dragging || mode != CAPTURE_AREA) return;
    if (!moved && hypot(dx, dy) < 4) return;
    moved = TRUE;
    double x = start_x + dx * logical_width / MAX(1, gtk_widget_get_width(canvas));
    double y = start_y + dy * logical_height / MAX(1, gtk_widget_get_height(canvas));
    x = CLAMP(x, 0, logical_width);
    y = CLAMP(y, 0, logical_height);
    if (drag == MOVE_AREA) area = move_box(initial_area, x - start_x, y - start_y);
    else if (drag == RESIZE_AREA) area = span(opposite_x, opposite_y, x, y);
    else area = span(start_x, start_y, x, y);
    set_message(NULL, FALSE);
    gtk_widget_queue_draw(canvas);
}

static void drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    if (!dragging) return;
    drag_update(gesture, dx, dy, data);
    dragging = FALSE;
    if (mode == CAPTURE_WINDOW) {
        selected = target_at(start_x, start_y);
    } else if (drag == DRAW_AREA && !moved) {
        if (anchor_set) {
            area = span(anchor_x, anchor_y, start_x, start_y);
            anchor_set = FALSE;
        } else {
            anchor_x = start_x;
            anchor_y = start_y;
            anchor_set = TRUE;
            area = (struct rectangle){0};
        }
    } else {
        anchor_set = FALSE;
    }
    if (!valid(area)) area = (struct rectangle){0};
    guidance();
    update_controls();
}

static void drag_cancel(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data) {
    (void)gesture; (void)sequence; (void)data;
    if (!dragging) return;
    dragging = FALSE;
    area = initial_area;
    guidance();
    update_controls();
}

static void change_mode(GtkButton *button, gpointer data) {
    (void)button;
    if (!active || waiting != EXPORT_NONE) return;
    mode = GPOINTER_TO_INT(data);
    selected = hovered = NULL;
    anchor_set = dragging = FALSE;
    gtk_widget_set_cursor_from_name(canvas, mode == CAPTURE_AREA ? "crosshair" : "default");
    guidance();
    update_controls();
}

static gboolean write_all(int fd, const guint8 *bytes, gsize size) {
    while (size != 0) {
        ssize_t written = write(fd, bytes, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return FALSE;
        bytes += written;
        size -= (gsize)written;
    }
    return TRUE;
}

static char *save_png(GBytes *png) {
    const char *pictures = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    g_autofree char *fallback = pictures ? NULL : g_build_filename(g_get_home_dir(), "Pictures", NULL);
    g_autofree char *folder = g_build_filename(pictures ? pictures : fallback, "Screenshots", NULL);
    if (g_mkdir_with_parents(folder, 0700) != 0) return NULL;

    g_autofree char *temp_path = g_build_filename(folder, ".mothership-screenshot-XXXXXX", NULL);
    int fd = g_mkstemp_full(temp_path, O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) return NULL;
    gsize size;
    const guint8 *data = g_bytes_get_data(png, &size);
    gboolean written = write_all(fd, data, size);
    int closed = close(fd);
    char *name = NULL;
    if (written && closed == 0) {
        g_autoptr(GDateTime) now = g_date_time_new_now_local();
        g_autofree char *stamp = g_date_time_format(now, "%Y-%m-%d_%H-%M-%S");
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            g_autofree char *basename = attempt
                ? g_strdup_printf("Screenshot_%s_%u.png", stamp, attempt)
                : g_strdup_printf("Screenshot_%s.png", stamp);
            g_autofree char *path = g_build_filename(folder, basename, NULL);
            if (renameat2(AT_FDCWD, temp_path, AT_FDCWD, path, RENAME_NOREPLACE) == 0) {
                name = g_steal_pointer(&basename);
                break;
            }
            if (errno != EEXIST) break;
        }
    }
    g_unlink(temp_path);
    return name;
}

static struct image crop_frame(GdkRectangle crop) {
    if (crop.width <= 0 || crop.height <= 0) return (struct image){0};
    int stride = crop.width * 4;
    gsize size = (gsize)stride * crop.height;
    guint8 *bytes = g_try_malloc(size);
    if (!bytes) return (struct image){0};
    const guint8 *source = g_bytes_get_data(frame.pixels, NULL);
    source += (gsize)crop.y * frame.stride + (gsize)crop.x * 4;
    for (int y = 0; y < crop.height; ++y)
        memcpy(bytes + (gsize)y * stride, source + (gsize)y * frame.stride, stride);
    return (struct image){g_bytes_new_take(bytes, size), crop.width, crop.height, stride};
}

static void complete_export(const struct image *image, enum export_action action) {
    g_autoptr(GdkTexture) texture = image->pixels ? image_texture(image) : NULL;
    g_autoptr(GBytes) png = texture ? gdk_texture_save_to_png_bytes(texture) : NULL;
    if (!png) {
        set_message("Could not prepare the screenshot. Try again.", TRUE);
        return;
    }

    g_autofree char *success = NULL;
    if (action == EXPORT_COPY) {
        GdkClipboard *clipboard = gtk_widget_get_clipboard(canvas);
        g_autoptr(GdkContentProvider) provider = gdk_content_provider_new_for_bytes("image/png", png);
        if (!gdk_clipboard_set_content(clipboard, provider)) {
            set_message("Could not copy the image. Try Save PNG.", TRUE);
            return;
        }
        success = g_strdup("Screenshot copied");
    } else {
        g_autofree char *name = save_png(png);
        if (!name) {
            set_message("Could not save to Screenshots. You can still copy the image.", TRUE);
            return;
        }
        success = g_strdup_printf("Saved Screenshots/%s", name);
    }
    finish(TRUE);
    show_notice(success);
}

static void export_clicked(GtkButton *button, gpointer data) {
    (void)button;
    enum export_action action = GPOINTER_TO_INT(data);
    if (!active || waiting != EXPORT_NONE || dragging) return;
    if (mode == CAPTURE_WINDOW) {
        if (!selected) return;
        waiting = action;
        set_message("Capturing window…", FALSE);
        update_controls();
        map_pointer_screenshot_window(selected->hi, selected->lo);
    } else if (mode == CAPTURE_DISPLAY) {
        complete_export(&frame, action);
    } else if (valid(area)) {
        struct image cropped = crop_frame(pixel_box(area));
        complete_export(&cropped, action);
        image_clear(&cropped);
    }
}

static void cancel_clicked(GtkButton *button, gpointer data) {
    (void)button; (void)data;
    if (active) finish(TRUE);
}

static void cycle_focus(gboolean previous) {
    GtkWidget *order[] = {canvas, modes[0], modes[1], modes[2], copy_button, save_button, cancel_button};
    int count = G_N_ELEMENTS(order), current = previous ? 0 : count - 1;
    GtkWidget *focus = gtk_window_get_focus(owner);
    for (int i = 0; i < count; ++i)
        if (focus == order[i]) current = i;
    for (int step = 1; step <= count; ++step) {
        int i = (current + (previous ? count - step : step)) % count;
        if (gtk_widget_is_sensitive(order[i]) && gtk_widget_grab_focus(order[i])) return;
    }
}

gboolean screenshot_visible(void) {
    return active;
}

gboolean panel_screenshot_enabled(void) {
    return available;
}

gboolean screenshot_key(guint key, GdkModifierType mods) {
    if (!active) return FALSE;
    const GdkModifierType command = GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK;
    if (key == GDK_KEY_Escape) {
        finish(TRUE);
        return TRUE;
    }
    if ((key == GDK_KEY_Tab || key == GDK_KEY_ISO_Left_Tab) && !(mods & command)) {
        cycle_focus(key == GDK_KEY_ISO_Left_Tab || (mods & GDK_SHIFT_MASK));
        return TRUE;
    }
    if (gtk_window_get_focus(owner) != canvas || mods & command || waiting != EXPORT_NONE) return FALSE;

    guint lower = gdk_keyval_to_lower(key);
    if (mode == CAPTURE_WINDOW && targets->len && (lower == GDK_KEY_w || lower == GDK_KEY_s)) {
        guint index;
        if (!selected || !g_ptr_array_find(targets, selected, &index)) index = targets->len - 1;
        else if (lower == GDK_KEY_w) index = (index + targets->len - 1) % targets->len;
        else index = (index + 1) % targets->len;
        selected = g_ptr_array_index(targets, index);
    } else if (mode == CAPTURE_AREA && (lower == GDK_KEY_w || lower == GDK_KEY_a ||
            lower == GDK_KEY_s || lower == GDK_KEY_d)) {
        double dx = lower == GDK_KEY_a ? -8 : lower == GDK_KEY_d ? 8 : 0;
        double dy = lower == GDK_KEY_w ? -8 : lower == GDK_KEY_s ? 8 : 0;
        if (!valid(area)) area = (struct rectangle){logical_width / 4., logical_height / 4.,
            logical_width / 2., logical_height / 2.};
        if (mods & GDK_SHIFT_MASK) {
            area.width = CLAMP(area.width + dx, 1, logical_width - area.x);
            area.height = CLAMP(area.height + dy, 1, logical_height - area.y);
        } else area = move_box(area, dx, dy);
        anchor_set = FALSE;
    } else return FALSE;
    guidance();
    update_controls();
    return TRUE;
}

static void opened_after_paint(GdkFrameClock *clock, gpointer data) {
    gulong *handler = data;
    if (pending) {
        map_pointer_screenshot_begin();
        gdk_display_flush(gtk_widget_get_display(surface));
    }
    g_signal_handler_disconnect(clock, *handler);
}

static void free_signal_data(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}

static gboolean wait_for_paint(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
    (void)widget; (void)data;
    gulong *handler = g_new(gulong, 1);
    *handler = g_signal_connect_data(clock, "after-paint", G_CALLBACK(opened_after_paint),
        handler, free_signal_data, G_CONNECT_AFTER);
    return G_SOURCE_REMOVE;
}

void panel_screenshot_available(gboolean supported) {
    available = supported;
    for (unsigned i = 0; i < G_N_ELEMENTS(entries); ++i)
        if (entries[i]) gtk_widget_set_visible(entries[i], supported);
    if (!supported && (active || pending)) finish(FALSE);
}

static void open_capture(gboolean invoking_menu) {
    if (!available || !surface || active || pending) return;
    pending = TRUE;
    if (invoking_menu) prepare_session(TRUE);
    g_weak_ref_set(&previous_focus, gtk_window_get_focus(owner));
    gtk_widget_add_tick_callback(GTK_WIDGET(owner), wait_for_paint, NULL, NULL);
    gtk_widget_queue_draw(GTK_WIDGET(owner));
}

void panel_screenshot_open(void) {
    open_capture(FALSE);
}

void screenshot_open_from_menu(void) {
    open_capture(TRUE);
}

void panel_screenshot_target(uint32_t hi, uint32_t lo, int x, int y, int width, int height, const char *title) {
    if (width <= 0 || height <= 0) {
        if (!active) return;
        for (guint i = 0; i < targets->len; ++i) {
            struct target *item = g_ptr_array_index(targets, i);
            if (item->hi != hi || item->lo != lo) continue;
            if (hovered == item) hovered = NULL;
            if (selected == item) {
                selected = NULL;
                waiting = EXPORT_NONE;
            }
            g_ptr_array_remove_index(targets, i);
            guidance();
            update_controls();
            break;
        }
        return;
    }
    if (!pending) return;
    struct target *item = g_new0(struct target, 1);
    *item = (struct target){hi, lo, {x, y, width, height}, g_utf8_make_valid(title, -1)};
    g_ptr_array_add(targets, item);
}

void panel_screenshot_frame(int fd, int width, int height, int stride, int logical_w, int logical_h) {
    if (!pending) {
        if (fd >= 0) close(fd);
        return;
    }
    struct image received = image_from_fd(fd, width, height, stride);
    if (!received.pixels || logical_w <= 0 || logical_h <= 0) {
        image_clear(&received);
        finish(TRUE);
        show_notice("Could not open the screenshot");
        return;
    }

    frame = received;
    logical_width = logical_w;
    logical_height = logical_h;
    // Hit and outline only the part of each window visible on this output.
    for (guint i = 0; i < targets->len; ++i) {
        struct target *item = g_ptr_array_index(targets, i);
        struct rectangle r = item->bounds;
        item->bounds = span(CLAMP(r.x, 0, logical_w), CLAMP(r.y, 0, logical_h),
            CLAMP(r.x + r.width, 0, logical_w), CLAMP(r.y + r.height, 0, logical_h));
    }
    g_autoptr(GdkTexture) texture = image_texture(&frame);
    gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(texture));
    pending = FALSE;
    active = TRUE;
    prepare_session(FALSE);
    area = (struct rectangle){0};
    gtk_widget_set_visible(surface, TRUE);
    change_mode(NULL, GINT_TO_POINTER(CAPTURE_AREA));
    gtk_widget_grab_focus(modes[0]);
    update_region();
}

void panel_screenshot_image(int fd, int width, int height, int stride) {
    if (!active || waiting == EXPORT_NONE) {
        if (fd >= 0) close(fd);
        return;
    }
    enum export_action action = waiting;
    waiting = EXPORT_NONE;
    struct image image = image_from_fd(fd, width, height, stride);
    complete_export(&image, action);
    image_clear(&image);
    if (active) update_controls();
}

void panel_screenshot_problem(const char *text, gboolean terminal) {
    if (!pending && !active) return;
    if (terminal || pending) {
        finish(FALSE);
        if (text && *text) show_notice(text);
        return;
    }

    waiting = EXPORT_NONE;
    set_message(text && *text ? text : "Could not capture the window", TRUE);
    update_controls();
}

static void entry_clicked(GtkButton *button, gpointer data) {
    (void)button; (void)data;
    screenshot_open_from_menu();
}

static GtkWidget *make_button(GtkWidget *row, const char *label, GCallback clicked, int data) {
    GtkWidget *button = gtk_button_new_with_label(label);
    analog_button_style(button, 9, 7);
    g_signal_connect(button, "clicked", clicked, GINT_TO_POINTER(data));
    gtk_box_append(GTK_BOX(row), button);
    return button;
}

static void toolbar_reflow(GtkDrawingArea *area_, int width, int height, gpointer data) {
    (void)area_; (void)height; (void)data;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(controls),
        width < 350 ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
}

void screenshot_init(GtkWindow *window, GtkOverlay *root, GtkWidget *normal_entry, GtkWidget *overview_entry,
        void (*prepare)(gboolean), void (*region_changed)(void), void (*notice)(const char *)) {
    owner = window;
    entries[0] = normal_entry;
    entries[1] = overview_entry;
    for (unsigned i = 0; i < G_N_ELEMENTS(entries); ++i) {
        g_signal_connect(entries[i], "clicked", G_CALLBACK(entry_clicked), NULL);
        gtk_widget_set_visible(entries[i], available);
    }
    prepare_session = prepare;
    update_region = region_changed;
    show_notice = notice;
    g_weak_ref_init(&previous_focus, NULL);
    targets = g_ptr_array_new_with_free_func(target_free);

    surface = gtk_overlay_new();
    gtk_widget_set_visible(surface, FALSE);
    gtk_overlay_add_overlay(root, surface);
    picture = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_FILL);
    gtk_widget_set_can_target(picture, FALSE);
    gtk_overlay_set_child(GTK_OVERLAY(surface), picture);

    canvas = gtk_drawing_area_new();
    gtk_widget_set_focusable(canvas, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(canvas), GTK_ACCESSIBLE_PROPERTY_LABEL, "Capture selection",
        GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Use W A S D to adjust the area or to choose a window. Hold Shift to resize an area.", -1);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(canvas), draw_selection, NULL, NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(surface), canvas);

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(selection_motion), NULL);
    g_signal_connect(motion, "leave", G_CALLBACK(selection_leave), NULL);
    gtk_widget_add_controller(canvas, motion);
    GtkGesture *gesture = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect(gesture, "drag-begin", G_CALLBACK(drag_begin), NULL);
    g_signal_connect(gesture, "drag-update", G_CALLBACK(drag_update), NULL);
    g_signal_connect(gesture, "drag-end", G_CALLBACK(drag_end), NULL);
    g_signal_connect(gesture, "cancel", G_CALLBACK(drag_cancel), NULL);
    gtk_widget_add_controller(canvas, GTK_EVENT_CONTROLLER(gesture));

    GtkWidget *cluster = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_halign(cluster, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(cluster, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(cluster, HOME_BOTTOM + HOME_HEIGHT + 16);
    gtk_widget_set_margin_start(cluster, 8);
    gtk_widget_set_margin_end(cluster, 8);
    gtk_overlay_add_overlay(GTK_OVERLAY(surface), cluster);

    message = gtk_label_new(NULL);
    gtk_widget_add_css_class(message, "screenshot-message");
    gtk_widget_set_halign(message, GTK_ALIGN_CENTER);
    gtk_widget_set_can_target(message, FALSE);
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(message), 50);
    gtk_box_append(GTK_BOX(cluster), message);

    GtkWidget *bar = gtk_overlay_new();
    gtk_box_append(GTK_BOX(cluster), bar);
    GtkWidget *face = gtk_drawing_area_new();
    gtk_widget_set_can_target(face, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(face), draw_toolbar, NULL, NULL);
    gtk_overlay_set_child(GTK_OVERLAY(bar), face);
    controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(controls, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(controls, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(controls, 5);
    gtk_widget_set_margin_end(controls, 5);
    gtk_widget_set_margin_top(controls, 4);
    gtk_widget_set_margin_bottom(controls, 4);
    gtk_overlay_add_overlay(GTK_OVERLAY(bar), controls);
    gtk_overlay_set_measure_overlay(GTK_OVERLAY(bar), controls, TRUE);
    GtkWidget *mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    gtk_box_append(GTK_BOX(controls), mode_row);
    gtk_box_append(GTK_BOX(controls), action_row);
    modes[CAPTURE_AREA] = make_button(mode_row, "Area", G_CALLBACK(change_mode), CAPTURE_AREA);
    modes[CAPTURE_WINDOW] = make_button(mode_row, "Window", G_CALLBACK(change_mode), CAPTURE_WINDOW);
    modes[CAPTURE_DISPLAY] = make_button(mode_row, "Display", G_CALLBACK(change_mode), CAPTURE_DISPLAY);
    copy_button = make_button(action_row, "Copy", G_CALLBACK(export_clicked), EXPORT_COPY);
    gtk_widget_add_css_class(copy_button, "screenshot-primary");
    save_button = make_button(action_row, "Save PNG", G_CALLBACK(export_clicked), EXPORT_SAVE);
    cancel_button = make_button(action_row, "×", G_CALLBACK(cancel_clicked), 0);
    gtk_accessible_update_property(GTK_ACCESSIBLE(cancel_button), GTK_ACCESSIBLE_PROPERTY_LABEL, "Cancel screenshot", -1);
    g_signal_connect(canvas, "resize", G_CALLBACK(toolbar_reflow), NULL);

    GtkCssProvider *style = gtk_css_provider_new();
    gtk_css_provider_load_from_string(style,
        ".screenshot-message { color: #dceef5; background: #101e28; border: 1px solid #547884; padding: 3px 8px; }"
        ".screenshot-message.screenshot-error { color: #ffded0; border-color: #9c6659; }"
        "button.screenshot-primary.analog-button { color: #a8f1f4; }"
        "button.screenshot-primary.analog-button:disabled { color: #728693; }");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(surface), GTK_STYLE_PROVIDER(style),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    g_object_unref(style);
}

/*             (c) 2014 vaddr -- MIT license; see vtabs/LICENSE              */

#include <X11/Xlib.h>

extern int x11_active_desktop;
extern int x11_num_desktops;

int x11_init(Display *dpy, Window root);
int x11_handle_event(XEvent *ev);
const char* x11_get_desktop_name(int index);
int x11_set_desktop_name(int index, const char *new_name);
int x11_set_num_desktops(int count);
int x11_set_active_desktop(int index);
int x11_move_windows(int from, int to);


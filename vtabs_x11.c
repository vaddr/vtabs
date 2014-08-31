/*             (c) 2014 vaddr -- MIT license; see vtabs/LICENSE              */
#include "vtabs_x11.h"
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_GETHOSTNAME
# include <unistd.h>
#elif defined(HAVE_UNAME)
# include <sys/utsname.h>
#endif

static Display *dpy  = NULL;
static Window   root = None;

static Atom _NET_NUMBER_OF_DESKTOPS;
static Atom _NET_CURRENT_DESKTOP;
static Atom _NET_DESKTOP_NAMES;
static Atom _NET_CLIENT_LIST;
static Atom _NET_WM_DESKTOP;
static Atom _NET_WM_PID;

static uint32_t num_desktops      = 0;
static uint32_t active_desktop    = 0;
static char**   desktop_names     = NULL;
static uint32_t num_desktop_names = 0;

static uint32_t x11_get_u32_prop(Window w, Atom atom);
static void     x11_get_desktop_names(void);

typedef struct {
    Window   window;
    uint32_t pid;     // 0 if unknown / not on localhost
    uint32_t desktop; // 0xffffffff means sticky or unknown
} wininfo_t;

// The window list is totally unordered, and may be realloced.
static wininfo_t *win_list       = NULL;
static uint32_t   win_list_size  = 0;
static uint32_t   win_list_alloc = 0;

static wininfo_t *win_list_add(Window window);
static int win_list_remove(wininfo_t *window);
static wininfo_t *win_list_get(Window window);

int x11_init(Display *_dpy, Window _root)
{
    dpy  = _dpy;
    root = _root;

    // Cache atoms we'll need later.
    _NET_NUMBER_OF_DESKTOPS = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", 0);
    _NET_CURRENT_DESKTOP    = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", 0);
    _NET_DESKTOP_NAMES      = XInternAtom(dpy, "_NET_DESKTOP_NAMES", 0);
    _NET_CLIENT_LIST        = XInternAtom(dpy, "_NET_CLIENT_LIST", 0);
    _NET_WM_DESKTOP         = XInternAtom(dpy, "_NET_WM_DESKTOP", 0);
    _NET_WM_PID             = XInternAtom(dpy, "_NET_WM_PID", 0);
    
    // Setup event listening on the root window so we can be pushed relevant
    // events.
    XSelectInput(dpy, root, SubstructureNotifyMask |
                            StructureNotifyMask    |
                            PropertyChangeMask);

    // Query for the initial state.
    num_desktops   = x11_get_u32_prop(root, _NET_NUMBER_OF_DESKTOPS);
    active_desktop = x11_get_u32_prop(root, _NET_CURRENT_DESKTOP);
    x11_get_desktop_names();

    // Add all existing windows. 
    // TODO: there is a race here, in that by the time we get around to 
    // querying for the window properties, it may already be gone. 
    Atom ret_type;
    int ret_fmt;
    unsigned long ret_n;
    unsigned long bytes_after;
    unsigned char *val;
    if (XGetWindowProperty(dpy, root, _NET_CLIENT_LIST, 0, (1 << 20), 0, 
                AnyPropertyType, &ret_type, &ret_fmt, &ret_n, &bytes_after, 
                &val) != Success) {
        fprintf(stderr, "Failed to retrieve client list\n");
        return 0;
    }
    
    for (int i = 0; i < ret_n; i++)
        win_list_add(((Window*)val)[i]);

    XFree(val);

    return 1;
}

static int x11_handle_property_event(XPropertyEvent *ev);

int x11_handle_event(XEvent *ev)
{
    // Return 1 if the event was handled. 
    // (TODO: maybe change this to return whether redraw is needed)

    switch (ev->type) {
        case PropertyNotify:
            return x11_handle_property_event((XPropertyEvent*)ev);
        case CreateNotify: 
            return win_list_add(ev->xany.window) != NULL;
        case DestroyNotify:
            return win_list_remove(win_list_get(ev->xany.window));
        case MapNotify:
        case UnmapNotify:
        default: return 0;
    }
}

static wininfo_t *win_list_add(Window window)
{
    if (win_list_size == win_list_alloc) {
        win_list_alloc = (win_list_alloc ? 2 * win_list_alloc : 32);
        win_list = realloc(win_list, win_list_alloc * sizeof(win_list[0]));
    }
    
    wininfo_t *rv = &win_list[win_list_size++];
    rv->window  = window;
    rv->pid     = 0;
    rv->desktop = 0;
    
    rv->desktop = x11_get_u32_prop(window, _NET_WM_DESKTOP);
   
    XTextProperty host = { 0 };
    if (XGetWMClientMachine(dpy, window, &host) && host.value) {
        int localhost = 0;

        // String comparison of hostname seems vaguely sketchy. 

#ifdef HAVE_GETHOSTNAME
        char buf[256] = { 0 };
        gethostname(buf, 256);
        localhost = (strcmp(buf, host.value) == 0);
#elif defined(HAVE_UNAME)
        struct utsname name;
        uname(&name);
        localhost = (strcmp(name.nodename, host.value) == 0);
#else
        // hope for the best...
        localhost = 1;
#endif

        if (localhost)
            rv->pid = x11_get_u32_prop(window, _NET_WM_PID);

        XFree(host.value);
    }

    // We want to know when _NET_WM_DESKTOP changes
    XSelectInput(dpy, window, PropertyChangeMask);

    return rv;
}

static int win_list_remove(wininfo_t *window)
{
    if (window == NULL)
        return 0;

    if (--win_list_size > 0)
        *window = win_list[win_list_size];

    return 1;
}

static wininfo_t *win_list_get(Window window)
{
    for (int i = 0; i < win_list_size; i++)
        if (win_list[i].window == window)
            return &win_list[i];

    return NULL;
}

static int x11_handle_property_event(XPropertyEvent *ev)
{
    if (ev->window != root)
        return 0;

    if (ev->atom == _NET_NUMBER_OF_DESKTOPS) {
        num_desktops = x11_get_u32_prop(root, ev->atom);
        return 1;
    } else if (ev->atom == _NET_CURRENT_DESKTOP) {
        active_desktop = x11_get_u32_prop(root, ev->atom);
        return 1;
    } else if (ev->atom == _NET_DESKTOP_NAMES) {
        x11_get_desktop_names();
        return 1;
    }

    return 0;
}

static uint32_t x11_get_u32_prop(Window w, Atom atom)
{
    Atom ret_type;
    int ret_fmt;
    unsigned long ret_n;
    unsigned long bytes_after;
    unsigned char *val;

    if (XGetWindowProperty(dpy, w, atom, 0, 1, 0, AnyPropertyType, 
                &ret_type, &ret_fmt, &ret_n, &bytes_after, &val) != Success)
        return 0;

    if (ret_n != 1)
        return 0;

    uint32_t rv = *(uint32_t*)val;
    XFree(val);

    return rv;
}

static void x11_get_desktop_names(void)
{
    // TODO: consider using XGetTextProperty instead.

    Atom ret_type;
    int ret_fmt;
    unsigned long ret_n;
    unsigned long bytes_after;
    unsigned char *val;

    if (XGetWindowProperty(dpy, root, _NET_DESKTOP_NAMES, 0, (1 << 20), 0, 
                AnyPropertyType, &ret_type, &ret_fmt, &ret_n, &bytes_after, 
                &val) != Success) {
        // just leave the existing names, if any, on failure to retrieve names
        return;
    }

    // Count number of strings
    int n_strings = 0;
    for (char *p = val; *p; p += strlen(p) + 1) 
        n_strings++;

    // Copy strings into new array. 
    char **str_list = malloc(n_strings * sizeof(char*));
    int i = 0;
    for (char *p = val; *p; i++) {
        int len = strlen(p) + 1;
        str_list[i] = malloc(len);
        memcpy(str_list[i], p, len);
        p += len;
    }

    // Free the old desktop name array.
    for (i = 0; i < num_desktop_names; i++)
        free(desktop_names[i]);
    free(desktop_names);

    desktop_names     = str_list;
    num_desktop_names = n_strings;

    XFree(val);
}


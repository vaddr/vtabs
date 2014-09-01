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

#define X11_MAX_DESKTOPS 1024

// declared in vtabs.c
extern int verbose;
extern int no_action;

// externed for read-only use in other files
int x11_num_desktops      = 0;
int x11_active_desktop    = 0;

static char*    desktop_names[X11_MAX_DESKTOPS];
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
    x11_num_desktops   = x11_get_u32_prop(root, _NET_NUMBER_OF_DESKTOPS);
    x11_active_desktop = x11_get_u32_prop(root, _NET_CURRENT_DESKTOP);
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

const char* x11_get_desktop_name(int index)
{
    if (index < 0 || index >= num_desktop_names)
        return NULL;

    return desktop_names[index];
}

int x11_set_desktop_name(int index, const char *new_name)
{
    if (index < 0 || index >= x11_num_desktops) {
        fprintf(stderr, "Can't rename desktop %d; index out of range\n", index);
        return 0;
    }

    if (new_name == NULL || new_name[0] == '\0')
        new_name = " ";

    // The names array is not required to be as long as the number of desktops,
    // so make it longer if need be.
    if (index >= num_desktop_names) {
        for (int i = num_desktop_names; i < index+1; i++) {
            // I believe it is invalid to have empty strings in this list.
            desktop_names[i] = malloc(2);
            desktop_names[i][0] = ' ';
            desktop_names[i][1] = '\0';
        }
        num_desktop_names = index+1;
    }

    if (desktop_names[index])
        free(desktop_names[index]);

    int n = strlen(new_name)+1;
    desktop_names[index] = malloc(n);
    memcpy(desktop_names[index], new_name, n);

    // Now that we've updated our internal state, update the property on the 
    // window manager's side. 

    XTextProperty prop;
    if (!XStringListToTextProperty(desktop_names, num_desktop_names, &prop)) {
        fprintf(stderr, "XStringListToTextProperty failed\n");
        return 0;
    }

    XSetTextProperty(dpy, root, &prop, _NET_DESKTOP_NAMES);
    XFree(prop.value);

    return 1;
}

static int x11_client_message(Window win, Atom type, long l0, long l1);

int x11_set_num_desktops(int count)
{
    if (count == x11_num_desktops)
        return 1;

    if (count < 1 || count > X11_MAX_DESKTOPS) {
        fprintf(stderr, "Invalid desktop count: %d\n", count);
        return 0;
    }

    if (verbose) 
        printf("Setting number of desktops to %d\n", count);

    if (no_action) {
        // pretend it worked
        x11_num_desktops = count;
        return 1;
    }

    if (!x11_client_message(root, _NET_NUMBER_OF_DESKTOPS, count, 0)) {
        fprintf(stderr, "Failed to change number of desktops\n");
        return 0;
    }

    // For now, assume it will succeed
    x11_num_desktops = count;

    return 1;
}

int x11_set_active_desktop(int index)
{
    if (index == x11_active_desktop)
        return 1;
    
    if (index < 0 || index >= x11_num_desktops) {
        fprintf(stderr, "Invalid desktop: %d\n", index);
        return 0;
    }

    if (verbose)
        printf("Setting active desktop to %d\n", index);

    if (no_action)
        return 1;

    if (!x11_client_message(root, _NET_CURRENT_DESKTOP, index, 0)) {
        fprintf(stderr, "Failed to switch to desktop %d\n", index);
        return 0;
    }
    
    return 1;
}

int x11_move_windows(int from, int to)
{
    if (from == to)
        return 1;
    
    if (from < 0 || from >= x11_num_desktops) {
        fprintf(stderr, "Invalid desktop: %d\n", from);
        return 0;
    }
    
    if (to < 0 || to >= x11_num_desktops) {
        fprintf(stderr, "Invalid desktop: %d\n", to);
        return 0;
    }

    for (int i = 0; i < win_list_size; i++) {
        wininfo_t *w = &win_list[i];
        if (w->desktop == from) {
            if (verbose) {
                printf("Moving window 0x%x from %d to %d\n", 
                        w->window, from, to);
            }

            if (no_action) {
                // pretend it worked
                w->desktop = to;
                continue;
            }

            if (!x11_client_message(w->window, _NET_WM_DESKTOP, to, 2)) {
                fprintf(stderr, "Failed to move window 0x%x", w->window);
                return 0;
            }
        }
    }

    return 1;
}

static int x11_client_message(Window win, Atom type, long l0, long l1)
{
    XEvent ev = {
        .xclient = {
            .window       = win,
            .type         = ClientMessage,
            .send_event   = 1,
            .display      = dpy,
            .message_type = type,
            .format       = 32,
            .data.l[0]    = l0,
            .data.l[1]    = l1,
        }
    };
    static const long mask = SubstructureRedirectMask | SubstructureNotifyMask;
    return XSendEvent(dpy, root, 0, mask, &ev);
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

    if (verbose) {
        printf("Window 0x%x on desktop %d with pid %d\n", 
                rv->window, rv->desktop, rv->pid);
    }

    // We want to know when _NET_WM_DESKTOP changes
    XSelectInput(dpy, window, PropertyChangeMask);

    return rv;
}

static int win_list_remove(wininfo_t *window)
{
    if (window == NULL)
        return 0;

    if (verbose)
        printf("Window 0x%x went away\n", window->window);

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
        if (verbose)
            printf("_NET_NUMBER_OF_DESKTOPS changed\n");
        x11_num_desktops = x11_get_u32_prop(root, ev->atom);
        return 1;
    } else if (ev->atom == _NET_CURRENT_DESKTOP) {
        if (verbose)
            printf("_NET_CURRENT_DESKTOP changed\n");
        x11_active_desktop = x11_get_u32_prop(root, ev->atom);
        return 1;
    } else if (ev->atom == _NET_DESKTOP_NAMES) {
        if (verbose)
            printf("_NET__DESKTOP_NAMES changed\n");
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

    // Free the old desktop name strings.
    for (int i = 0; i < num_desktop_names; i++)
        free(desktop_names[i]);
    num_desktop_names = 0;

    // Copy the strings into the array and count them.
    char *p = val;
    for (char *p = val; *p;) {
        int n = strlen(p) + 1;
        char *s = desktop_names[num_desktop_names++] = malloc(n);
        memcpy(s, p, n);
        p += n;
    }

    XFree(val);
}


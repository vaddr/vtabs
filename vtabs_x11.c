/*             (c) 2014 vaddr -- MIT license; see vtabs/LICENSE              */
#include "vtabs_x11.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Display *dpy  = NULL;
static Window   root = None;

static Atom _NET_NUMBER_OF_DESKTOPS;
static Atom _NET_CURRENT_DESKTOP;
static Atom _NET_DESKTOP_NAMES;
static Atom _NET_CLIENT_LIST;
static Atom _NET_WM_PID;
static Atom WM_CLIENT_MACHINE;

static uint32_t num_desktops      = 0;
static uint32_t active_desktop    = 0;
static char**   desktop_names     = NULL;
static uint32_t num_desktop_names = 0;

static uint32_t x11_get_u32_prop(Window w, Atom atom);
static void     x11_get_desktop_names(void);

int x11_init(Display *_dpy, Window _root)
{
    dpy  = _dpy;
    root = _root;

    // Cache atoms we'll need later.
    _NET_NUMBER_OF_DESKTOPS = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", 0);
    _NET_CURRENT_DESKTOP    = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", 0);
    _NET_DESKTOP_NAMES      = XInternAtom(dpy, "_NET_DESKTOP_NAMES", 0);
    _NET_CLIENT_LIST        = XInternAtom(dpy, "_NET_CLIENT_LIST", 0);
    _NET_WM_PID             = XInternAtom(dpy, "_NET_WM_PID", 0);
    WM_CLIENT_MACHINE       = XInternAtom(dpy, "WM_CLIENT_MACHINE", 0);
    
    // Setup event listening on the root window so we can be pushed relevant
    // events.
    XSelectInput(dpy, root, SubstructureNotifyMask |
                            StructureNotifyMask    |
                            PropertyChangeMask);

    // Query for the initial state.
    num_desktops   = x11_get_u32_prop(root, _NET_NUMBER_OF_DESKTOPS);
    active_desktop = x11_get_u32_prop(root, _NET_CURRENT_DESKTOP);
    x11_get_desktop_names();

    // TODO: do we really want to keep state on every active window?
    // might be better to poll

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
        case DestroyNotify:
        case MapNotify:
        case UnmapNotify:
        default: return 0;
    }
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

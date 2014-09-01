/*             (c) 2014 vaddr -- MIT license; see vtabs/LICENSE              */

#include "pstree.h"
#include "vtabs_x11.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#define INT_UNSET 0x80000000

static char *my_name = NULL;

static void usage(const char *fmt, ...) 
{
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
    }

#define USAGE \
"Usage: %s [<options>] <command> [<command> ...]\n\n"                          \
"Commands:\n"                                                                  \
"  add [-i <index>] [-n <name>]\n"                                             \
"    Adds a new desktop.\n"                                                    \
"    -i: insert the new desktop at the given index (default: end of list)\n"   \
"    -n: specifies a name for the new desktop (default: empty string)\n"       \
"    -c: stay on current desktop (default is to switch to the new one)\n"      \
"\n"                                                                           \
"  remove [-i <index>] [-c] [-s <index>] [-d <index>]\n"                       \
"    Removes a desktop and moves (or closes) orphaned windows.\n"              \
"    -i: specify the desktop to remove (default: active desktop)\n"            \
"    -c: attempt to close orphaned windows\n"                                  \
"    -s: specify the desktop to switch to (default: same or new highest)\n"    \
"    -d: specify a destination for orphaned windows (default: new active)\n"   \
"\n"                                                                           \
"  rename [-i <index>] [-n <name>]\n"                                          \
"    Renames an existing desktop.\n"                                           \
"    -i: specify the desktop to rename (default: active desktop)\n"            \
"    -n: specify the name of the new desktop (default: empty string)\n"        \
"\n"                                                                           \
"  switch (-i <index> | -d <delta> | -r <delta>)\n"                            \
"    Switches the active desktop.\n"                                           \
"    -i: specify an absolute index to switch to\n"                             \
"    -d: specify a delta to shift by, stopping at the first or last\n"         \
"    -r: specify a delta to rotate by, wrapping around the ends\n"             \
"\n"                                                                           \
"  move -d <index> [-s <index>]\n"                                             \
"    Move windows from one desktop to another.\n"                              \
"    -d: specify the destination for moved windows\n"                          \
"    -s: specify the desktop to move windows from (default: active desktop)\n" \
"\n"                                                                           \
"  clear [-i <index>]\n"                                                       \
"    Attempt to close windows on a desktop.\n"                                 \
"    -i: specify the desktop whose windows are to be closed\n"                 \
"\n"                                                                           \
"Options:\n"                                                                   \
"    -v: verbose mode\n"                                                       \
"    -p: preview mode (verbose, but don't take any action)\n"                  \
"    -f: specify path to vtabsrc (default: ~/.config/vtabsrc)\n"               \
"\n"

    fprintf(stderr, USAGE, my_name);
    exit(1);
}

static Display *dpy  = NULL;
static Window   root = None;

static char *rcfile = "~/config/vtabsrc";

// TODO: might be better to error out on invalid indices
static int normalize(int index) {
    if (index < 0 || index >= x11_num_desktops)
        return x11_num_desktops - 1;
    return index;
}

// These are externed elsewhere
int verbose   = 0;
int no_action = 0;

// For option parsing
static int get_flag(char ***args, char flag);
static int get_int_flag(char ***args, char flag, int *val);
static int get_str_flag(char ***args, char flag, char **val);

static char** do_add(char **args);
static char** do_remove(char **args);
static char** do_rename(char **args);
static char** do_switch(char **args);
static char** do_move(char **args);
static char** do_clear(char **args);

int main(int argc, char **argv)
{
    my_name = argv[0];
    if (strrchr(my_name, '/'))
        my_name = strrchr(argv[0], '/') + 1;

    if ((dpy = XOpenDisplay(NULL)) == NULL)
        return 1;

    root = DefaultRootWindow(dpy);

    if (!x11_init(dpy, root))
        return 1;

    // Process global options
    char **args = argv + 1;
    while (*args) {
        if (args[0][0] != '-')
            break;

        if (get_flag(&args, 'v')) {
            verbose = 1;
        } else if (get_flag(&args, 'p')) {
            verbose = no_action = 1;
        } else if (get_str_flag(&args, 'f', &rcfile)) {
            // When the rc file is explicitly specified, throw an error
            // if it doesn't exist. We don't do this for the default.
            if (access(rcfile, F_OK) == -1)
                usage("Specified config doesn't exist: %s\n", rcfile);
        } else {
            usage("Unrecognized option: %s\n", args[0]);
        }
    }

    // Read the config if it exists
    if (access(rcfile, F_OK) != -1) {
        FILE *f = fopen(rcfile, "r");
        if (!f) {
            perror(rcfile);
            return 1;
        }

        // TODO

        fclose(f);
    }

    if (!args[0])
        usage("No commands specified.\n");

    while (*args) {

        // Prior to each command, handle pending events.
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            x11_handle_event(&ev);
        }

        if (strcmp(args[0], "add") == 0) {
            args = do_add(args+1);
        } else if (strcmp(args[0], "remove") == 0) {
            args = do_remove(args+1);
        } else if (strcmp(args[0], "rename") == 0) {
            args = do_rename(args+1);
        } else if (strcmp(args[0], "switch") == 0) {
            args = do_switch(args+1);
        } else if (strcmp(args[0], "move") == 0) {
            args = do_move(args+1);
        } else if (strcmp(args[0], "clear") == 0) {
            args = do_clear(args+1);
        } else {
            usage("Unrecognized command: %s\n", args[0]);
        }

        // Sync after each command
        XSync(dpy, 0);

    }

    return 0;
}

//////////////////////////////// Commands /////////////////////////////////////

static char** do_add(char **args)
{
    int   index = INT_UNSET;
    int   stay  = 0;
    char *name  = NULL;

    while (*args) {
        if (args[0][0] != '-') break;
        if (get_int_flag(&args, 'i', &index)) {
        } else if (get_str_flag(&args, 'n', &name)) {
        } else if (get_flag(&args, 'c')) {
            stay = 1;
        } else usage("Unrecognized option to add: %s\n", args[0]);
    }

    // Make sure index is valid
    if (index < 0 || index > x11_num_desktops)
        index = x11_num_desktops;

    // Increase the number of desktops by 1
    if (!x11_set_num_desktops(x11_num_desktops+1))
        exit(1);

    if (index != x11_num_desktops - 1) {
        // Hard case: new desktop goes in the middle, so we need to rename 
        // all the desktops that were offset and move their windows.
        for (int i = x11_num_desktops - 1; i > index; i--) {
            if (!x11_set_desktop_name(i, x11_get_desktop_name(i-1)))
                exit(1);
            if (!x11_move_windows(i-1, i))
                exit(1);
        }
    }
    
    x11_set_desktop_name(index, name);
    
    // To stay on the current desktop will actually require a switch if the
    // desktop being added is earlier in the list.
    if (stay && x11_active_desktop >= index) {
        stay = 0;
        index = x11_active_desktop + 1;
    }

    // Switch to the new desktop (or to stay on the current desktop)
    if (!stay && !x11_set_active_desktop(index))
        exit(1);

    return args;
}

static char** do_remove(char **args)
{
    int index    = INT_UNSET;
    int switchto = INT_UNSET;
    int dest     = INT_UNSET;
    int close    = 0;

    while (*args) {
        if (args[0][0] != '-') break;
        if (get_int_flag(&args, 'i', &index)) {
        } else if (get_int_flag(&args, 's', &switchto)) {
        } else if (get_int_flag(&args, 'd', &dest)) {
        } else if (get_flag(&args, 'c')) {
            close = 1;
        } else usage("Unrecognized option to remove: %s\n", args[0]);
    }
    
    if (x11_num_desktops == 1) {
        fprintf(stderr, "Can't remove the only desktop\n");
        exit(1);
    }

    // TODO: respect close flag


    // Make sure index is valid.
    if (index == INT_UNSET)
        index = x11_active_desktop;
    if (index < 0 || index >= x11_num_desktops)
        index = x11_num_desktops - 1;
    
    // Finalize the desktop to switch to.
    // The index is pre-removal, so it may need to be decremented.
    if (switchto == INT_UNSET)
        switchto = x11_active_desktop;
    if (switchto < 0 || switchto >= x11_num_desktops)
        switchto = x11_num_desktops - 1;
    if (switchto > index)
        switchto--;

    // Finalize the desktop to move orphans to.
    if (dest == INT_UNSET)
        dest = switchto;
    if (dest < 0 || dest >= x11_num_desktops - 1)
        dest = x11_num_desktops - 2;

    if (index == x11_num_desktops - 1) {
        // Simple case: remove last desktop
        if (!x11_move_windows(index, dest))
            exit(1);
        if (!x11_set_num_desktops(x11_num_desktops-1))
            exit(1);
    } else {
        // Hard case: remove from middle, so we need to shift and rename 
        // desktops that came after. 

        

    }

    if (!x11_set_active_desktop(switchto))
        exit(1);

    return args;
}

static char** do_rename(char **args)
{
    int   index = INT_UNSET;
    char *name  = NULL;

    while (*args) {
        if (args[0][0] != '-') break;
        if (get_int_flag(&args, 'i', &index)) {
        } else if (get_str_flag(&args, 'n', &name)) {
        } else usage("Unrecognized option to rename: %s\n", args[0]);
    }

    index = normalize(index);
    if (!x11_set_desktop_name(index, name))
        exit(1);

    return args;
}

static char** do_switch(char **args)
{
    int index  = INT_UNSET;
    int rotate = INT_UNSET;
    int delta  = INT_UNSET;
    
    while (*args) {
        if (args[0][0] != '-') break;
        if (get_int_flag(&args, 'i', &index)) {
        } else if (get_int_flag(&args, 'r', &rotate)) {
        } else if (get_int_flag(&args, 'd', &delta)) {
        } else usage("Unrecognized option to switch: %s\n", args[0]);
    }

    // exactly one of index, rotate, delta must be specified
    if (index != INT_UNSET) {
        if (rotate != INT_UNSET || delta != INT_UNSET)
            goto fail;

        index = normalize(index);

    } else if (rotate != INT_UNSET) {
        if (delta != INT_UNSET)
            goto fail;

        index = x11_active_desktop + rotate;
        if (index < 0)
            index += x11_num_desktops * -(index / x11_num_desktops - 1);
        index %= x11_num_desktops;


    } else if (delta != INT_UNSET) {
        
        index = x11_active_desktop + delta;
        if (index < 0)
            index = 0;
        if (index >= x11_num_desktops)
            index = x11_num_desktops - 1;

    } else goto fail;

    if (!x11_set_active_desktop(index))
        exit(1);

    return args;

fail:
    usage("Exactly one of -i, -r, -d must be passed to the switch command\n");
    return NULL;
}

static char** do_move(char **args)
{
    int src = INT_UNSET;
    int dst = INT_UNSET;

    while (*args) {
        if (args[0][0] != '-') break;
        if (get_int_flag(&args, 's', &src)) {
        } else if (get_int_flag(&args, 'd', &dst)) {
        } else usage("Unrecognized option to move: %s\n", args[0]);
    }

    if (dst == INT_UNSET)
        usage("The -d option is required for the move command\n");

    if (src == INT_UNSET)
        src = x11_active_desktop;

    dst = normalize(dst);
    src = normalize(src);

    if (!x11_move_windows(src, dst))
        exit(1);
    
    return args;
}

static char** do_clear(char **args)
{
    int index = INT_UNSET;
    
    while (*args) {
        if (args[0][0] != '-') break;
        if (get_int_flag(&args, 'i', &index)) {
        } else usage("Unrecognized option to clear: %s\n", args[0]);
    }

    // TODO

    return args;
}

//////////////////////////// Arg parsing //////////////////////////////////////

static int get_flag(char ***args, char flag)
{
    if ((**args)[0] == '-' && (**args)[1] == flag && (**args)[2] == '\0') {
        (*args)++;
        return 1;
    }
    return 0;
}

static int get_str_flag(char ***args, char flag, char **val)
{
    if ((**args)[0] != '-' || (**args)[1] != flag)
        return 0;
    
    if ((**args)[2] != '\0') {
        *val = &((**args)[2]);
        (*args)++;
    } else if ((*args)[1] != NULL) {
        (*args)++;
        *val = **args;
        (*args)++;
    } else {
        usage("Missing argument to -%c\n", flag);
    }

    return 1;
}

static int get_int_flag(char ***args, char flag, int *val)
{
    char *str, *end;
    if (!get_str_flag(args, flag, &str))
        return 0;
    
    *val = strtol(str, &end, 10);
    if (*end != '\0')
        usage("Argument %s to -%c is not an integer\n", str, flag);

    return 1;
}

/*             (c) 2014 vaddr -- MIT license; see vtabs/LICENSE              */

#include "pstree.h"
#include "vtabs_x11.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

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

static const char *rcfile = "~/config/vtabsrc";

// These are externed elsewhere
int verbose   = 0;
int no_action = 0;

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
    for (; *args; args++) {
        if (args[0][0] != '-')
            break;
        
        if (!args[0][1] || args[0][2])
            usage("Unrecognized option: %s\n", args[0]);

        switch (args[0][1]) {
            case 'v': verbose = 1; break;
            case 'p': verbose = 1; no_action = 1; break;
            case 'f': {
                if (!args[1])
                    usage("-f option requires argument\n");
                rcfile = args[1];
                // When the rc file is explicitly specified, throw an error
                // if it doesn't exist. We don't do this for the default.
                if (access(rcfile, F_OK) == -1)
                    usage("Specified config doesn't exist: %s\n", rcfile);
            } break;
            default: usage("Unrecognized option: %s\n", args[0]);
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

    while (args[0]) {
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

    }

    return 0;
}

static char** do_add(char **args)
{
    return args;
}

static char** do_remove(char **args)
{
    return args;
}

static char** do_rename(char **args)
{
    return args;
}

static char** do_switch(char **args)
{
    return args;
}

static char** do_move(char **args)
{
    return args;
}

static char** do_clear(char **args)
{
    return args;
}


/*             (c) 2014 vaddr -- MIT license; see vtabs/LICENSE              */
#include "pstree.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

static pstree_node_t *pstree_do_node(int pid, pstree_node_t *root);

pstree_node_t *pstree_create(void)
{
    DIR *dirp = opendir("/proc");
    if (!dirp) {
        static int did_perror = 0;
        if (!did_perror++)
            perror("Opening /proc");
        return NULL;
    }

    pstree_node_t *root = calloc(1, sizeof(*root));
    root->pid = 1;

    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        char *endp = NULL;
        errno = 0;
        int pid = strtol(entry->d_name, &endp, 10);
        if (errno != 0 || !endp || endp[0] != '\0')
            continue;

        pstree_do_node(pid, root);
    }

    closedir(dirp);
    return root;
}

static pstree_node_t *pstree_do_node(int pid, pstree_node_t *root)
{
    pstree_node_t *rv = NULL;
    pstree_node_t *parentnode = NULL;
    int fd = -1;
    int parentpid = -1;
    int readlen = 0;
    int rv_allocated = 0;
    char *readbuf = NULL;
    char *openparen = NULL, *closeparen = NULL;
    char *c;
    char fnbuf[32];
    char *sp = fnbuf;
    
    if (root == NULL || pid < 0)
        return NULL;

    if ((rv = pstree_find(root, pid)) != NULL && rv->exec)
        return rv;

    sp += sprintf(fnbuf, "/proc/%d/", pid);
    strcpy(sp, "stat");
    if ((fd = open(fnbuf, O_RDONLY)) < 0)
        goto fail;

    // The first 4 fields of the stat file are:
    // 1. pid
    // 2. executable name, in parens
    // 3. status code (single char)
    // 4. parent pid -- this is what we want
    // All fields that follow are numeric.
    // Unfortunately, the executable name may contain spaces or parens,
    // so it is necessary to find the last instance of ')' in the file to
    // properly find the 4th field. 

    readbuf = malloc(1024);
    readlen = 0;
    while (1) {
        int n = read(fd, readbuf + readlen, 1024);
        if (n < 0)
            goto fail;
        if (n == 0)
            break;

        readlen += n;
        readbuf = realloc(readbuf, 1024 + readlen);
    }

    openparen = strchr(readbuf, '(');
    closeparen = strrchr(readbuf, ')');
    if (!openparen || !closeparen || openparen > closeparen)
        goto fail;
    if (closeparen[1] != ' ')
        goto fail;

    if (!rv) {
        rv_allocated = 1;
        rv = calloc(1, sizeof(*rv));
        rv->pid = pid;
    }
    rv->exec = malloc(closeparen - openparen);
    strncpy(rv->exec, openparen+1, closeparen - openparen - 1);
    rv->exec[closeparen - openparen - 1] = '\0';

    errno = 0;
    parentpid = strtol(closeparen+1, &c, 10);
    if (parentpid < 0 || errno != 0 || *c != ' ')
        goto fail;

    parentnode = pstree_do_node(parentpid, root);
    if (parentnode == NULL)
        goto fail;

    rv->parent = parentnode;
    if (parentnode->child)
        rv->sibling = parentnode->child;
    parentnode->child = rv;

    free(readbuf);
    return rv;

fail:
    if (rv_allocated) {
        if (rv->exec)
            free(rv->exec);
        free(rv);
    }
    if (readbuf)
        free(readbuf);
    return NULL;
}

void pstree_free(pstree_node_t *root)
{
    if (root == NULL)
        return;

    for (pstree_node_t *n = root->child, *next = NULL; n; n = next) {
        next = n->sibling;
        pstree_free(n);
    }
    free(root->exec);
    free(root);
}

pstree_node_t *pstree_find(pstree_node_t *root, int pid)
{
    if (root->pid == pid)
        return root;

    for (pstree_node_t *n = root->child; n; n = n->sibling) {
        pstree_node_t *rv = pstree_find(n, pid);
        if (rv) 
            return rv;
    }

    return NULL;
}

pstree_node_t *pstree_next_leaf(pstree_node_t *cur)
{
    if (cur == NULL)
        return NULL;

    if (cur->parent == NULL) {
        
        // Edge case: root is a leaf
        if (cur->child == NULL)
            return NULL;

        // Traverse down "left edge" to find first leaf
        while (cur->child)
            cur = cur->child;

        return cur;
    }
    
    // Handle upwards part of walk to find next lateral move
    while (!cur->sibling) {
        cur = cur->parent;
        if (!cur)
            return NULL;
    }

    // Do lateral move
    cur = cur->sibling;

    // Traverse down "left edge" to find leaf
    while (cur->child)
        cur = cur->child;
    
    return cur;
}

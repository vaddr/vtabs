/*             (c) 2014 vaddr -- MIT license; see vtabs/LICENSE              */
#ifndef PSTREE_H
#define PSTREE_H

typedef struct pstree_node_t {
    int   pid;      // pid of a process
    char *exec;     // path to the executable for this pid
    struct pstree_node_t *parent;   // Parent of pid (NULL for proper root)
    struct pstree_node_t *child;    // First child of pid (beware races)
    struct pstree_node_t *sibling;  // Next child
} pstree_node_t;

// Create a tree of all processes.
// Since there is no way to see only the children of a process, there is no
// benefit to creating a limited tree. 
// Creating a process tree is inherently subject to race conditions, since 
// the /proc tree cannot be read atomically.
pstree_node_t *pstree_create(void);

// Free memory associated with a process tree. Always pass the proper root of
// the tree, not a subtree.
void pstree_free(pstree_node_t *root);

// Locate a root node within the given tree or subtree. 
pstree_node_t *pstree_find(pstree_node_t *root, int pid);

// Find the next leaf node by depth-first traversal. Pass in root to get the
// first leaf node.
pstree_node_t *pstree_next_leaf(pstree_node_t *cur);

#endif

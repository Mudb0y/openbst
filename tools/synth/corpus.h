#ifndef BST_TOOL_CORPUS_H
#define BST_TOOL_CORPUS_H

#include "bst.h"

/* The reads a build is put through when its tables are being worked out.
   Shared so that the inventory measures the same reads the lift keeps, and
   a change to one cannot silently stop matching the other. */
void bst_corpus_sweep(bst *h);
void bst_corpus_file(bst *h, const char *path);

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Prints bytes from a loaded, relocated 16-bit module, so a candidate table
   address can be looked at as the engine would see it rather than as the file
   holds it. */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: nedump DLL [--gaps] [ADDR[,LEN]...]\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    if (!d || fread(d, 1, n, f) != (size_t)n) return 1;
    fclose(f);

    bst_image img;
    if (bst_image_init_ne(&img, d, n, &BST_MAP_1998_ENG) < 0) {
        fprintf(stderr, "not a 16-bit module\n");
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--gaps")) {
            const char *g[128];
            int n = bst_map_gaps(&img.t, g, 128);
            printf("%d entries not yet settled:", n);
            for (int k = 0; k < n; k++) printf(" %s", g[k]);
            printf("\n");
            continue;
        }
        unsigned addr = 0, len = 32;
        sscanf(argv[i], "%x,%u", &addr, &len);
        const uint8_t *p = bst_at(&img, addr, len);
        printf("%08x ", addr);
        if (!p) { printf("unmapped\n"); continue; }
        for (unsigned k = 0; k < len; k++) printf(" %02x", p[k]);
        printf("  |");
        for (unsigned k = 0; k < len; k++)
            printf("%c", p[k] >= 0x20 && p[k] < 0x7f ? p[k] : '.');
        printf("|\n");
    }
    bst_image_free(&img);
    free(d);
    return 0;
}

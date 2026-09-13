#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Assembles a captured token sequence into a phrase stream.

   Input is one line per token, "K <kind> <emph> <mode> <hex buffer>", and a
   blank line starts a new phrase. Output is one hex line per phrase. */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: asmtest DLL < tokens\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    if (!d || fread(d, 1, n, f) != (size_t)n) return 1;
    fclose(f);
    bst_image img;
    bst_image_init(&img, d, n);

    static uint8_t stream[0x200];
    bst_assembler z;
    bst_assemble_init(&z, &img, stream);
    memset(stream, 0, sizeof stream);
    bst_assemble_start(&z);

    char line[4096];
    uint8_t buf[256];
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] != 'K') continue;
        int kind = 0, emph = 0, mode = 0, used = 0;
        if (sscanf(line + 1, "%d %d %d %n", &kind, &emph, &mode, &used) < 3) continue;
        const char *p = line + 1 + used;
        int k = 0;
        memset(buf, 0, sizeof buf);
        while (k < (int)sizeof buf) {
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            buf[k++] = (uint8_t)v;
            p += 2;
            while (*p == ' ') p++;
        }
        z.emph = emph;
        z.mode = mode;
        if (bst_assemble_token(&z, kind, buf)) {
            int len = z.len > 0 ? z.len : z.wp + 1;
            for (int i = 0; i < len; i++) printf("%s%02X", i ? " " : "", stream[i]);
            putchar('\n');
            memset(stream, 0, sizeof stream);
            bst_assemble_start(&z);
        }
    }
    free(d);
    return 0;
}

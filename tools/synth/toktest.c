#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_token.h"

/* A word token arrives as text and is pronounced before it is dispatched,
   which is what the engine does between the scanner and the assembler. */

/* Tokenises each line of stdin and prints "<kind> <hex buffer>" per token. */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: toktest DLL < text\n"); return 2; }
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

    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        bst_tok t;
        bst_tok_init(&t, &img, line);
        uint8_t buf[128];
        for (int guard = 0; guard < 512; guard++) {
            memset(buf, 0, sizeof buf);
            int kind = bst_tok_next(&t, buf);
            if (kind == 2) {
                bst_recs r;
                bst_stream st;
                bst_word w;
                char word[128];
                int n3 = 0;
                for (const uint8_t *q = buf + 1; *q && n3 < 120; q++) word[n3++] = (char)*q;
                word[n3] = 0;
                bst_normalise(&img, word, &w);
                bst_word_pronounce(&img, word, &r, &st);
                buf[0] = (uint8_t)(st.len > 1 ? st.len - 2 : 0);
                memcpy(buf + 1, st.buf, (size_t)st.len);
                kind = 3;
            }
            printf("%d", kind);
            int n2 = kind == 4 ? 1 : (kind == 3 ? buf[0] + 2 : 6);
            for (int i = 0; i < n2; i++) printf(" %02X", buf[i]);
            putchar('\n');
            if (kind == 6) break;
        }
    }
    free(d);
    return 0;
}

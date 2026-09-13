#include <string.h>
#include "bst_text.h"

/* Section mapping for the 1995 build. The tables are read from the image the
   caller supplies; nothing is copied into the library. */
#define RDATA_VA   0x10020000u
#define RDATA_OFF  0x17800u
#define LETTER_ATTR 0x10021120u

#define A_VOWEL   1
#define A_VOICED  2
#define A_CONS    4
#define A_DOUBLE  8

int bst_image_init(bst_image *img, const void *data, size_t len) {
    if (!data || len < 0x20000) return -1;
    const uint8_t *d = data;
    img->image = d;
    img->len = len;
    img->nsec = 0;
    img->base = 0;

    uint32_t pe = (uint32_t)(d[0x3C] | (d[0x3D] << 8) | (d[0x3E] << 16) | (d[0x3F] << 24));
    if (pe + 0x100 >= len || d[pe] != 'P' || d[pe + 1] != 'E') return 0;
    int nsec = d[pe + 6] | (d[pe + 7] << 8);
    int optsz = d[pe + 20] | (d[pe + 21] << 8);
    const uint8_t *o = d + pe + 24 + 28;
    img->base = (uint32_t)(o[0] | (o[1] << 8) | (o[2] << 16) | (o[3] << 24));
    if (nsec > BST_SECTIONS) nsec = BST_SECTIONS;
    for (int i = 0; i < nsec; i++) {
        const uint8_t *h = d + pe + 24 + optsz + i * 40 + 8;
        uint32_t v[4];
        for (int k = 0; k < 4; k++)
            v[k] = (uint32_t)(h[k * 4] | (h[k * 4 + 1] << 8) |
                              (h[k * 4 + 2] << 16) | (h[k * 4 + 3] << 24));
        img->sec[img->nsec].vsize   = v[0];
        img->sec[img->nsec].va      = v[1];
        img->sec[img->nsec].rawsize = v[2];
        img->sec[img->nsec].raw     = v[3];
        img->nsec++;
    }
    return 0;
}

const uint8_t *bst_at(const bst_image *img, uint32_t va, size_t need) {
    if (!img->nsec) return NULL;
    uint32_t rva = va - img->base;
    for (int i = 0; i < img->nsec; i++) {
        uint32_t sz = img->sec[i].vsize > img->sec[i].rawsize
                    ? img->sec[i].vsize : img->sec[i].rawsize;
        if (rva < img->sec[i].va || rva >= img->sec[i].va + sz) continue;
        size_t off = img->sec[i].raw + (rva - img->sec[i].va);
        if (off + need > img->len) return NULL;
        return img->image + off;
    }
    return NULL;
}

static int attr(const bst_image *img, int c) {
    if (c < 0 || c > 255) return 0;
    size_t off = RDATA_OFF + (LETTER_ATTR - RDATA_VA) + (unsigned)c;
    return off < img->len ? img->image[off] : 0;
}

/* Decides whether a stem ending at i wants its silent e back. */
static int wants_e(const bst_image *img, const char *w, int i) {
    int c = i >= 0 ? (unsigned char)w[i] : 0;
    int p = i >= 1 ? (unsigned char)w[i - 1] : 0;
    if (attr(img, c) & A_CONS) return 0;
    if (!(attr(img, c) & A_VOWEL) || !(attr(img, p) & A_VOWEL)) return 1;
    if (p == 'u' && i >= 2 && (w[i - 2] == 'q' || w[i - 2] == 'g')) return 1;
    return 0;
}

/* The stem check before -ed and -ing come off. Returns the new last index, or
   -1 to refuse the strip. */
static int stem(const bst_image *img, char *w, int i, int vowels) {
    if (i < 0) return -1;
    int c = (unsigned char)w[i];
    int p = i >= 1 ? (unsigned char)w[i - 1] : 0;
    int add_e = 0;

    if (p == c && (attr(img, c) & A_DOUBLE)) return i - 1;

    switch (c) {
    case 'a': case 'e': case 'o': case 'x': case 'y':
        break;
    case 'b':
        if (strchr("aeiouy", p)) add_e = 1;
        else if (p == 'l' || p == 'm' || p == 'r') break;
        else return -1;
        break;
    case 'c': case 'u': case 'v':
        add_e = 1;
        break;
    case 'g':
        if (!(p == 'n' && i >= 2 && (w[i - 2] == 'i' || w[i - 2] == 'o'))) add_e = 1;
        break;
    case 'h':
        if (p == 't') add_e = 1;
        break;
    case 'i':
        w[i] = 'y';
        break;
    case 'l':
        if (p == 'a' || p == 'e') { if (vowels <= 2) add_e = 1; }
        else if (strchr("bcdfghjkmnpqstvxz", p)) return -1;
        else add_e = wants_e(img, w, i - 1);
        break;
    case 'r':
        if (attr(img, p) & A_CONS) return -1;
        if (vowels > 2) {
            if (p == 'a') {
                if ((i >= 2 && w[i - 2] == 'p') ||
                    (i >= 3 && w[i - 2] == 'l' && w[i - 3] == 'c')) add_e = 1;
            } else if (p != 'e' && p != 'o') {
                add_e = wants_e(img, w, i - 1);
            }
        } else add_e = 1;
        break;
    case 's':
        if (p == 's') break;
        if (p == 'u' && i >= 2 && w[i - 2] == 'o') return -1;
        add_e = 1;
        break;
    case 't':
        if (p == 'a') {
            if (!(i >= 2 && (w[i - 2] == 'o' || w[i - 2] == 'e'))) add_e = 1;
        } else if (p == 'e') {
            if (vowels <= 2) add_e = 1;
        } else if (p == 'i') {
            if (!(vowels > 2 && (i < 2 || w[i - 2] != 'v') &&
                  !(i >= 3 && w[i - 2] == 'r' && w[i - 3] == 'w'))) add_e = 1;
        } else add_e = wants_e(img, w, i - 1);
        break;
    case 'w':
        if (attr(img, p) & A_CONS) return -1;
        break;
    case 'z':
        if (attr(img, p) & A_VOWEL) add_e = 1;
        break;
    default:
        if (!wants_e(img, w, i - 1)) return i;
        add_e = 1;
        if (vowels > 2) {
            if (c == 'm') {
                if (p == 'o' && (i < 2 || w[i - 2] != 'c')) add_e = 0;
            } else if (c == 'n') {
                if (p == 'a' || p == 'o') add_e = 0;
                else if (p == 'e' &&
                         !(i >= 3 && w[i - 2] == 'v' && (attr(img, (unsigned char)w[i - 3]) & A_CONS)))
                    add_e = 0;
            } else if (c == 'p' && (p == 'i' || p == 'o')) {
                add_e = 0;
            }
        }
        break;
    }

    if (add_e) {
        w[i + 1] = 'e';
        return i + 1;
    }
    return i;
}

void bst_normalise(const bst_image *img, const char *word, bst_word *out) {
    char w[BST_WORD_MAX];
    int n = 0;

    memset(out, 0, sizeof *out);
    for (; word[n] && n < BST_WORD_MAX - 4; n++) {
        char c = word[n];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        w[n] = c == '\'' ? '`' : c;
    }
    w[n] = 0;

    int vowels = 0;
    for (int i = 0; i < n; i++)
        if (strchr("aeiouy", w[i])) vowels++;

    int end = n - 1, flags = 0;

    for (;;) {
        int again = 0;
        int c = end >= 0 ? (unsigned char)w[end] : 0;

        if (c == 'd') {
            if (end >= 1 && w[end - 1] == 'e' && vowels > 1 &&
                (end < 2 || w[end - 2] != 'e')) {
                int k = stem(img, w, end - 2, vowels);
                if (k >= 0) { flags |= BST_SUF_ED; end = k; }
            }
        } else if (c == 'g') {
            if (end >= 2 && w[end - 1] == 'n' && w[end - 2] == 'i' && vowels > 1) {
                int k = stem(img, w, end - 3, vowels);
                if (k >= 0) { flags |= BST_SUF_ING; end = k; }
            }
        } else if (c == 's') {
            if (vowels != 0) {
                int prev = end >= 1 ? (unsigned char)w[end - 1] : 0;
                int take = 1, keep = end - 1;
                if (prev == '`') {
                    flags |= BST_SUF_APOS;
                    end -= 2;
                    again = 1;
                    take = 0;
                } else if (prev == 'i' || prev == 's' || prev == 'u') {
                    take = 0;
                } else if (prev == 'a' || prev == 'o' || prev == 'y') {
                    if (vowels == 1) take = 0;
                } else if (prev == 'd' || prev == 'g') {
                    again = 1;
                } else if (prev == 'e') {
                    if (vowels < 2) take = 0;
                    else {
                        int p2 = end >= 2 ? (unsigned char)w[end - 2] : 0;
                        int p3 = end >= 3 ? (unsigned char)w[end - 3] : 0;
                        if (p2 == 'h')      keep = (p3 == 's' || p3 == 'c') ? end - 2 : end - 1;
                        else if (p2 == 'i') { vowels--; again = 1; w[end - 2] = 'y'; keep = end - 2; }
                        else if (p2 == 'j' || p2 == 'o' || p2 == 'x') keep = end - 2;
                        else if (p2 == 's') {
                            keep = end - 1;
                            if (p3 == 'u' && vowels > 2) {
                                keep = end - 2;
                                if (end >= 4 && w[end - 4] == 'o') take = 0;
                            }
                        } else if (p2 == 'z') {
                            keep = (attr(img, p3) & A_CONS) ? end - 2 : end - 1;
                        } else keep = end - 1;
                    }
                }
                if (take) { flags |= BST_SUF_S; end = keep; }
            }
        } else if (c == 'y') {
            if (end >= 1 && w[end - 1] == 'l' && vowels > 1) {
                int p = end >= 2 ? (unsigned char)w[end - 2] : 0;
                int take = 1, keep = end - 2;
                if (p == 'a' || p == 'b' || p == 'o' || p == 'u') take = 0;
                else if (p == 'd' || p == 'g') { vowels--; again = 1; }
                else if (p == 'e') take = vowels >= 3;
                else if (p == 'i') {
                    if (vowels < 3 || (end >= 4 && w[end - 3] == 'r' && w[end - 4] == 'a'))
                        take = 0;
                    else { w[end - 2] = 'y'; out->y_from_i = 1; }
                } else if (p == 'l') {
                    if (vowels < 3) take = 0;
                    else {
                        int b = end >= 3 ? (unsigned char)w[end - 3] : 0;
                        if (b == 'a') {
                            if (end >= 5 && w[end - 4] == 'c' && w[end - 5] == 'i') keep = end - 4;
                        } else if (b != 'u' &&
                                   !(attr(img, end >= 4 ? (unsigned char)w[end - 4] : 0) & A_VOWEL)) {
                            take = 0;
                        }
                    }
                }
                if (take) { flags |= BST_SUF_LY; end = keep; }
            }
        }

        if (!again) break;
    }

    out->buf[0] = '_';
    int k = 0;
    for (; k <= end && k + 1 < BST_WORD_MAX - 3; k++) out->buf[k + 1] = w[k];
    out->buf[k + 1] = '_';
    out->buf[k + 2] = '_';
    out->buf[k + 3] = 0;
    out->len = k + 3;
    out->flags = flags;
    out->vowels = vowels;
}

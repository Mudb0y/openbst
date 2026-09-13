#include "bst_text.h"

/* Diphone expansion: an allophone index to the acoustic targets behind it.
 *
 * The inventory is indexed by the previous sound times forty-eight plus the
 * current one, so every sound is stored in the context of the one before it,
 * and that is where the engine's coarticulation comes from. An entry holds one
 * sub-sequence per position the sound covers; records are variable length,
 * chosen by bits four to six of the first byte, and bit seven ends a
 * sub-sequence. The three-byte forms carry one nine-bit target index and the
 * five-byte form carries two, naming both ends of a glide. */

#define RDATA_VA    0x10020000u
#define RDATA_OFF   0x17800u
#define RECORDS     0x100292A0u
#define OFFSETS     0x1002D508u
#define OFFSETS_END 0x1002E818u
#define VOICES      0x10022284u
#define VOICE_SPAN  0x19Au        /* targets per voice */

static const int RECLEN[8] = { 1, 3, 1, 3, 3, 5, 1, 1 };

static size_t off(unsigned va) { return RDATA_OFF + (va - RDATA_VA); }

int bst_diphone_count(const bst_image *img) {
    (void)img;
    return (int)((off(OFFSETS_END) - off(OFFSETS)) / 2);
}

int bst_diphone(const bst_image *img, int index, int positions,
                uint16_t *out, int max) {
    int n = bst_diphone_count(img);
    if (index < 0 || index >= n) return 0;
    size_t t = off(OFFSETS) + (size_t)index * 2;
    if (t + 1 >= img->len) return 0;
    unsigned entry = (unsigned)(img->image[t] | (img->image[t + 1] << 8));
    if (!entry) return 0;

    size_t p = off(RECORDS) + entry;
    int k = 0;
    for (int g = 0; g < positions; g++) {
        /* A sub-sequence is at most a couple of dozen records; the bound is
           only there so a corrupt entry cannot run off the section. */
        for (int guard = 0; guard < 32; guard++) {
            if (p >= img->len) return k;
            int b = img->image[p];
            int type = (b & 0x70) >> 4;
            int len = RECLEN[type];
            if (p + (size_t)len > img->len) return k;
            if (type == 1 || type == 3 || type == 4 || type == 5) {
                if (k < max) out[k] = (uint16_t)(((img->image[p + 2] << 8) |
                                                  img->image[p + 1]) & 0x1FF);
                k++;
            }
            if (type == 5) {
                if (k < max) out[k] = (uint16_t)(((img->image[p + 4] << 8) |
                                                  img->image[p + 3]) & 0x1FF);
                k++;
            }
            p += (size_t)len;
            if (b & 0x80) break;
        }
    }
    return k > max ? max : k;
}

/* The ten reflection coefficients a target names, in the Q8 form the lattice
   wants. */
void bst_target_coeffs(const bst_image *img, int voice, int target, int16_t k[10]) {
    size_t base = off(VOICES) + (size_t)(voice * (int)VOICE_SPAN + target) * 10;
    for (int i = 0; i < 10; i++)
        k[i] = (base + (size_t)i < img->len)
             ? (int16_t)((int8_t)img->image[base + i] * 2) : 0;
}

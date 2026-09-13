#include <string.h>
#include "bst_synth.h"

/* Both lookups clamp the same way the engine's do: an index outside 0..255
   yields 255 when positive and 0 when negative. */
static int lut(const int16_t *t, int x) {
    if (x >= 0 && x <= 255) return t[x];
    return x >= 0 ? 255 : 0;
}

void bst_interp_init(bst_interp *ip, const bst_tables *t) {
    memset(ip, 0, sizeof *ip);
    ip->t = t;
}

void bst_interp_snap(bst_interp *ip, const int16_t target[BST_ORDER]) {
    memcpy(ip->state, target, sizeof ip->state);
}

void bst_interp_step(bst_interp *ip, const int16_t target[BST_ORDER],
                     int dur, int remaining) {
    /* rate = log(dur) - log(scaled remaining) - four octaves. Adding it to
       log(distance/2) and taking the antilog gives the step. */
    int idx = (((remaining - (remaining >> 2)) + dur) >> 5) + 1;
    int rate = lut(ip->t->log, dur) - lut(ip->t->log, idx) - 0x80;

    for (int i = 0; i < BST_ORDER; i++) {
        int diff = target[i] - ip->state[i];
        if (diff < 0)
            ip->state[i] -= (int16_t)lut(ip->t->alog, lut(ip->t->log, (-diff) >> 1) + rate);
        else
            ip->state[i] += (int16_t)lut(ip->t->alog, lut(ip->t->log, diff >> 1) + rate);
    }
}

int bst_transition_len(const int16_t from[BST_ORDER], const int16_t to[BST_ORDER]) {
    int sum = 0;
    for (int i = 0; i < BST_ORDER; i++) {
        int d = from[i] - to[i];
        sum += d < 0 ? -d : d;
    }
    return (sum >> 2) + (sum >> 3);
}

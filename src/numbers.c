#include <string.h>
#include "bst_token.h"

/* Saying a number.
 *
 * Up to four digits a number is read as speech rather than spelled: one digit
 * is its own name, two are a tens name and a units name with the teens as a
 * special case, three are a hundreds group, and four are either two pairs --
 * which is how a year comes out -- or a single group of thousands when the
 * last three digits are zeros. Anything longer, or anything with a leading
 * zero, is spelled digit by digit.
 *
 * Every name is a stored string of phoneme codes, and the tables are indexed
 * by the digit's character code rather than its value. */

/* Each name is a stored string, named in the build's table directory. */
#define STR(t, n) ((t)->img->t.s[n])

static void one(bst_tok *t, int c)  { bst_tok_say(t, STR(t, BST_S_DIGITS) + (unsigned)c * 4); }
static void teen(bst_tok *t, int c) { bst_tok_say(t, STR(t, BST_S_TEENS)  + (unsigned)c * 4); }
static void ten(bst_tok *t, int c)  { bst_tok_say(t, STR(t, BST_S_TENS)   + (unsigned)c * 4); }

/* German and Dutch say the units first, joined to the tens by "und" or "en",
   and the unit one takes a shorter form there than it does alone. */
static void two_germanic(bst_tok *t, const uint8_t *d) {
    if (d[0] == '0') {
        if (d[1] != '0') one(t, d[1]);
        return;
    }
    if (d[0] == '1') { teen(t, d[1]); return; }
    if (d[1] != '0') {
        if (d[1] == '1') bst_tok_say(t, STR(t, BST_S_ONE_ALT));
        else             one(t, d[1]);
        bst_tok_say(t, STR(t, BST_S_AND));
    }
    ten(t, d[0]);
}

/* French counts the seventies off the sixty word and the nineties off the
   eighty one, both finishing on a teen, and joins a unit one with "et" except
   after eighty. */
static void two_french(bst_tok *t, const uint8_t *d) {
    if (d[0] == '0') { one(t, d[1]); return; }
    if (d[0] == '1') { teen(t, d[1]); return; }
    if (d[0] == '7' || d[0] == '9') {
        ten(t, d[0] == '7' ? '6' : '8');
        if (d[0] == '7' && d[1] == '1') bst_tok_say(t, STR(t, BST_S_AND));
        teen(t, d[1]);
        return;
    }
    ten(t, d[0]);
    if (d[1] == '1') { if (d[0] != '8') bst_tok_say(t, STR(t, BST_S_AND)); }
    else if (d[1] == '0') return;
    one(t, d[1]);
}

static void two(bst_tok *t, const uint8_t *d) {
    if (t->img->t.num_two_kind == 1) { two_germanic(t, d); return; }
    if (t->img->t.num_two_kind == 3) { two_french(t, d); return; }
    if (d[0] == '0') {
        if (d[1] == '0') return;
        /* Russian and the Romance builds have no word for the empty tens
           place, where English says "oh". */
        if (t->img->t.num_two_kind != 2) bst_tok_say(t, STR(t, BST_S_OH));
        one(t, d[1]);
        return;
    }
    if (d[0] == '1') { teen(t, d[1]); return; }
    if (t->img->t.num_two_kind == 2) {
        /* The twenties take a form of their own and join the unit straight
           on; the rest join it with the build's own word for "and". */
        if (d[0] == '2' && d[1] != '0') { ten(t, '0'); one(t, d[1]); return; }
        ten(t, d[0]);
        if (d[1] != '0') {
            if (STR(t, BST_S_AND)) bst_tok_say(t, STR(t, BST_S_AND));
            one(t, d[1]);
        }
        return;
    }
    ten(t, d[0]);
    if (d[1] != '0') one(t, d[1]);
}

/* Groups of three, most significant first, each followed by its scale. */
static void groups(bst_tok *t, const uint8_t *d, int n) {
    int all_zero = (n == 2 && d[0] == '0' && d[1] == '0');
    int empty = 0, said_scale = 0, first = 1;
    for (;;) {
        n--;
        if (n < 0) return;
        /* Russian keeps a word of its own for each hundreds place, and puts a
           comma between groups so the pause falls there. */
        if (t->img->t.num_group_kind == 1 && !first && !all_zero &&
            !(d[0] == '0' && d[1] == '0' && d[2] == '0'))
            bst_tok_put(t, ',');
        first = 0;
        if (d[0] == '0') empty = 1;
        else if (t->img->t.num_group_kind == 1)
            bst_tok_say(t, STR(t, BST_S_HUNDREDS) + (unsigned)d[0] * 4);
        else {
            /* German says the short one before its hundred word and Dutch
               says no one at all, where English says "one hundred". */
            int k = t->img->t.num_group_kind;
            if (d[0] == '1' && k == 2)      bst_tok_say(t, STR(t, BST_S_ONE_ALT));
            else if (d[0] != '1' || k != 3) one(t, d[0]);
            bst_tok_say(t, STR(t, BST_S_HUNDRED));
        }
        if (d[1] == '0') {
            if (d[2] != '0') { one(t, d[2]); empty = 0; }
        } else {
            two(t, d + 1);
            empty = 0;
        }
        d += 3;
        if (*d < '0' || *d > '9') d++;    /* step over a group separator */
        if (empty) {
            empty = 0;
            if (n == 0 && !said_scale) bst_tok_say(t, STR(t, BST_S_ZERO));
        } else {
            bst_tok_say(t, STR(t, BST_S_SCALES) + (unsigned)n * 4);
            said_scale = 1;
        }
        (void)all_zero;
    }
}

/* Spelled out, some builds break a run longer than seven into fives with a
   comma, so the pause falls where the eye would put it. */
static void spell(bst_tok *t, const uint8_t *d, int n) {
    int split = t->img->t.num_spell_fives && n > 7;
    for (int i = 0, left = n - 1; i < n; i++, left--) {
        one(t, d[i]);
        if (split && left != 0 && left % 5 == 0) bst_tok_put(t, ',');
    }
}

void bst_say_digits(bst_tok *t, const uint8_t *d, int n) { spell(t, d, n); }

/* A number written in groups: the digits are padded out to a whole number of
   threes and then read group by group, each followed by its scale. */
void bst_say_grouped(bst_tok *t, const uint8_t *d, int n) {
    uint8_t buf[32];
    if (n <= 0 || n > 24) { spell(t, d, n); return; }
    int g = (n + 2) / 3;
    int pad = g * 3 - n;
    for (int i = 0; i < pad; i++) buf[i] = '0';
    memcpy(buf + pad, d, (size_t)n);
    buf[g * 3] = 0;
    groups(t, buf, g);
}

/* Russian splits by how many digits there are rather than by whether the
   number is short: four and five digits are padded out to two whole groups
   instead of being read as a year. */
static void say_number_russian(bst_tok *t, const uint8_t *d, int n) {
    uint8_t pad[10];
    switch (n) {
    case 1: one(t, d[0]); return;
    case 2: two(t, d); return;
    case 3: groups(t, d, 1); return;
    case 4:
        pad[0] = '0'; pad[1] = '0'; pad[2] = d[0]; pad[3] = ',';
        memcpy(pad + 4, d + 1, 3);
        pad[7] = 0;
        groups(t, pad, 2);
        return;
    case 5:
        pad[0] = '0';
        memcpy(pad + 1, d, 5);
        pad[6] = 0;
        groups(t, pad, 2);
        return;
    case 6: groups(t, d, 2); return;
    default: spell(t, d, n); return;
    }
}

/* German reads four digits as hundreds only when the thousands place is a one
   or nothing and the hundreds place is not a zero; Dutch asks only about the
   hundreds place. Anything else is padded out to two whole groups. */
static void say_number_german(bst_tok *t, const uint8_t *d, int n) {
    uint8_t pad[10];
    if (n > 4 || d[0] == '0') { spell(t, d, n); return; }
    switch (n) {
    case 1: one(t, d[0]); return;
    case 2: two(t, d); return;
    case 3: groups(t, d, 1); return;
    default:
        if (d[1] != '0' && (d[0] <= '1' || t->img->t.num_kind == 3)) {
            two(t, d);
            bst_tok_say(t, STR(t, BST_S_HUNDRED));
            two(t, d + 2);
            return;
        }
        pad[0] = '0'; pad[1] = '0'; pad[2] = d[0]; pad[3] = ',';
        memcpy(pad + 4, d + 1, 3);
        pad[7] = 0;
        groups(t, pad, 2);
        return;
    }
}

/* French reads up to four digits and spells anything longer, and its four are
   two whole groups rather than the year English makes of them. */
static void say_number_french(bst_tok *t, const uint8_t *d, int n) {
    uint8_t pad[10];
    if (n > 4 || d[0] == '0') { spell(t, d, n); return; }
    switch (n) {
    case 1: one(t, d[0]); return;
    case 2: two(t, d); return;
    case 3: groups(t, d, 1); return;
    default:
        pad[0] = '0'; pad[1] = '0'; pad[2] = d[0]; pad[3] = ',';
        memcpy(pad + 4, d + 1, 3);
        pad[7] = 0;
        groups(t, pad, 2);
        return;
    }
}

void bst_say_number(bst_tok *t, const uint8_t *d, int n) {
    if (n < 1) return;
    if (t->img->t.num_kind == 4) { say_number_french(t, d, n); return; }
    if (t->img->t.num_kind == 1) { say_number_russian(t, d, n); return; }
    if (t->img->t.num_kind == 2 || t->img->t.num_kind == 3)
        { say_number_german(t, d, n); return; }
    if (n < 5 && n > 0 && d[0] != '0') {
        uint8_t pad[8];
        switch (n) {
        case 1: one(t, d[0]); return;
        case 2: two(t, d); return;
        case 3: groups(t, d, 1); return;
        case 4:
            if (d[1] == '0' && d[2] == '0' && d[3] == '0') {
                pad[0] = '0'; pad[1] = '0'; pad[2] = d[0]; pad[3] = ',';
                memcpy(pad + 4, d + 1, 3);
                pad[7] = 0;
                groups(t, pad, 2);
                return;
            }
            two(t, d);
            if (d[2] == '0' && d[3] == '0') bst_tok_say(t, STR(t, BST_S_HUNDRED));
            else two(t, d + 2);
            return;
        default: break;
        }
    }
    spell(t, d, n);
}

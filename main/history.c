#include "history.h"
#include <string.h>

static size_t utf8_span(const unsigned char *s, size_t len) {
    // Longest prefix of s, at most len bytes, that ends on a code point boundary.
    size_t cut = len;
    while (cut && (s[cut] & 0xC0) == 0x80) --cut;    // back up to a lead byte
    if (!cut) return 0;
    unsigned char lead = s[cut];
    size_t need = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
    return (need && cut+need <= len) ? cut+need : cut;
}
size_t history_append(char *dst, size_t cap, const char *src) {
    size_t len = strlen(dst);
    if (!src || cap < 2 || len+1 >= cap) return len;
    size_t room = cap-1-len, n = strlen(src);
    if (n > room) n = utf8_span((const unsigned char *)src, room);
    memcpy(dst+len, src, n);
    dst[len+n] = 0;
    return len+n;
}
// Transcripts come from the service; flatten control characters so one turn
// cannot forge extra recap lines.
static void append_clean(char *dst, size_t cap, const char *src) {
    size_t start = strlen(dst), end = history_append(dst, cap, src);
    for (size_t i = start; i < end; ++i) {
        unsigned char c = (unsigned char)dst[i];
        if (c < 0x20 || c == 0x7F) dst[i] = ' ';
    }
}
void history_init(history_t *h, size_t keep) {
    memset(h, 0, sizeof *h);
    h->keep = keep > HISTORY_TURNS ? HISTORY_TURNS : keep;
}
void history_clear(history_t *h) { h->count = 0; memset(h->turn, 0, sizeof h->turn); }
void history_begin_turn(history_t *h) {
    if (!h->keep) return;
    if (h->count == h->keep) {
        memmove(h->turn, h->turn+1, (h->keep-1)*sizeof h->turn[0]);
        --h->count;
    }
    memset(&h->turn[h->count], 0, sizeof h->turn[0]);
    ++h->count;
}
void history_add_user(history_t *h, const char *text) {
    if (h->count) append_clean(h->turn[h->count-1].user, HISTORY_TEXT, text);
}
void history_add_reply(history_t *h, const char *text) {
    if (h->count) append_clean(h->turn[h->count-1].reply, HISTORY_TEXT, text);
}
bool history_empty(const history_t *h) {
    for (size_t i = 0; i < h->count; ++i)
        if (h->turn[i].user[0] || h->turn[i].reply[0]) return false;
    return true;
}
size_t history_recap(const history_t *h, char *out, size_t cap) {
    if (!cap) return 0;
    out[0] = 0;
    if (history_empty(h)) return 0;
    history_append(out, cap, "[Baglanti yenilendi. Onceki konusmanin ozeti, sesli tekrarlama:]");
    for (size_t i = 0; i < h->count; ++i) {
        if (h->turn[i].user[0]) {
            history_append(out, cap, "\nKullanici: ");
            history_append(out, cap, h->turn[i].user);
        }
        if (h->turn[i].reply[0]) {
            history_append(out, cap, "\nCeko: ");
            history_append(out, cap, h->turn[i].reply);
        }
    }
    return strlen(out);
}

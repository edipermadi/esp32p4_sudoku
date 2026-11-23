/**
 * @file search_core.c
 * @ingroup search_core
 * @brief The Puzzles screen's search: matching (search_core.h).
 */
#include "search_core.h"
#include <ctype.h>
#include <string.h>

/**
 * @brief Is a word (len characters) somewhere in text, ignoring case?
 * @param[in] text  where to look
 * @param[in] word  the word
 * @param[in] len   its length
 * @return true if so
 */
static bool has_word(const char *text, const char *word, size_t len)
{
    for (const char *t = text; *t; t++) {
        size_t k = 0;
        while (k < len && t[k] && tolower((unsigned char)t[k]) == tolower((unsigned char)word[k])) {
            k++;
        }
        if (k == len) {
            return true;
        }
    }
    return false;
}

/* (documented in search_core.h) */
bool is_search_match(const char *query, const char *text)
{
    bool any = false;
    for (const char *q = query; *q;) {
        while (*q == ' ') {
            q++;
        }
        size_t len = 0;
        while (q[len] && q[len] != ' ') {
            len++;
        }
        if (!len) {
            break;
        }
        if (!has_word(text, q, len)) {
            return false;
        }
        any = true;
        q += len;
    }
    return any;
}

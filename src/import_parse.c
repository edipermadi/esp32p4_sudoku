/**
 * @file import_parse.c
 * @ingroup import_parse
 * @brief Reading puzzle files to import, without the database: a puzzle per line (.sdm, .txt) or OpenSudoku's
 *        XML (the .sdx format: sdx.c).
 */
#include "import_parse.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* (documented in import_parse.h) */
bool import_parse_cells(const char *p, uint8_t out[81])
{
    for (int i = 0; i < 81; i++) {
        char c = p[i];
        if (c >= '1' && c <= '9') {
            out[i] = (uint8_t)(c - '0');
        } else if (c == '0' || c == '.') {
            out[i] = 0;
        } else {
            return false;
        }
    }
    char after = p[81];
    return !(after >= '0' && after <= '9') && after != '.';
}

/* (documented in import_parse.h) */
int import_parse_file(const char *path, bool xml, uint8_t (*cells)[81], int max)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    static char line[512];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        if (xml) {
            for (const char *q = line; n < max && (q = strstr(q, "data=")) != NULL; q += 5) {
                if ((q[5] == '"' || q[5] == '\'') && strlen(q + 6) >= 82 && import_parse_cells(q + 6, cells[n])) {
                    n++;
                }
            }
        } else {
            const char *q = line;
            while (*q == ' ' || *q == '\t') {
                q++;
            }
            if (strlen(q) >= 81 && import_parse_cells(q, cells[n])) {
                n++;
            }
        }
    }
    fclose(f);
    return n;
}

/* (documented in import_parse.h) */
bool is_import_name_ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

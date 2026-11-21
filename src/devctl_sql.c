/**
 * @file devctl_sql.c
 * @ingroup devctl
 * @brief sql: SQL on sudoku.db, its result sent back (compressed with z).
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "stats_db.h"
#include <miniz.h>

#define SQL_OUT      (256 * 1024)                   /**< sql: the most it returns (the rest: "truncated") */
#define SQL_MAX (64 * 1024)                         /**< SQL, inflated: the most */

/* (documented in devctl_internal.h) */
void devctl_sql(const char *line)
{
    const char *p = line + 3;
    while (*p == ' ') {
        p++;
    }
    bool z = p[0] == 'z' && p[1] == ' ';
    long raw = 0;
    if (z) {
        char *end;
        raw = strtol(p + 2, &end, 10);
        p = end;
        while (*p == ' ') {
            p++;
        }
    }
    size_t n = strlen(p);
    uint8_t *coded = (uint8_t *)heap_caps_malloc(n * 3 / 4 + 4, MALLOC_CAP_SPIRAM);
    char *sql = z ? (char *)heap_caps_malloc(raw > 0 && raw <= SQL_MAX ? raw + 1 : 1, MALLOC_CAP_SPIRAM) : (char *)coded;
    char *out = (char *)heap_caps_malloc(SQL_OUT, MALLOC_CAP_SPIRAM);
    int len = coded ? devctl_b64_decode(p, coded, (int)(n * 3 / 4 + 3)) : -1;
    if (z && sql && len > 0) {
        size_t got = raw > 0 && raw <= SQL_MAX
                     ? tinfl_decompress_mem_to_mem(sql, (size_t)raw, coded, (size_t)len, TINFL_FLAG_PARSE_ZLIB_HEADER)
                     : TINFL_DECOMPRESS_MEM_TO_MEM_FAILED;
        len = got == (size_t)raw ? (int)raw : -1;
    }
    if (!coded || !sql || !out) {
        devctl_reply("ERR sql: no memory\n");
    } else if (len <= 0) {
        devctl_reply("ERR sql: sql [z <size>] <base64 of the SQL> (tools/devctl.py --sql codes it)\n");
    } else {
        sql[len] = 0;
        stats_sql_result_t r;
        bool ok = stats_db_sql(sql, out, SQL_OUT, &r);
        if (z && devctl_z_begin('Q', true)) {              /* the result, compressed */
            serial_println("SQL BEGIN z");
            devctl_z_write(out, strlen(out));
            devctl_z_end();
            serial_printf("SQL END %u %08x\n", (unsigned)devctl_z_bytes(), (unsigned)devctl_z_crc());
        } else {
            for (char *q = out; *q;) {              /* the result, a line at a time */
                char *e = strchr(q, '\n');
                if (e) {
                    *e = 0;
                }
                serial_print("SQL ");
                serial_println(q);
                if (!e) {
                    break;
                }
                q = e + 1;
            }
        }
        if (ok) {
            serial_printf("OK sql %d %d%s\n", r.rows, r.changes, r.truncated ? " truncated" : "");
        } else {
            serial_printf("ERR sql: %s\n", r.err);
        }
    }
    if (sql != (char *)coded) {
        heap_caps_free(sql);
    }
    heap_caps_free(coded);
    heap_caps_free(out);
}

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sqlite3.h>

struct Shadow {
    sqlite3 *db;
    sqlite3_stmt *range;
    sqlite3_stmt *lookup;
};

struct Result {
    uint64_t rows;
    uint64_t bytes;
};

static const char range_sql[] =
    "SELECT m.number, a.overview FROM newsgroup AS g "
    "JOIN membership AS m ON m.group_id = g.id "
    "JOIN article AS a ON a.id = m.article_id "
    "WHERE g.name = ?1 AND m.number BETWEEN ?2 AND ?3 ORDER BY m.number";

static const char lookup_sql[] =
    "SELECT m.number, a.overview FROM article AS a "
    "JOIN membership AS m ON m.article_id = a.id "
    "JOIN newsgroup AS g ON g.id = m.group_id "
    "WHERE g.name = ?1 AND a.message_id = ?2 ORDER BY m.number DESC LIMIT 1";

static char *
snapshot_uri(const char *path)
{
    static const char suffix[] = "?immutable=1";
    static const char hex[] = "0123456789ABCDEF";
    const char *scheme = path[0] == '/' ? "file://" : "file:";
    size_t prefix = strlen(scheme);
    size_t length = strlen(path);
    char *uri, *out;

    if (length > (SIZE_MAX - prefix - sizeof(suffix)) / 3)
        return NULL;
    uri = malloc(prefix + length * 3 + sizeof(suffix));
    if (uri == NULL)
        return NULL;
    memcpy(uri, scheme, prefix);
    out = uri + prefix;
    for (const unsigned char *p = (const unsigned char *)path; *p != '\0'; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("/-._~", *p) != NULL) {
            *out++ = *p;
        } else {
            *out++ = '%';
            *out++ = hex[*p >> 4];
            *out++ = hex[*p & 15];
        }
    }
    memcpy(out, suffix, sizeof(suffix));
    return uri;
}

static int
shadow_open(struct Shadow *shadow, const char *path)
{
    char *uri = snapshot_uri(path);
    int code;

    if (uri == NULL) {
        fputs("cannot allocate overview snapshot URI\n", stderr);
        return -1;
    }
    code = sqlite3_open_v2(uri, &shadow->db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, NULL);
    free(uri);
    if (code != SQLITE_OK ||
        sqlite3_prepare_v2(shadow->db, range_sql, -1, &shadow->range, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(shadow->db, lookup_sql, -1, &shadow->lookup, NULL) != SQLITE_OK) {
        fprintf(stderr, "open overview snapshot: %s\n",
                shadow->db == NULL ? "out of memory" : sqlite3_errmsg(shadow->db));
        return -1;
    }
    return 0;
}

static void
shadow_close(struct Shadow *shadow)
{
    sqlite3_finalize(shadow->lookup);
    sqlite3_finalize(shadow->range);
    sqlite3_close(shadow->db);
}

static int
bind_bytes(sqlite3_stmt *statement, int parameter, const char *value)
{
    return sqlite3_bind_blob64(statement, parameter, value, strlen(value), SQLITE_STATIC);
}

/* If out is NULL, count the wire bytes without writing to a socket or a file. */
static int
read_rows(sqlite3_stmt *statement, FILE *out, struct Result *result)
{
    int code;
    int status = -1;

    *result = (struct Result){0};
    while ((code = sqlite3_step(statement)) == SQLITE_ROW) {
        char prefix[32];
        int64_t number = sqlite3_column_int64(statement, 0);
        const void *data = sqlite3_column_blob(statement, 1);
        int size = sqlite3_column_bytes(statement, 1);
        int prefix_size = snprintf(prefix, sizeof(prefix), "%" PRId64 "\t", number);

        if (data == NULL || size <= 0 || prefix_size < 0 ||
            (size_t)prefix_size >= sizeof(prefix)) {
            fputs("invalid row in overview snapshot\n", stderr);
            goto done;
        }
        if (out != NULL &&
            (fwrite(prefix, 1, (size_t)prefix_size, out) != (size_t)prefix_size ||
             fwrite(data, 1, (size_t)size, out) != (size_t)size)) {
            perror("write overview");
            goto done;
        }
        result->rows++;
        result->bytes += (uint64_t)prefix_size + (uint64_t)size;
    }
    if (code != SQLITE_DONE) {
        fprintf(stderr, "read overview: %s\n", sqlite3_errmsg(sqlite3_db_handle(statement)));
        goto done;
    }
    status = 0;

done:
    sqlite3_reset(statement);
    return status;
}

static int
shadow_read_range(struct Shadow *shadow, const char *group, int64_t first, int64_t last,
                  FILE *out, struct Result *result)
{
    sqlite3_stmt *statement = shadow->range;

    if (bind_bytes(statement, 1, group) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, first) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 3, last) != SQLITE_OK) {
        fprintf(stderr, "bind group range: %s\n", sqlite3_errmsg(shadow->db));
        return -1;
    }
    return read_rows(statement, out, result);
}

static int
shadow_lookup_msgid(struct Shadow *shadow, const char *group, const char *id, FILE *out)
{
    sqlite3_stmt *statement = shadow->lookup;
    struct Result result;

    if (bind_bytes(statement, 1, group) != SQLITE_OK ||
        bind_bytes(statement, 2, id) != SQLITE_OK) {
        fprintf(stderr, "bind Message-ID: %s\n", sqlite3_errmsg(shadow->db));
        return -1;
    }
    return read_rows(statement, out, &result);
}

static int
parse_number(const char *text, int64_t *number)
{
    char *end;
    intmax_t value;

    if (*text < '1' || *text > '9')
        return -1;
    errno = 0;
    value = strtoimax(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || value > INT64_MAX)
        return -1;
    *number = value;
    return 0;
}

static int
compare_samples(const void *lhs, const void *rhs)
{
    double left = *(const double *)lhs;
    double right = *(const double *)rhs;

    return (left > right) - (left < right);
}

static int
shadow_bench(struct Shadow *shadow, const char *group, int64_t first, int64_t last,
             size_t repeats)
{
    struct Result result = {0};
    double *samples = calloc(repeats, sizeof(*samples));
    int status = -1;

    if (samples == NULL) {
        perror("allocate benchmark samples");
        return -1;
    }
    for (size_t i = 0; i < repeats; i++) {
        struct timespec start, end;

        if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
            perror("monotonic clock");
            goto done;
        }
        if (shadow_read_range(shadow, group, first, last, NULL, &result) != 0)
            goto done;
        if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
            perror("monotonic clock");
            goto done;
        }
        samples[i] = (end.tv_sec - start.tv_sec) * 1000.0 +
                     (end.tv_nsec - start.tv_nsec) / 1000000.0;
    }
    qsort(samples, repeats, sizeof(*samples), compare_samples);
    printf("{\"sqlite_version\":\"%s\",\"repeats\":%zu,\"rows\":%" PRIu64
           ",\"bytes\":%" PRIu64 ",\"p50_ms\":%.6f,\"p95_ms\":%.6f}\n",
           sqlite3_libversion(), repeats, result.rows, result.bytes,
           samples[(repeats - 1) / 2], samples[(repeats - 1) * 95 / 100]);
    status = 0;

done:
    free(samples);
    return status;
}

static int
usage(const char *program)
{
    fprintf(stderr, "usage: %s SNAPSHOT range GROUP FIRST LAST\n"
                    "       %s SNAPSHOT lookup GROUP MESSAGE-ID\n"
                    "       %s SNAPSHOT bench GROUP FIRST LAST REPEATS\n",
            program, program, program);
    return EXIT_FAILURE;
}

int
main(int argc, char **argv)
{
    struct Shadow shadow = {0};
    struct Result result;
    int64_t first = 0, last = 0, repeats = 0;
    int status = -1;
    int lookup, bench;

    if (argc < 4 || argv[1][0] == '\0' || argv[3][0] == '\0')
        return usage(argv[0]);
    lookup = strcmp(argv[2], "lookup") == 0;
    bench = strcmp(argv[2], "bench") == 0;
    if (lookup) {
        if (argc != 5 || argv[4][0] == '\0')
            return usage(argv[0]);
    } else {
        if ((!bench && strcmp(argv[2], "range") != 0) ||
            argc != (bench ? 7 : 6) ||
            parse_number(argv[4], &first) != 0 ||
            parse_number(argv[5], &last) != 0 || first > last)
            return usage(argv[0]);
        if (bench && (parse_number(argv[6], &repeats) != 0 || repeats > 10000))
            return usage(argv[0]);
    }

    if (shadow_open(&shadow, argv[1]) == 0) {
        if (lookup)
            status = shadow_lookup_msgid(&shadow, argv[3], argv[4], stdout);
        else if (bench)
            status = shadow_bench(&shadow, argv[3], first, last, (size_t)repeats);
        else
            status = shadow_read_range(&shadow, argv[3], first, last, stdout, &result);
    }
    shadow_close(&shadow);
    if (status == 0 && fflush(stdout) == EOF) {
        perror("flush overview");
        status = -1;
    }
    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

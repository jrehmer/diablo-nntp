#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <sqlite3.h>

#define WRITER_APPLICATION_ID 1331058514
#define WAL_CHECKPOINT_PAGES 4096

enum Query {
    FIND_GROUP, ADD_GROUP, FIND_ARTICLE, ADD_ARTICLE,
    ADD_MEMBERSHIP, FIND_MEMBERSHIP, UPDATE_HIGH, QUERY_COUNT
};

static const char *const sql[QUERY_COUNT] = {
    [FIND_GROUP] = "SELECT id FROM newsgroup WHERE name=?1",
    [ADD_GROUP] = "INSERT INTO newsgroup (name) VALUES (?1)",
    [FIND_ARTICLE] = "SELECT id, overview FROM article WHERE message_id=?1",
    [ADD_ARTICLE] = "INSERT INTO article (message_id, overview) VALUES (?1, ?2)",
    [ADD_MEMBERSHIP] = "INSERT INTO membership VALUES (?1, ?2, ?3) "
                       "ON CONFLICT (group_id, number) DO NOTHING",
    [FIND_MEMBERSHIP] = "SELECT article_id FROM membership WHERE group_id=?1 AND number=?2",
    [UPDATE_HIGH] = "INSERT INTO group_high VALUES (?1, ?2) "
                    "ON CONFLICT (group_id) DO UPDATE SET number=excluded.number "
                    "WHERE excluded.number > group_high.number",
};

struct Bytes {
    const char *data;
    size_t length;
};

struct Membership {
    struct Bytes group, id, overview;
    int64_t number;
};

struct Writer {
    sqlite3 *db;
    sqlite3_stmt *query[QUERY_COUNT];
    char *wal_path;
};

static int
execute(struct Writer *writer, const char *statement)
{
    if (sqlite3_exec(writer->db, statement, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "%s: %s\n", statement, sqlite3_errmsg(writer->db));
        return -1;
    }
    return 0;
}

static int
writer_configs(struct Writer *writer)
{
    sqlite3_stmt *statement = NULL;
    int valid;

    valid = sqlite3_prepare_v2(writer->db, "PRAGMA application_id", -1, &statement, NULL) ==
                SQLITE_OK &&
            sqlite3_step(statement) == SQLITE_ROW &&
            sqlite3_column_int(statement, 0) == WRITER_APPLICATION_ID;
    sqlite3_finalize(statement);
    statement = NULL;
    if (!valid) {
        fputs("expected a disposable overview writer database\n", stderr);
        return -1;
    }
    valid = sqlite3_prepare_v2(writer->db, "PRAGMA journal_mode=WAL", -1, &statement, NULL) ==
                SQLITE_OK &&
            sqlite3_step(statement) == SQLITE_ROW &&
            sqlite3_column_text(statement, 0) != NULL &&
            strcmp((const char *)sqlite3_column_text(statement, 0), "wal") == 0;
    sqlite3_finalize(statement);
    if (!valid) {
        fprintf(stderr, "cannot enable WAL: %s\n", sqlite3_errmsg(writer->db));
        return -1;
    }
    if (execute(writer, "PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON") != 0 ||
        sqlite3_wal_autocheckpoint(writer->db, WAL_CHECKPOINT_PAGES) != SQLITE_OK)
        return -1;
    return 0;
}

static int
writer_open(struct Writer *writer, const char *path)
{
    if (sqlite3_open_v2(path, &writer->db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        fprintf(stderr, "open writer database: %s\n",
                writer->db == NULL ? "out of memory" : sqlite3_errmsg(writer->db));
        return -1;
    }
    if (writer_configs(writer) != 0)
        return -1;
    writer->wal_path = sqlite3_mprintf("%s-wal", sqlite3_db_filename(writer->db, "main"));
    if (writer->wal_path == NULL) {
        fputs("cannot allocate WAL path\n", stderr);
        return -1;
    }
    for (size_t i = 0; i < QUERY_COUNT; i++) {
        if (sqlite3_prepare_v2(writer->db, sql[i], -1, &writer->query[i], NULL) != SQLITE_OK) {
            fprintf(stderr, "prepare writer statement: %s\n", sqlite3_errmsg(writer->db));
            return -1;
        }
    }
    return 0;
}

static void
writer_close(struct Writer *writer)
{
    for (size_t i = 0; i < QUERY_COUNT; i++)
        sqlite3_finalize(writer->query[i]);
    if (writer->db != NULL && !sqlite3_get_autocommit(writer->db))
        (void)execute(writer, "ROLLBACK");
    sqlite3_close(writer->db);
    sqlite3_free(writer->wal_path);
}

static int
bind_bytes(sqlite3_stmt *statement, int parameter, struct Bytes value)
{
    return sqlite3_bind_blob64(statement, parameter, value.data, value.length, SQLITE_TRANSIENT);
}

static int
finish(sqlite3_stmt *statement, int success, const char *operation)
{
    if (!success)
        fprintf(stderr, "%s: %s\n", operation, sqlite3_errmsg(sqlite3_db_handle(statement)));
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    return success ? 0 : -1;
}

static int
writer_group_id(struct Writer *writer, struct Bytes name, sqlite3_int64 *id)
{
    sqlite3_stmt *find = writer->query[FIND_GROUP];
    sqlite3_stmt *add = writer->query[ADD_GROUP];
    int code = bind_bytes(find, 1, name);

    if (code == SQLITE_OK)
        code = sqlite3_step(find);
    if (code == SQLITE_ROW) {
        *id = sqlite3_column_int64(find, 0);
        return finish(find, 1, "find group");
    }
    if (finish(find, code == SQLITE_DONE, "find group") != 0)
        return -1;
    if (finish(add, bind_bytes(add, 1, name) == SQLITE_OK &&
                    sqlite3_step(add) == SQLITE_DONE, "add group") != 0)
        return -1;
    *id = sqlite3_last_insert_rowid(writer->db);
    return 0;
}

static int
writer_article_id(struct Writer *writer, const struct Membership *row, sqlite3_int64 *id)
{
    sqlite3_stmt *find = writer->query[FIND_ARTICLE];
    sqlite3_stmt *add = writer->query[ADD_ARTICLE];
    int code = bind_bytes(find, 1, row->id);

    if (code == SQLITE_OK)
        code = sqlite3_step(find);
    if (code == SQLITE_ROW) {
        const void *existing = sqlite3_column_blob(find, 1);
        int size = sqlite3_column_bytes(find, 1);
        int identical = existing != NULL && (size_t)size == row->overview.length &&
                        memcmp(existing, row->overview.data, row->overview.length) == 0;

        *id = sqlite3_column_int64(find, 0);
        finish(find, 1, "find article");
        if (!identical) {
            fputs("conflicting overview for Message-ID\n", stderr);
            return -1;
        }
        return 0;
    }
    if (finish(find, code == SQLITE_DONE, "find article") != 0)
        return -1;
    if (finish(add, bind_bytes(add, 1, row->id) == SQLITE_OK &&
                    bind_bytes(add, 2, row->overview) == SQLITE_OK &&
                    sqlite3_step(add) == SQLITE_DONE, "add article") != 0)
        return -1;
    *id = sqlite3_last_insert_rowid(writer->db);
    return 0;
}

static int
writer_store_membership(struct Writer *writer, const struct Membership *row)
{
    sqlite3_stmt *add = writer->query[ADD_MEMBERSHIP];
    sqlite3_stmt *find = writer->query[FIND_MEMBERSHIP];
    sqlite3_stmt *high = writer->query[UPDATE_HIGH];
    sqlite3_int64 group_id, article_id;

    if (writer_group_id(writer, row->group, &group_id) != 0 ||
        writer_article_id(writer, row, &article_id) != 0)
        return -1;
    if (finish(add, sqlite3_bind_int64(add, 1, group_id) == SQLITE_OK &&
                    sqlite3_bind_int64(add, 2, row->number) == SQLITE_OK &&
                    sqlite3_bind_int64(add, 3, article_id) == SQLITE_OK &&
                    sqlite3_step(add) == SQLITE_DONE, "add membership") != 0)
        return -1;
    if (sqlite3_changes(writer->db) == 0) {
        int identical = sqlite3_bind_int64(find, 1, group_id) == SQLITE_OK &&
                        sqlite3_bind_int64(find, 2, row->number) == SQLITE_OK &&
                        sqlite3_step(find) == SQLITE_ROW &&
                        sqlite3_column_int64(find, 0) == article_id;
        finish(find, 1, "find membership");
        if (!identical) {
            fputs("conflicting article number in group\n", stderr);
            return -1;
        }
        return 0;
    }
    return finish(high, sqlite3_bind_int64(high, 1, group_id) == SQLITE_OK &&
                        sqlite3_bind_int64(high, 2, row->number) == SQLITE_OK &&
                        sqlite3_step(high) == SQLITE_DONE, "update group high-water mark");
}

static int
parse_membership(const char *line, size_t length, struct Membership *row)
{
    const char *group_end, *number, *id, *id_end;
    char *number_end;
    intmax_t value;

    if (length == 0 || line[length - 1] != '\n' || memchr(line, '\0', length) != NULL)
        return -1;
    group_end = strchr(line, '\t');
    if (group_end == NULL || group_end == line)
        return -1;
    number = group_end + 1;
    if (*number < '1' || *number > '9')
        return -1;
    errno = 0;
    value = strtoimax(number, &number_end, 10);
    if (errno == ERANGE || *number_end != '\t' || value > INT64_MAX)
        return -1;
    id = number_end + 1;
    for (int i = 0; i < 3; i++) {
        id = strchr(id, '\t');
        if (id == NULL)
            return -1;
        id++;
    }
    id_end = strchr(id, '\t');
    if (id_end == NULL || id_end == id)
        return -1;
    row->group = (struct Bytes){line, (size_t)(group_end - line)};
    row->id = (struct Bytes){id, (size_t)(id_end - id)};
    row->overview = (struct Bytes){number_end + 1, (size_t)(line + length - number_end - 1)};
    row->number = value;
    return 0;
}

static double
now(void)
{
    struct timespec time;

    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        perror("monotonic clock");
        return -1;
    }
    return time.tv_sec * 1000.0 + time.tv_nsec / 1000000.0;
}

static int
writer_commit_batch(struct Writer *writer)
{
    struct stat statbuf;
    intmax_t wal_bytes = 0;
    double start = now(), end;

    if (start < 0 || execute(writer, "COMMIT") != 0)
        return -1;
    end = now();
    if (end < 0)
        return -1;
    if (stat(writer->wal_path, &statbuf) == 0) {
        wal_bytes = statbuf.st_size;
    } else if (errno != ENOENT) {
        perror("stat WAL");
        return -1;
    }
    if (printf("commit %.6f %" PRIdMAX "\n", end - start, wal_bytes) < 0) {
        perror("write commit timing");
        return -1;
    }
    return 0;
}

static int
writer_ingest(struct Writer *writer, FILE *input, uint64_t batch)
{
    char *line = NULL, *previous = NULL;
    size_t line_capacity = 0, previous_length = 0;
    uint64_t articles = 0, memberships = 0, in_batch = 0;
    ssize_t length;
    double start = now(), end, checkpoint;
    int status = -1;

    if (start < 0)
        return -1;
    while ((length = getline(&line, &line_capacity, input)) >= 0) {
        struct Membership row;

        if (parse_membership(line, (size_t)length, &row) != 0) {
            fputs("invalid overview membership\n", stderr);
            goto done;
        }
        if (previous_length != row.id.length ||
            memcmp(previous, row.id.data, row.id.length) != 0) {
            char *id;

            if (in_batch == batch) {
                if (writer_commit_batch(writer) != 0)
                    goto done;
                in_batch = 0;
            }
            if (in_batch == 0 && execute(writer, "BEGIN IMMEDIATE") != 0)
                goto done;
            id = realloc(previous, row.id.length);
            if (id == NULL) {
                perror("remember Message-ID");
                goto done;
            }
            previous = id;
            memcpy(previous, row.id.data, row.id.length);
            previous_length = row.id.length;
            articles++;
            in_batch++;
        }
        if (writer_store_membership(writer, &row) != 0)
            goto done;
        memberships++;
    }
    if (!feof(input) || ferror(input)) {
        perror("read overview fixture");
        goto done;
    }
    if (in_batch != 0 && writer_commit_batch(writer) != 0)
        goto done;
    end = now();
    if (end < 0)
        goto done;
    if (sqlite3_wal_checkpoint_v2(writer->db, NULL, SQLITE_CHECKPOINT_TRUNCATE,
                                 NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "checkpoint writer database: %s\n", sqlite3_errmsg(writer->db));
        goto done;
    }
    checkpoint = now();
    if (checkpoint < 0)
        goto done;
    if (printf("summary %" PRIu64 " %" PRIu64 " %.6f %.6f\n",
               articles, memberships, end - start, checkpoint - end) < 0 ||
        fflush(stdout) == EOF) {
        perror("write benchmark summary");
        goto done;
    }
    status = 0;

done:
    if (status != 0)
        fprintf(stderr, "stopped at input row %" PRIu64 "\n", memberships + 1);
    free(previous);
    free(line);
    return status;
}

int
main(int argc, char **argv)
{
    struct Writer writer = {0};
    FILE *input;
    char *end;
    uintmax_t batch;
    int result = -1;

    if (argc != 4 || argv[1][0] == '\0' || *argv[2] < '1' || *argv[2] > '9') {
        fprintf(stderr, "usage: %s DATABASE BATCH_ARTICLES ROWS.tsv\n", argv[0]);
        return EXIT_FAILURE;
    }
    errno = 0;
    batch = strtoumax(argv[2], &end, 10);
    if (errno == ERANGE || *end != '\0' || batch > UINT64_MAX) {
        fputs("invalid batch size\n", stderr);
        return EXIT_FAILURE;
    }
    input = fopen(argv[3], "rb");
    if (input == NULL) {
        perror("open overview fixture");
        return EXIT_FAILURE;
    }
    if (writer_open(&writer, argv[1]) == 0)
        result = writer_ingest(&writer, input, (uint64_t)batch);
    writer_close(&writer);
    fclose(input);
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

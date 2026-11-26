PRAGMA foreign_keys = ON;

CREATE TABLE newsgroup (
    id INTEGER PRIMARY KEY,
    name BLOB NOT NULL UNIQUE
);

CREATE TABLE article (
    id INTEGER PRIMARY KEY,
    message_id BLOB NOT NULL UNIQUE,
    overview BLOB NOT NULL CHECK (length(overview) > 0)
);

CREATE TABLE header_tail (
    article_id INTEGER PRIMARY KEY REFERENCES article (id) ON DELETE CASCADE,
    zlib_data BLOB NOT NULL
);

CREATE TABLE membership (
    group_id INTEGER NOT NULL REFERENCES newsgroup (id),
    number INTEGER NOT NULL CHECK (number >= 1),
    article_id INTEGER NOT NULL REFERENCES article (id),
    PRIMARY KEY (group_id, number)
) WITHOUT ROWID;

CREATE INDEX membership_by_article ON membership (article_id, group_id, number DESC);

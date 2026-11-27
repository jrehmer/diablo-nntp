PRAGMA application_id = 1331058514; -- OVWR

CREATE TABLE group_high (
    group_id INTEGER PRIMARY KEY REFERENCES newsgroup (id),
    number INTEGER NOT NULL CHECK (number >= 1)
);

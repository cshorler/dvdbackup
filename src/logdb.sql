

CREATE TABLE dvdbackup_tbl (
    pid integer,
    lvl text,
    msg text,
    dt timestamp DEFAULT statement_timestamp()
);



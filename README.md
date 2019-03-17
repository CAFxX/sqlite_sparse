# sqlite_sparse
A small linux-only utility that turns in-place a SQLite3 file
into a sparse file by deallocating all SQLite3 free pages in the
file. The filesystem containing the SQLite3 file must support
sparse files creation via `fallocate(FALLOC_FL_PUNCH_HOLE)`.

## Rationale
`VACUUM`ing a SQLite3 database to release the free pages can take
a very long time if the file is big. On the contrary, deallocating
the free pages using sqlite_sparse is faster - especially in the
common case where the number of used pages is bigger than the number
of free pages.

`VACUUM`ing will shrink the database further because it not only
drops unused free pages, but it also coalesce partially-used pages
in fully-used pages.

## Compile
```
gcc -O2 -o sqlite_sparse sqlite_sparse.c
```

## Usage
```
# make a backup copy of your database (file.sqlite)
cp file.sqlite file.sqlite.bak

# check integrity of the database, and ensure there are no hot journal/wal
sqlite3 file.sqlite "pragma integrity_check"

# deallocate all free pages
sqlite_sparse file.sqlite

# ensure we did not corrupt the database
sqlite3 file.sqlite "pragma integrity_check"
```

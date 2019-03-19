// sqlite_sparse.c
// 2019 Carlo Alberto Ferraris <cafxx@strayorange.com>
//
// A small linux-only utility that turns in-place a sqlite3 file
// into a sparse file by deallocating all sqlite3 free pages in the
// file. The filesystem containing the sqlite3 file must support
// sparse files creation via fallocate(FALLOC_FL_PUNCH_HOLE).
// Windows support is implemented, but untested.
//
// Compile with: gcc -O2 -o sqlite_sparse sqlite_sparse.c
// Usage:
//
//   # make a backup copy of your database (file.sqlite)
//   cp file.sqlite file.sqlite.bak
//
//   # check integrity of the database, and ensure there are no hot journal/wal
//   sqlite3 file.sqlite "pragma integrity_check"
//
//   # deallocate all free pages
//   sqlite_sparse file.sqlite
//
//   # ensure we did not corrupt the database
//   sqlite3 file.sqlite "pragma integrity_check"
//
// IMPORTANT: Make a backup of your sqlite3 file before running
// sqlite_sparse, and run the sqlite `PRAGMA integrity_check;`.

#if defined(_WIN32) || defined(_WIN64)
#define __windows__
#elif defined(__linux__)
#define _GNU_SOURCE
#else
#error "Only Linux and Windows are supported"
#endif

#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(__windows__)
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <io.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#endif

#if defined(__windows__)
static ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    assert(_lseek(fd, offset, SEEK_SET) == offset);
    return _read(fd, buf, count);
}
#elif defined(__linux__)
#define O_BINARY 0
#endif

static int32_t numpages(int fd, size_t pagesize) {
    off_t off = lseek(fd, 0, SEEK_END);
    assert(off != -1);
    assert(off % pagesize == 0);
    return off/pagesize;
}   

static int32_t readpageindex(int fd, size_t off) {
    uint32_t v;
    assert(sizeof(v) == 4);
    assert(pread(fd, &v, sizeof(v), off) == sizeof(v));
    return (int32_t)ntohl(v);
}

static size_t readpagesize(int fd) {
    uint16_t v;
    assert(sizeof(v) == 2);
    assert(pread(fd, &v, sizeof(v), 16) == sizeof(v));
    v = ntohs(v);
    switch (v) {
    case 1<<9: case 1<<10: case 1<<11: case 1<<12:
    case 1<<13: case 1<<14: case 1<<15:
        return v;
    case 1:
        return 1<<16;
    default:
        abort();
    }
}

static void checkheader(int fd) {
    char buf[16];
    assert(pread(fd, buf, sizeof(buf), 0) == sizeof(buf));
    assert(memcmp(buf, "SQLite format 3", 16) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    int fd = open(argv[1], O_RDWR|O_BINARY);
    assert(fd != -1);

    checkheader(fd);

    size_t pagesize = readpagesize(fd);
    int32_t pages = getnumpages(fd, pagesize);
    char *bitmap = calloc((pages+7)/8+1, 1);
    int32_t freelistpage = readpageindex(fd, 32);
    size_t freed = 0;

    while (freelistpage > 1) {
        size_t pageoff = (freelistpage-1)*pagesize;
        int32_t L = readpageindex(fd, pageoff+4);
        assert((L+2)*4 < pagesize);
        for (int i = 0; i < L; i++) {
            int32_t freepage = readpageindex(fd, pageoff+(i+2)*4) - 1;
            assert(freepage > 0);
            assert(freepage < pages);
            bitmap[freepage/8] |= 1<<(freepage%8);
            freed++;
        }
        freelistpage = readpageindex(fd, pageoff+0);
    }

#ifdef __windows__
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    int sparse = 0;
#endif

    int32_t first = -1;
    for (i = 0; i < pages+1; i++) {
        if (first == -1 && (bitmap[i/8]&(1<<(i%8)) == 1)) {
            first = i;
        } else if (first != -1 && (bitmap[i/8]&(1<<(i%8)) == 0)) {
#if defined(__windows__)
            DWORD unused;
            if (!sparse) {
                assert(DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &unused, NULL));
                sparse = 1;
            }
            FILE_ZERO_DATA_INFORMATION fzdi;
            fzdi.FileOffset.QuadPart = pagesize*first;
            fzdi.BeyondFinalZero.QuadPart = pagesize*i;
            assert(DeviceIoControl(h, FSCTL_SET_ZERO_DATA, &fzdi, sizeof(fzdi), NULL, 0, &unused, NULL));
#elif defined(__linux__)
            assert(fallocate(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, pagesize*first, (i-first)*pagesize) == 0);
#endif            
            first = -1;
        }
    }
    
    printf("Deallocated %d pages (%d bytes)\n", freed, freed*pagesize);
}

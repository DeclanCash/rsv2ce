#pragma once

typedef struct {
    int number;
    char *name;
    char *abbr;
} rsv2ce_book;

typedef struct {
    int book;
    int chapter;
    int verse;
    char *text;
} rsv2ce_verse;

extern rsv2ce_verse rsv2ce_verses[];

extern int rsv2ce_verses_length;

extern rsv2ce_book rsv2ce_books[];

extern int rsv2ce_books_length;

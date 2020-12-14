/*
rsv2ce: Read the Word of God from your terminal

License: Public domain
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <sys/ioctl.h>

#include "data.h"
#include "intset.h"

typedef struct {
    int maximum_line_length;

    int context_before;
    int context_after;
    bool context_chapter;
} rsv2ce_config;

#define RSV2CE_REF_SEARCH 1
#define RSV2CE_REF_EXACT 2
#define RSV2CE_REF_EXACT_SET 3
#define RSV2CE_REF_RANGE 4
#define RSV2CE_REF_RANGE_EXT 5

typedef struct rsv2ce_ref {
    int type;
    unsigned int book;
    unsigned int chapter;
    unsigned int chapter_end;
    unsigned int verse;
    unsigned int verse_end;
    intset *verse_set;
    char *search_str;
    regex_t search;
} rsv2ce_ref;

static rsv2ce_ref *
rsv2ce_newref()
{
    return calloc(1, sizeof(rsv2ce_ref));
}

static void
rsv2ce_freeref(rsv2ce_ref *ref)
{
    if (ref) {
        free(ref->search_str);
        regfree(&ref->search);
        free(ref);
    }
}


static bool
rsv2ce_bookequal(const char *a, const char *b, bool short_match)
{
    for (size_t i = 0, j = 0; ; ) {
        if ((!a[i] && !b[j]) || (short_match && !b[j])) {
            return true;
        } else if (a[i] == ' ') {
            i++;
        } else if (b[j] == ' ') {
            j++;
        } else if (tolower(a[i]) != tolower(b[j])) {
            return false;
        } else {
            i++;
            j++;
        }
    }
}

static bool
rsv2ce_book_matches(const rsv2ce_book *book, const char *s)
{
    return rsv2ce_bookequal(book->name, s, false) ||
        rsv2ce_bookequal(book->abbr, s, false) ||
        rsv2ce_bookequal(book->name, s, true);
}

static int
rsv2ce_book_fromname(const char *s)
{
    for (int i = 0; i < rsv2ce_books_length; i++) {
        const rsv2ce_book *book = &rsv2ce_books[i];
        if (rsv2ce_book_matches(book, s)) {
            return book->number;
        }
    }
    return 0;
}

static int
rsv2ce_scanbook(const char *s, int *n)
{
    int i;
    int mode = 0;
    for (i = 0; s[i]; i++) {
        if (s[i] == ' ') {
            continue;
        } else if (('a' <= s[i] && s[i] <= 'z') || ('A' <= s[i] && s[i] <= 'Z')) {
            mode = 1;
        } else if ('0' <= s[i] && s[i] <= '9' && mode == 0) {
            continue;
        } else {
            break;
        }
    }
    *n = i;
    return mode >= 1;
}

static int
rsv2ce_parseref(rsv2ce_ref *ref, const char *ref_str)
{
    // 1. <book>
    // 2. <book>:?<chapter>
    // 3. <book>:?<chapter>:<verse>
    // 3a. <book>:?<chapter>:<verse>[,<verse>]...
    // 4. <book>:?<chapter>-<chapter>
    // 5. <book>:?<chapter>:<verse>-<verse>
    // 6. <book>:?<chapter>:<verse>-<chapter>:<verse>
    // 7. /<search>
    // 8. <book>/search
    // 9. <book>:?<chapter>/search

    ref->type = 0;
    ref->book = 0;
    ref->chapter = 0;
    ref->chapter_end = 0;
    ref->verse = 0;
    ref->verse_end = 0;
    intset_free(ref->verse_set);
    ref->verse_set = NULL;
    free(ref->search_str);
    ref->search_str = NULL;
    regfree(&ref->search);

    int n = 0;
    if (rsv2ce_scanbook(ref_str, &n) == 1) {
        // 1, 2, 3, 3a, 4, 5, 6, 8, 9
        char *bookname = strndup(ref_str, n);
        ref->book = rsv2ce_book_fromname(bookname);
        free(bookname);
        ref_str = &ref_str[n];
    } else if (ref_str[0] == '/') {
        // 7
        goto search;
    } else {
        return 1;
    }

    if (sscanf(ref_str, ":%u%n", &ref->chapter, &n) == 1 || sscanf(ref_str, "%u%n", &ref->chapter, &n) == 1) {
        // 2, 3, 3a, 4, 5, 6, 9
        ref_str = &ref_str[n];
    } else if (ref_str[0] == '/') {
        // 8
        goto search;
    } else if (ref_str[0] == '\0') {
        // 1
        ref->type = RSV2CE_REF_EXACT;
        return 0;
    } else {
        return 1;
    }

    if (sscanf(ref_str, ":%u%n", &ref->verse, &n) == 1) {
        // 3, 3a, 5, 6
        ref_str = &ref_str[n];
    } else if (sscanf(ref_str, "-%u%n", &ref->chapter_end, &n) == 1) {
        // 4
        if (ref_str[n] != '\0') {
            return 1;
        }
        ref->type = RSV2CE_REF_RANGE;
        return 0;
    } else if (ref_str[0] == '/') {
        // 9
        goto search;
    } else if (ref_str[0] == '\0') {
        // 2
        ref->type = RSV2CE_REF_EXACT;
        return 0;
    } else {
        return 1;
    }

    unsigned int value;
    int ret = sscanf(ref_str, "-%u%n", &value, &n);
    if (ret == 1 && ref_str[n] == '\0') {
        // 5
        ref->verse_end = value;
        ref->type = RSV2CE_REF_RANGE;
        return 0;
    } else if (ret == 1) {
        // 6
        ref->chapter_end = value;
        ref_str = &ref_str[n];
    } else if (ref_str[0] == '\0') {
        // 3
        ref->type = RSV2CE_REF_EXACT;
        return 0;
    } else if (sscanf(ref_str, ",%u%n", &value, &n) == 1) {
        // 3a
        ref->verse_set = intset_new();
        intset_add(ref->verse_set, ref->verse);
        intset_add(ref->verse_set, value);
        ref_str = &ref_str[n];
        while (true) {
            if (sscanf(ref_str, ",%u%n", &value, &n) != 1) {
                break;
            }
            intset_add(ref->verse_set, value);
            ref_str = &ref_str[n];
        }
        if (ref_str[0] != '\0') {
            return 1;
        }
        ref->type = RSV2CE_REF_EXACT_SET;
        return 0;
    } else {
        return 1;
    }

    if (sscanf(ref_str, ":%u%n", &ref->verse_end, &n) == 1 && ref_str[n] == '\0') {
        // 6
        ref->type = RSV2CE_REF_RANGE_EXT;
        return 0;
    } else {
        return 1;
    }

search:
    ref->type = RSV2CE_REF_SEARCH;
    if (regcomp(&ref->search, &ref_str[1], REG_EXTENDED|REG_ICASE|REG_NOSUB) != 0) {
        return 2;
    }
    ref->search_str = strdup(&ref_str[1]);
    return 0;
}

static char *
str_join(size_t n, char *strs[])
{
    size_t length = 0;
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            length++;
        }
        length += strlen(strs[i]);
    }
    char *str = malloc(length + 1);
    str[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            strcat(str, " ");
        }
        strcat(str, strs[i]);
    }
    return str;
}

static bool
rsv2ce_verse_matches(const rsv2ce_ref *ref, const rsv2ce_verse *verse)
{
    switch (ref->type) {
        case RSV2CE_REF_SEARCH:
            return (ref->book == 0 || ref->book == verse->book) &&
                (ref->chapter == 0 || verse->chapter == ref->chapter) &&
                regexec(&ref->search, verse->text, 0, NULL, 0) == 0;

        case RSV2CE_REF_EXACT:
            return ref->book == verse->book &&
                (ref->chapter == 0 || ref->chapter == verse->chapter) &&
                (ref->verse == 0 || ref->verse == verse->verse);

        case RSV2CE_REF_EXACT_SET:
            return ref->book == verse->book &&
                (ref->chapter == 0 || verse->chapter == ref->chapter) &&
                intset_contains(ref->verse_set, verse->verse);

        case RSV2CE_REF_RANGE:
            return ref->book == verse->book &&
                ((ref->chapter_end == 0 && ref->chapter == verse->chapter) ||
                    (verse->chapter >= ref->chapter && verse->chapter <= ref->chapter_end)) &&
                (ref->verse == 0 || verse->verse >= ref->verse) &&
                (ref->verse_end == 0 || verse->verse <= ref->verse_end);

        case RSV2CE_REF_RANGE_EXT:
            return ref->book == verse->book &&
                (
                    (verse->chapter == ref->chapter && verse->verse >= ref->verse && ref->chapter != ref->chapter_end) ||
                    (verse->chapter > ref->chapter && verse->chapter < ref->chapter_end) ||
                    (verse->chapter == ref->chapter_end && verse->verse <= ref->verse_end && ref->chapter != ref->chapter_end) ||
                    (ref->chapter == ref->chapter_end && verse->chapter == ref->chapter && verse->verse >= ref->verse && verse->verse <= ref->verse_end)
                );

        default:
            return false;
    }
}

#define RSV2CE_DIRECTION_BEFORE -1
#define RSV2CE_DIRECTION_AFTER 1

static int
rsv2ce_chapter_bounds(int i, int direction, int maximum_steps)
{
    assert(direction == -1 || direction == 1);

    int steps = 0;
    for ( ; 0 <= i && i < rsv2ce_verses_length; i += direction) {
        if (maximum_steps != -1 && steps >= maximum_steps) {
            break;
        }
        if ((direction == -1 && i == 0) || (direction == 1 && i + 1 == rsv2ce_verses_length)) {
            break;
        }
        const rsv2ce_verse *current = &rsv2ce_verses[i], *next = &rsv2ce_verses[i + direction];
        if (current->book != next->book || current->chapter != next->chapter) {
            break;
        }
        steps++;
    }
    return i;
}

static int
rsv2ce_next_match(const rsv2ce_ref *ref, int i)
{
    for ( ; i < rsv2ce_verses_length; i++) {
        const rsv2ce_verse *verse = &rsv2ce_verses[i];
        if (rsv2ce_verse_matches(ref, verse)) {
            return i;
        }
    }
    return -1;
}

typedef struct {
    int start;
    int end;
} rsv2ce_range;

typedef struct {
    int current;
    int next_match;
    rsv2ce_range matches[2];
} rsv2ce_next_data;

static void
rsv2ce_next_addrange(rsv2ce_next_data *next, rsv2ce_range range) {
    if (next->matches[0].start == -1 && next->matches[0].end == -1) {
        next->matches[0] = range;
    } else if (range.start < next->matches[0].end) {
        next->matches[0] = range;
    } else {
        next->matches[1] = range;
    }
}

static int
rsv2ce_next_verse(const rsv2ce_ref *ref, const rsv2ce_config *config, rsv2ce_next_data *next)
{
    if (next->current >= rsv2ce_verses_length) {
        return -1;
    }

    if (next->matches[0].start != -1 && next->matches[0].end != -1 && next->current >= next->matches[0].end) {
        next->matches[0] = next->matches[1];
        next->matches[1] = (rsv2ce_range){-1, -1};
    }

    if ((next->next_match == -1 || next->next_match < next->current) && next->next_match < rsv2ce_verses_length) {
        int next_match = rsv2ce_next_match(ref, next->current);
        if (next_match >= 0) {
            next->next_match = next_match;
            rsv2ce_range bounds = {
                .start = rsv2ce_chapter_bounds(next_match, RSV2CE_DIRECTION_BEFORE, config->context_chapter ? -1 : config->context_before),
                .end = rsv2ce_chapter_bounds(next_match, RSV2CE_DIRECTION_AFTER, config->context_chapter ? -1 : config->context_after) + 1,
            };
            rsv2ce_next_addrange(next, bounds);
        } else {
            next_match = rsv2ce_verses_length;
        }
    }

    if (next->matches[0].start == -1 && next->matches[0].end == -1) {
        return -1;
    }

    if (next->current < next->matches[0].start) {
        next->current = next->matches[0].start;
    }

    return next->current++;
}

#define ESC_BOLD "\033[1m"
#define ESC_UNDERLINE "\033[4m"
#define ESC_RESET "\033[m"

static void
rsv2ce_output_verse(const rsv2ce_verse *verse, FILE *f, const rsv2ce_config *config)
{
    fprintf(f, ESC_BOLD "%d:%d" ESC_RESET "\t", verse->chapter, verse->verse);
    char verse_text[1024];
    strcpy(verse_text, verse->text);
    size_t characters_printed = 0;
    char *word = strtok(verse_text, " ");
    while (word != NULL) {
        size_t word_length = strlen(word);
        if (characters_printed + word_length + (characters_printed > 0 ? 1 : 0) > config->maximum_line_length - 8 - 2) {
            fprintf(f, "\n\t");
            characters_printed = 0;
        }
        if (characters_printed > 0) {
            fprintf(f, " ");
            characters_printed++;
        }
        fprintf(f, "%s", word);
        characters_printed += word_length;
        word = strtok(NULL, " ");
    }
    fprintf(f, "\n");
}

static bool
rsv2ce_output(const rsv2ce_ref *ref, FILE *f, const rsv2ce_config *config)
{
    rsv2ce_next_data next = {
        .current = 0,
        .next_match = -1,
        .matches = {
            {-1, -1},
            {-1, -1},
        },
    };

    rsv2ce_verse *last_printed = NULL;
    for (int verse_id; (verse_id = rsv2ce_next_verse(ref, config, &next)) != -1; ) {
        rsv2ce_verse *verse = &rsv2ce_verses[verse_id];
        if (last_printed == NULL || verse->book != last_printed->book) {
            if (last_printed != NULL) {
                fprintf(f, "\n");
            }
            fprintf(f, ESC_UNDERLINE "%s" ESC_RESET "\n\n", rsv2ce_books[verse->book - 1].name);
        }
        rsv2ce_output_verse(verse, f, config);
        last_printed = verse;
    }
    return last_printed != NULL;
}

static int
rsv2ce_render(const rsv2ce_ref *ref, const rsv2ce_config *config)
{
    int fds[2];
    if (pipe(fds) == -1) {
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(fds[1]);
        dup2(fds[0], STDIN_FILENO);
        char *args[9] = {NULL};
        int arg = 0;
        args[arg++] = "less";
        args[arg++] = "-J";
        args[arg++] = "-I";
        if (ref->search_str != NULL) {
            args[arg++] = "-p";
            args[arg++] = ref->search_str;
        }
        args[arg++] = "-R";
        args[arg++] = "-f";
        args[arg++] = "-";
        args[arg++] = NULL;
        execvp("less", args);
        printf("unable not exec less\n");
        _exit(0);
    } else if (pid == -1) {
        printf("unable to fork\n");
        return 2;
    }
    close(fds[0]);
    FILE *output = fdopen(fds[1], "w");
    bool printed = rsv2ce_output(ref, output, config);
    if (!printed) {
        kill(pid, SIGTERM);
    }
    fclose(output);
    waitpid(pid, NULL, 0);
    if (!printed) {
        printf("unknown reference\n");
    }
    return 0;
}

const char *
usage = "usage: rsv2ce [flags] [reference...]\n"
    "\n"
    "Flags:\n"
    "  -A num  number of verses of context after matching verses\n"
    "  -B num  number of verses of context before matching verses\n"
    "  -C      show matching verses in context of the chapter\n"
    "  -l      list books\n"
    "  -h      show help\n"
    "\n"
    "Reference:\n"
    "    <Book>\n"
    "        Individual book\n"
    "    <Book>:<Chapter>\n"
    "        Individual chapter of a book\n"
    "    <Book>:<Chapter>:<Verse>[,<Verse>]...\n"
    "        Individual verse(s) of a specific chapter of a book\n"
    "    <Book>:<Chapter>-<Chapter>\n"
    "        Range of chapters in a book\n"
    "    <Book>:<Chapter>:<Verse>-<Verse>\n"
    "        Range of verses in a book chapter\n"
    "    <Book>:<Chapter>:<Verse>-<Chapter>:<Verse>\n"
    "        Range of chapters and verses in a book\n"
    "\n"
    "    /<Search>\n"
    "        All verses that match a pattern\n"
    "    <Book>/<Search>\n"
    "        All verses in a book that match a pattern\n"
    "    <Book>:<Chapter>/<Search>\n"
    "        All verses in a chapter of a book that match a pattern\n";

int
main(int argc, char *argv[])
{
    rsv2ce_config config = {
        .maximum_line_length = 80,

        .context_before = 0,
        .context_after = 0,
        .context_chapter = false,
    };

    bool list_books = false;

    opterr = 0;
    for (int opt; (opt = getopt(argc, argv, "A:B:ClWh")) != -1; ) {
        char *endptr;
        switch (opt) {
        case 'A':
            config.context_after = strtol(optarg, &endptr, 10);
            if (endptr[0] != '\0') {
                fprintf(stderr, "rsv2ce: invalid flag value for -A\n\n%s", usage);
                return 1;
            }
            break;
        case 'B':
            config.context_before = strtol(optarg, &endptr, 10);
            if (endptr[0] != '\0') {
                fprintf(stderr, "rsv2ce: invalid flag value for -B\n\n%s", usage);
                return 1;
            }
            break;
        case 'C':
            config.context_chapter = true;
            break;
        case 'l':
            list_books = true;
            break;
        case 'h':
            printf("%s", usage);
            return 0;
        case '?':
            fprintf(stderr, "rsv2ce: invalid flag -%c\n\n%s", optopt, usage);
            return 1;
        }
    }

    if (list_books) {
        for (int i = 0; i < rsv2ce_books_length; i++) {
            rsv2ce_book *book = &rsv2ce_books[i];
            printf("%s (%s)\n", book->name, book->abbr);
        }
        return 0;
    }

    struct winsize ttysize;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ttysize) == 0 && ttysize.ws_col > 0) {
        config.maximum_line_length = ttysize.ws_col;
    }

    signal(SIGPIPE, SIG_IGN);

    if (argc == optind) {
        using_history();
        while (true) {
            char *input = readline("rsv2ce> ");
            if (input == NULL) {
                break;
            }
            add_history(input);
            rsv2ce_ref *ref = rsv2ce_newref();
            int success = rsv2ce_parseref(ref, input);
            free(input);
            if (success == 0) {
                rsv2ce_render(ref, &config);
            }
            rsv2ce_freeref(ref);
        }
    } else {
        char *ref_str = str_join(argc-optind, &argv[optind]);
        rsv2ce_ref *ref = rsv2ce_newref();
        int success = rsv2ce_parseref(ref, ref_str);
        free(ref_str);
        if (success == 0) {
            rsv2ce_render(ref, &config);
        }
        rsv2ce_freeref(ref);
    }

    return 0;
}

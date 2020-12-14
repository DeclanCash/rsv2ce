# rsv2ce [![AUR](https://img.shields.io/badge/AUR-rsv2ce--git-blue.svg)](https://aur.archlinux.org/packages/rsv2ce-git/)

Read the Word of God from your terminal

## Usage

    usage: rsv2ce [flags] [reference...]

    Flags:
      -A num  number of verses of context after matching verses
      -B num  number of verses of context before matching verses
      -C      show matching verses in context of the chapter
      -l      list books
      -h      show help

    Reference:
        <Book>
            Individual book
        <Book>:<Chapter>
            Individual chapter of a book
        <Book>:<Chapter>:<Verse>[,<Verse>]...
            Individual verse(s) of a specific chapter of a book
        <Book>:<Chapter>-<Chapter>
            Range of chapters in a book
        <Book>:<Chapter>:<Verse>-<Verse>
            Range of verses in a book chapter
        <Book>:<Chapter>:<Verse>-<Chapter>:<Verse>
            Range of chapters and verses in a book

        /<Search>
            All verses that match a pattern
        <Book>/<Search>
            All verses in a book that match a pattern
        <Book>:<Chapter>/<Search>
            All verses in a chapter of a book that match a pattern

## Build

rsv2ce can be built by cloning the repository and then running make:

    git clone https://github.com/DeclanCash/rsv2ce.git
    cd rsv2ce
    make

## License

Public domain

# cfd

The cfd utility transforms and sorts ANSI colorized text while preserving
the original ANSI coloring.

Originally, this program was written to sort and transform the output of
[fd](https://github.com/sharkdp/fd) - at first it only stripped the leading
'./' prefix from the input (there is now a `--strip-cwd-prefix` option to
handle that), but it eventually evolved into a more general "sort" utility
for ANSI colorized text preserves the ANSI coloring.

## Usage:

Sort the output of `fd` case-intensively while preserving color.
```sh
$ fd --color=always | cfd --isort
```

## Installation

The Makefile here is quite bare so to install cfd, you need to run `make` then
place the cfd executable on your PATH.

## Tips

To create a wrapper around `fd` that sorts its output case-insensitively I use
the following shell script and save it as `sfd` on my PATH (I also use this as
my default FZF ctrl-t command despite the fact that FZF uses my own
[fastwalk](https://github.com/charlievieth/fastwalk/)) library by default -
since I like colors).

```sh
#!/bin/sh
set -e
if command -v cfd >/dev/null; then
    \fd --color=always "$@" | cfd --isort
else
    \fd --color=always "$@" | LC_ALL=C sort --ignore-case
fi
```

## Testing

There is a fairly comprehensive [test suite](./scripts/test.bash)
(written in bash of all things) that be invoked via `make test`,
which will also enable clang's address sanitizer.

#!/usr/bin/env python3

import argparse
import fileinput
import sys


# Sort standard input ignoring case. We use this because the sorting
# rules for sort(1) don't exactly match C's strcasecmp.
def sort_stdin(ignore_case=False) -> None:
    with fileinput.input(files=('-')) as f:
        if ignore_case:
            lines = sorted([line for line in f], key=str.lower)
        else:
            lines = sorted([line for line in f])
    for line in lines:
        sys.stdout.write(line)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--ignore-case', action='store_true', default=False)
    sort_stdin(parser.parse_args().ignore_case)
    return 0


if __name__ == '__main__':
    sys.exit(main())

# parser.add_argument('--foo', help='foo help')
# args = parser.parse_args()
# parser.add_argument('--foobar', action='store_true')

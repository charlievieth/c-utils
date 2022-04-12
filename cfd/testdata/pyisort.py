#!/usr/bin/env python3

import fileinput
import sys

# Sort standard input ignoring case. We use this because the sorting
# rules for sort(1) don't exactly match C's strcasecmp.
for line in sorted([line for line in fileinput.input()], key=str.lower):
    sys.stdout.write(line)

#!/bin/sh
# Like a "mv" command, but for selected by "fargs" files.
for f in $(fsel l); do mv $f $argv; done
fsel c

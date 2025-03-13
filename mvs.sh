#!/bin/sh
# Like a "mv" command, but for selected by "fargs" files.
for f in $(fsel o); do mv $f $argv; done
fsel c

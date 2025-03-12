#!/bin/sh
# Like a "mv" command, but for selected by "fargs" files.
for f in $(fargs o); do mv $f $argv; done

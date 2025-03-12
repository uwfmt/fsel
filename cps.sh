#!/bin/sh
# Like a "cp" command, but for selected by "fargs" files.
for f in $(fargs o); do cp -a $f $argv; done

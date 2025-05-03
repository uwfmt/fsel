#!/bin/sh
# Like a "cp" command, but for selected by "fargs" files.
for f in $(fsel l); do cp -a $f $argv; done

#!/bin/sh
# Like a "rm" command, but for selected by "fargs" files.
# Dangerous! By default uses option for recursive removal.
for f in $(fargs o); do rm -rf $f $argv; done

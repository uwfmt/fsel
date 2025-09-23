# Like a "cp" command, but for selected by "fargs" files.
function cps
    for f in (fsel)
        cp -a $f $argv
    end
end

# Like a "mv" command, but for selected by "fargs" files.
function mvs
    for f in (fsel)
        mv $f $argv
    end
    fsel -c
end

# Like a "rm" command, but for selected by "fargs" files.
# Dangerous! By default uses option for recursive removal.
function rms
    for f in (fsel)
        rm -rf $f $argv
    end
    fsel -c
end

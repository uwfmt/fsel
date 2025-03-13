# Like a "cp" command, but for selected by "fargs" files.
function cps
    for f in (fsel out)
        cp -a $f $argv
    end
end

# Like a "mv" command, but for selected by "fargs" files.
function mvs
    for f in (fsel out)
        mv $f $argv
    end
    fsel clean
end

# Like a "rm" command, but for selected by "fargs" files.
# Dangerous! By default uses option for recursive removal.
function rms
    for f in (fsel out)
        rm -rf $f $argv
    end
    fsel clean
end

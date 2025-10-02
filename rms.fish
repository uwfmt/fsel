# Like a "rm" command, but for selected by "fsel" files.
# Dangerous! By default uses option for recursive removal.
function rms
    for f in (fsel)
        rm -rf $f $argv
    end
    fsel -c
end


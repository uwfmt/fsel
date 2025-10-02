# Like a "mv" command, but for selected by "fsel" files.
function mvs
    for f in (fsel)
        mv $f $argv
    end
    fsel -c
end


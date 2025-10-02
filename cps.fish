# Like a "cp" command, but for selected by "fsel" files.
function cps
    for f in (fsel)
        cp -a $f $argv
    end
end


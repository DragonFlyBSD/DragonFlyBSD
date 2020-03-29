#!/bin/sh

# The awk script used in this file was written by vgersh99

if [ "$#" = "0" ]; then
	DIR="."
else
	DIR=$*
fi

for _dir in $DIR; do
if [ ! -d $_dir ]; then
	echo "$_dir: No such directory"
	continue
fi

echo $_dir | sed -E 's/\/+$//'
find -s ${_dir}/ | awk '
BEGIN {
        FS = "/";
        i = 0;
}

function fineprint(branches)
{
        gsub(" ","    ", branches);
        gsub("[|]","&   ", branches);
        gsub("`","&-- ", branches);
        gsub("[+]","|-- ", branches);
        return branches;
}

function mktree(number,branches,   tnumber)
{

        if (number > NR) {
                return number - 1;
        }

        tnumber = number;

        while (array["shift", number] < array["shift", tnumber + 1]) {
                tnumber = mktree(tnumber + 1, branches" ");
                if (tnumber == NR) break;
        }

        if (array["shift", number] == array["shift", tnumber + 1]) {
                array["slip", number] = branches"+";
        }

        if ((array["shift", number] > array["shift", tnumber + 1]) \
                || tnumber == NR) {
                array["slip", number] = branches"`";
        }


        return tnumber;
}

{
        array["shift", NR] = NF;
        array["name", NR] = $(NF);
}

END {
        if (EXIT)
                exit EXIT;

        for (i = 1; i <= NR; i++) {
                i = mktree(i, "");
        }

        for (i = 1; i <= NR; i++) {

                if (i > 1) {
                        lprev = length(array["slip", i - 1]);
                        lcurr = length(array["slip", i]);
                        if (lprev > lcurr - 1)
                                legacy = substr(array["slip", i - 1], 0, \
                                                lcurr - 1);
                        else
                                legacy = array["slip", i-1];
                        tail = substr(array["slip", i], length(legacy) + 1 , \
                                        lcurr - length(legacy));
       			gsub("[+]", "|", legacy);
                        gsub("`", " ", legacy);
                        array["slip", i] = (legacy)(tail);
                }

                printf "%s%s\n", fineprint(array["slip", i]), \
                                array["name", i];

        }
}
' | grep -v '^|-- $'
done


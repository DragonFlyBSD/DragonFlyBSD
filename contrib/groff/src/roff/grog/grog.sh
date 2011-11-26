#! /bin/sh
# grog -- guess options for groff command
# Like doctype in Kernighan & Pike, Unix Programming Environment, pp 306-8.

# Source file position: <groff-source>/src/roff/grog/grog.sh
# Installed position: <prefix>/bin/grog

# Copyright (C) 1993, 2006, 2009 Free Software Foundation, Inc.
# Written by James Clark, maintained by Werner Lemberg.
# Rewritten by and put under GPL Bernd Warken.

# This file is part of `grog', which is part of `groff'.

# `groff' is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License (GPL) as published
# by the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# `groff' is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

########################################################################
Last_Update='5 Jan 2009'
########################################################################

soelim=@g@soelim

opts=
mopt=
nr=0
sp="([	 ]|$)"
double_minus=0
was_minus=0
filespec=
had_filespec=0

for arg
do
  if test _"${double_minus}"_ = _1_
  then
    had_filespec=1
    if test -f "${arg}" && test -r "${arg}"
    then
      case "$arg" in
      *" "*)
        eval filespec="'${filespec} '"'\"'"'${arg}'"'\"'
        ;;
      *)
        eval filespec="'${filespec} ${arg}'"
        ;;
      esac
    else
      echo "grog: ${arg} is not a readable file.">&2
    fi
    continue
  fi
  case "$arg" in
  --)
    double_minus=1
    ;;
  -)
    # omit several -
    if test _"${was_minus}"_ = _0_
    then
      was_minus=1
      filespec="${filespec} -"
    fi
    ;;
  -C)
    sp=; opts="$opts -C";
    ;;
  -v|--v|--ve|--ver|--vers|--versi|--versio|--version)
    echo \
"Shell version of GNU grog of $Last_Update in groff version @VERSION@";
    exit 0
    ;;
  -h|--h|--he|--hel|--help)
    cat <<EOF

usage: grog [option]... [--] [filespec]...

"filespec" is either the name of an existing, readable file or "-" for
standard input.  If no "filespec" is specified, standard input is
assumed automatically.

"option" is either a "groff" option or one of these:

-C            compatibility mode
-h --help     print this uasge message and exit
-v --version  print version information and exit

"groff" options are appended to the output, "-m" options are checked.

EOF
    exit 0
    ;;
  -m*)
    if test _"$nr"_ = _0_
    then
      nr=1
      mopt="$arg"
    else
      echo "grog: several -m* options are not allowed." >&2
      mopt=
    fi
    ;;
  -*)
    opts="$opts $arg"
    ;;
  *)
    had_filespec=1
    if test -f "${arg}" && test -r "${arg}"
    then
      case "$arg" in
      *" "*)
        eval filespec="'${filespec} '"'\"'"'${arg}'"'\"'
        ;;
      *)
        eval filespec="'${filespec} ${arg}'"
        ;;
      esac
    else
      echo "grog: ${arg} is not a readable file.">&2
    fi
    ;;
  esac
done
if test _"${filespec}"_ = __ && test _"${had_filespec}"_ = _0_
then
  filespec='-'
fi
if test _"${filespec}"_ = __
then
  exit 1
fi
if test "${double_minus}" = 1
then
  eval files="'-- ${filespec}'"
else
  eval files="'${filespec}'"
fi
files=`echo $files|sed s/\"/\'/g`


eval sed "'s/[ 	]*$//'" '--' "${filespec}" \
| sed 's/^[ 	]*begin[ 	][ 	]*chem$/.cstart/' \
| sed 's/^[.'"'"'][ 	]*/./' \
| @EGREP@ -h \
  "^\.(\[|\])|cstart|((P|PS|[PLI]P|[pnil]p|sh|Dd|Tp|Dp|De|Cx|Cl|Oo|.* Oo|Oc|.* Oc|NH|TL|TS|TE|EQ|TH|SH|so|\[|R1|GS|G1|PH|SA)$sp)" \
| sed '/^\.so/s/^.*$/.SO_START\
&\
.SO_END/' \
| $soelim \
| @EGREP@ \
    '^\.(cstart|P|PS|[PLI]P|[pnil]p|sh|Dd|Tp|Dp|De|Cx|Cl|Oo|.* Oo|Oc|.* Oc|NH|TL|TS|TE|EQ|TH|TL|NH|SH|\[|\]|R1|GS|G1|PH|SA|SO_START|SO_END)' \
| awk '
/^\.SO_START$/ { so = 1 }
/^\.SO_END$/ { so = 0 }
/^\.cstart$/ { chem++ }
/^\.TS/ { tbl++; in_tbl = 1; if (so > 0) soelim++; }
/^\.TE/ { in_tbl = 0 }
/^\.PS([ 0-9.<].*)?$/ { pic++; if (so > 0) soelim++ }
/^\.EQ/ { eqn++; if (so > 0) soelim++ }
/^\.R1/ { refer++; if (so > 0) soelim++ }
/^\.\[/ {refer_start++; if (so > 0) soelim++ }
/^\.\]/ {refer_end++; if (so > 0) soelim++ }
/^\.GS/ { grn++; if (so > 0) soelim++ }
/^\.G1/ { grap++; pic++; if (so > 0) soelim++ }
/^\.TH/ { if (in_tbl != 1) TH++ }
/^\.PP/ { PP++ }
/^\.TL/ { TL++ }
/^\.NH/ { NH++ }
/^\.[IL]P/ { ILP++ }
/^\.P$/ { P++ }
/^\.SH/ { SH++ }
/^\.(PH|SA)/ { mm++ }
/^\.([pnil]p|sh)/ { me++ }
/^\.Dd/ { mdoc++ }
/^\.(Tp|Dp|De|Cx|Cl)/ { mdoc_old++ }
/^\.(O[oc]|.* O[oc]( |$))/ {
  sub(/\\\".*/, "")
  gsub(/\"[^\"]*\"/, "")
  sub(/\".*/, "")
  sub(/^\.Oo/, " Oo ")
  sub(/^\.Oc/, " Oc ")
  sub(/ Oo$/, " Oo ")
  sub(/ Oc$/, " Oc ")
  while (/ Oo /) {
    sub(/ Oo /, " ")
    Oo++
  }
  while (/ Oc /) {
    sub(/ Oc /, " ")
    Oo--
  }
}
/^\.(PRINTSTYLE|START)/ { mom++ }

END {
  if (chem > 0) {
    printf "chem %s | ", files
    pic++    
    files = ""
  }
  printf "groff"
  refer = refer || (refer_start && refer_end)
  if (pic > 0 || tbl > 0 || grn > 0 || grap > 0 || eqn > 0 || refer > 0) {
    printf " -"
    if (soelim > 0) printf "s"
    if (refer > 0) printf "R"
    if (grn > 0) printf "g"
    if (grap > 0) printf "G"
    if (pic > 0) printf "p"
    if (tbl > 0) printf "t"
    if (eqn > 0) printf "e"
  }
  mnr = 0
  m = ""
  is_man = 0
  is_mm = 0
  is_mom = 0
  # me
  if (me > 0) {
    mnr++; m = "-me"; printf " -me"
  }
  # man
  if (SH > 0 && TH > 0) {
    mnr++; m = "-man"; printf " -man"; is_man = 1
  }
  # mom
  if (mom > 0) {
    mnr++; m = "-mom"; printf " -mom"; is_mom = 1
  }
  # mm
  if ( mm > 0 || (P > 0 && is_man == 0) ) {
    mnr++; m = "-mm"; printf " -mm"; is_mm = 1
  }
  # ms
  if ( NH > 0 || (TL > 0 && is_mm == 0) || (ILP > 0 && is_man == 0) ||
       (PP > 0 && is_mom == 0 && is_man == 0) ) {
    # .TL also occurs in -mm, .IP and .LP also occur in -man
    # .PP also occurs in -mom and -man
    mnr++; m = "-ms"; printf " -ms"
  }
  # mdoc
  if (mdoc > 0) {
    mnr++
    if (mdoc_old > 0 || Oo > 0) {
      m = "-mdoc-old"; printf " -mdoc-old"
    }
    else {
      m = "-mdoc"; printf " -mdoc"
    }
  }

  if (mnr == 0) {
    if (mopt != "")
      printf "%s", mopt
  }

  if (opts != "")
    printf "%s", opts
  if (files != "")
    printf " %s", files
  print ""

  if (mnr == 1) {
    if (mopt != "" && m != mopt)
      printf "grog: wrong option %s\n", mopt | "cat 1>&2"
  }
  if (mnr >= 2) {
    err = "grog: error: there are several -m* arguments"
    printf "%s\n", err | "cat 1>&2"
    exit mnr
  }
}' "opts=$opts" "mopt=$mopt" "files=$files" -

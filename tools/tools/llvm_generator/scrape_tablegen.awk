# must set SRCDIR externally

BEGIN {
  scanning=0;
  srcs=0;
  print "# Automatically generated.  Do not edit!";
  print
}

{
  if (scanning) {
     keep_going = (NF > 0);
     if (keep_going && ($1 == "#")) {
        keep_going = 0;
     }
     if (keep_going && ($1 == ")")) {
        keep_going = 0;
        scanning = 0;
     }
     if (keep_going && ($1 == "LINK_LIBS")) {
        keep_going = 0;
     }
     if (keep_going) {
        if (srcs) {
           print "\t" $1 " \\";
        } else {
           srcs=1;
           print "SRCS=\t" $1 " \\";
        }
     }
  } else if (substr($1,1,13) ==  "add_tablegen(") {
       scanning=1
       rest=substr($1,14)
       space=index(rest," ")
       prog=substr(rest,space + 1)
       print "PROG_CXX=\t" prog "\n"
  }
}

END {
  print "\nSRCDIR= " SRCDIR
}

# must set SRCDIR externally

BEGIN {
  scanning=0;
  incs=0;
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
     if (keep_going) {
        if (incs) {
           print "\t" $1 " \\";
        } else {
           incs=1;
           print "INCS=\t" $1 " \\";
        }
     }
   } else if (substr($1,1,9) == "set(files") {
       scanning=1
   } else if (substr($1,1,15) ==  "clang_tablegen(") {
       hdr=substr($1,16)
       print "\nGENINCS+=\t" hdr;
   }
}

END {
  print "\nSRCDIR= " SRCDIR
}

# helper script to check ldd(1) output for a given library
# objects detected to be linked with the library are printed
# usage: ldd <object> ... | awk -v library=<path-to-library>

BEGIN {
	FS = ":";
	object = "";
	library = " => " library " (";
}

{
	if ($0 ~ /:$/) {
		object = $1;
	} else if (object != "") {
		if (index($0, library) > 0) {
			print object;
			object = "";
		}
	}
}

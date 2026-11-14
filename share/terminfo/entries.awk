# Print "x/xterm" for every name in a terminfo.src entry header.
# tic installs each name under a directory named after its first character.

/^[^#[:space:]]/ {
	sub(/,[[:space:]]*$/, "")
	n = split($0, field, "|")
	if (n == 1) {
		print substr(field[1], 1, 1) "/" field[1]
	} else {
		# The last field of a header is a description, not a name.
		for (i = 1; i < n; i++) {
			name = field[i]
			gsub(/[[:space:]]/, "", name)
			print substr(name, 1, 1) "/" name
		}
	}
}


BIN=	oclo oclo_errors ocloexec_verify
CFLAGS=	-std=gnu11 -fno-common -Wmissing-include-dirs -Wsystem-headers -Wall -Wformat-security -Winit-self -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wold-style-definition  -Wno-pointer-sign -Wno-maybe-uninitialized -Wno-unused-parameter -Wno-format-overflow -Wno-format-truncation -Wno-stringop-truncation -Wno-stringop-overflow -Wno-unused-but-set-variable -Wno-unused-but-set-parameter -Wno-unused-value -Wno-implicit-fallthrough

.PHONY: all
all:	$(BIN)

oclo: $@.c
	$(CC) -o $@ $(CFLAGS) $<

oclo_errors: $@.c
	$(CC) -o $@ $(CFLAGS) $<

ocloexec_verify: $@.c
	$(CC) -o $@ $(CFLAGS) $<

clean:
	rm -f $(BIN)

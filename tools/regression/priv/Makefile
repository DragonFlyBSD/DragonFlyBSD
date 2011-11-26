compile: test_acct

run: compile
	./test_acct

clean:
	rm *.o test_acct

test.o: test.c
	${CC} -c test.c

test_acct: test_acct.c test.o test.h
	${CC} -o test_acct test.o test_acct.c

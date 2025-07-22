all:
	gcc cookie_test.c -lssl -lcrypto -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube
clean:
	rm metube
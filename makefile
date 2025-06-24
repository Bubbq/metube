all:
	gcc test.c -lssl -lcrypto -lcurl -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube

clean:
	rm metube
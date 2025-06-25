all:
	gcc metube.c -lcurl -lssl -lcrypto -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube
clean:
	rm metube
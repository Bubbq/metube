all:
	gcc metube.c -lssl -lcrypto -lcurl -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube
	# gcc test.c -lssl -lcrypto -lcurl -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube
clean:
	rm metube
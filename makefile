all:
	gcc metube.c -lssl -lcrypto -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube
final:
	gcc test.c -lssl -lcrypto -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube
clean:
	rm metube
	rm *.json
	rm *.txt
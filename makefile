all:
	gcc texture_cache.c timer.c buffer.c metube.c -lssl -lcrypto -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube
clean:
	rm metube
	rm *.json
	rm *.txt
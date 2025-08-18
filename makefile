all:
	gcc buffer.c connection.c https_utils.c json_utils.c list.c texture_cache.c thread_utils.c timer.c utils.c main.c -lssl -lcrypto -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -fsanitize=address -o metube

clean:
	rm metube
	rm *.json
	rm *.txt

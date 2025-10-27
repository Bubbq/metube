all:
	gcc innertube/*.c json_utils.c thumbnails.c ssl_utils.c texture_cache.c thread_utils.c timer.c utils.c main.c -Llib -llinked_list -lbuffer -lconnection -lssl -lcrypto -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -fsanitize=leak -o metube

clean:
	rm metube
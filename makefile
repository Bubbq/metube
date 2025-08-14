all:
	gcc thread_context.c query_ops.c thumbnail_loader.c client_context.c user_data.c threads.c yt_parse.c list.c search_result.c thread_task.c raw_thumbnail.c media_type.c json_utils.c https_utils.c request_config.c query.c connection.c utils.c texture_cache.c timer.c buffer.c metube.c -lssl -lcrypto -lcjson -I raylib/src/ raylib/src/libraylib.a -lm -Wall -o metube
test:
	gcc search_result.c list.c test.c -Wall -o metube

clean:
	rm metube
	rm *.json
	rm *.txt
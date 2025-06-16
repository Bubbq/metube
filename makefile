all:
	gcc metube.c list.c api.c -o metube -lcurl -lcjson -lraylib -lm -Wall
	# gcc test.c -o metube -lcurl -lcjson

clean:
	rm metube
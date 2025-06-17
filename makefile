all:
	gcc metube.c -lcurl -lcjson -lraylib -lm -Wall -o metube

clean:
	rm metube
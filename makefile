all:
	gcc metube.c -o metube -lcurl -lcjson -Wall

clean:
	rm metube
	clear
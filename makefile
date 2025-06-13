all:
	gcc metube.c list.c api.c -o metube -lcurl -lcjson -Wall

clean:
	# rm *.json 
	rm metube
	clear
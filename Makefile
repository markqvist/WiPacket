test:
	mkdir -p bin
	gcc main.c -Wall if_helper.c -o bin/wipacket

debug:
	mkdir -p bin
	gcc main.c if_helper.c -g -Wall -o bin/wipacket

clean:
	rm -r bin

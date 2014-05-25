test:
	mkdir -p bin
	gcc main.c -Wall if_helper.c -o bin/wipacket

clean:
	rm -r bin
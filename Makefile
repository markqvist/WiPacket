test:
	gcc main.c if_helper.c -o bin/wipacket -liw

clean:
	rm wipacket
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

char *file_name;
char *socket_path;
bool verbose = false;

int wiSocket;
FILE *fd;

#define TIMEOUT_MSEC 1500
#define HEADER_SIZE 4 // Size of unsigned long
#define FRAGMENT_SIZE 1482
#define PACKET_SIZE HEADER_SIZE+FRAGMENT_SIZE
char buffer[PACKET_SIZE];
unsigned long fragment;
unsigned long ack;

int main(int argc, char **argv) {
    extern char *optarg;
    extern int optind;

    int o, err = 0;
    while ((o = getopt(argc, argv, "v::")) != -1) {
        switch (o) {
            case 'v':
                verbose = true;
                break;

            case '?':
                err = 1;
                break;
        }
    }

    if ((optind+1) > argc || err) {
        if ((optind+2) > argc) {
            printf("No file specified\n");
        }
        if ((optind+1) > argc) {
            printf("No WiPacket socket specified\n");
        }
        if (err) {
            //printf("Invalid option specified\n");
        }
        printf("Receives a file using WiPacket\n");
        printf("Usage: rxfile [-v] socket file\n");
        exit(1);
    } else {
        socket_path = argv[optind-1];
        file_name = argv[optind];
    }

    printf("File %s\n",file_name);
    printf("Socket %s\n",socket_path);

    fd = fopen(file_name, "w");
    if (fd == NULL) {
        printf("Could not output file\n");
        exit(1);
    }

    struct sockaddr_un remote;
    wiSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (wiSocket == -1) {
        printf("Could not create socket\n");
        exit(1);
    }

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, socket_path);
    printf("Opening socket at %s\n", remote.sun_path);
    int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(wiSocket, (struct sockaddr*)&remote, len) == -1) {
        perror("connect");
        printf("Could not connect to WiPacket socket\n");
        exit(1);
    }

    printf("Connected to WiPacket\n");

    fd_set wiSocketSet;
    struct timeval timeout;

    fragment = 0;
    ack = 0;
    int nRead;
    bool done = false;
    while (!done) {

        FD_ZERO(&wiSocketSet);
        FD_SET(wiSocket, &wiSocketSet);
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MSEC*1000;
        
        int socketReady = select(wiSocket+1, &wiSocketSet, NULL, NULL, &timeout);
        if (socketReady < 0) {
            printf("Error reading from WiPacket while waiting for data\n");
            exit(1);
        }

        if (socketReady == 1) {
            nRead = recv(wiSocket, buffer, PACKET_SIZE, 0);
            if (nRead != 0) {
                printf("Read %d bytes\n", nRead);
                memcpy(&fragment, buffer, HEADER_SIZE);

                if (fragment == ack+1) {
                    printf("Got fragment %lu\n", fragment);
                    ack++;
                    if ((fwrite(buffer+HEADER_SIZE, 1, nRead - HEADER_SIZE, fd)) != nRead - HEADER_SIZE) {
                        printf("Error while writing received data to file\n");
                        exit(1);
                    }

                    memcpy(buffer, &ack, HEADER_SIZE);
                    if (send(wiSocket, buffer, HEADER_SIZE, 0) < 0) {
                        printf("Error writing to WiPacket socket while sending ACK\n");
                        exit(1);
                    }
                }
            }
        }


    }

    return 0;
}
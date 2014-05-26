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

#define TIMEOUT_MSEC 25
#define HEADER_SIZE 4 // Size of unsigned long
#define FRAGMENT_SIZE 1482
#define PACKET_SIZE HEADER_SIZE+FRAGMENT_SIZE
char fBuffer[PACKET_SIZE];
char sBuffer[PACKET_SIZE];
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
        printf("Sends a file using WiPacket\n");
        printf("Usage: txfile [-v] socket file\n");
        exit(1);
    } else {
        socket_path = argv[argc-2];
        file_name = argv[argc-1];
    }

    fd = fopen(file_name, "r");
    if (fd == NULL) {
        printf("Could not input file\n");
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
    int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(wiSocket, (struct sockaddr*)&remote, len) == -1) {
        perror("connect");
        printf("Could not connect to WiPacket socket\n");
        exit(1);
    }

    if (verbose) printf("Connected to WiPacket\n");
    printf("Sending file...\n");

    fd_set wiSocketSet;
    struct timeval timeout;

    fragment = 0;
    ack = 0;
    bool gotAck;
    int fRead;
    int sRead;
    while ((fRead = fread(fBuffer+HEADER_SIZE, 1, FRAGMENT_SIZE, fd))) {
        fragment++;
        memcpy(fBuffer, &fragment, HEADER_SIZE);

        gotAck = false;
        while (!gotAck) {

            if (send(wiSocket, fBuffer, fRead, 0) < 0) {
                printf("Error writing to WiPacket socket\n");
                exit(1);
            }
            if (verbose) printf("Sent fragment %lu\n", fragment);

            FD_ZERO(&wiSocketSet);
            FD_SET(wiSocket, &wiSocketSet);
            timeout.tv_sec = 0;
            timeout.tv_usec = TIMEOUT_MSEC*1000;
            
            int socketReady = select(wiSocket+1, &wiSocketSet, NULL, NULL, &timeout);
            if (socketReady < 0) {
                printf("Error reading from WiPacket while waiting for ACK\n");
                exit(1);
            }

            if (socketReady == 1) {
                sRead = recv(wiSocket, sBuffer, PACKET_SIZE, 0);
                if (sRead != 0) {
                    memcpy(&ack, sBuffer, HEADER_SIZE);

                    if (ack == fragment) {
                        gotAck = true;
                    } else {
                        if (verbose) printf("Got a response, but not correct ACK\n");
                    }
                }
            }

        }
        if (verbose) printf("Got ACK for fragment %lu, moving on...\n", fragment);
    }

    // File transmitted, send EOF packet
    fragment = 0;
    memcpy(fBuffer, &fragment, HEADER_SIZE);
    gotAck = false;

    while (!gotAck) {

        if (send(wiSocket, fBuffer, HEADER_SIZE, 0) < 0) {
            printf("Error writing to WiPacket socket\n");
            exit(1);
        }
        if (verbose) printf("Sent EOF packet\n");

        FD_ZERO(&wiSocketSet);
        FD_SET(wiSocket, &wiSocketSet);
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MSEC*1000;
        
        int socketReady = select(wiSocket+1, &wiSocketSet, NULL, NULL, &timeout);
        if (socketReady < 0) {
            printf("Error reading from WiPacket while waiting for ACK\n");
            exit(1);
        }

        if (socketReady == 1) {
            sRead = recv(wiSocket, sBuffer, PACKET_SIZE, 0);
            if (sRead != 0) {
                memcpy(&ack, sBuffer, HEADER_SIZE);

                if (ack == fragment) {
                    gotAck = true;
                } else {
                    if (verbose) printf("Got a response, but not correct ACK\n");
                }
            }
        }

    }
    ///////////////////////////////////////

    printf("File transmitted\n");

    fclose(fd);
    return 0;
}

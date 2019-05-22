#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <netinet/in.h>

#include "if_helper.h"

// Forward declarations
void init();
void registerSockets();
void configureInterface();
void interfaceInfo(int sd, struct ifreq *req);
void prepeareBroadcastHeader();
bool transmit(char *payload, size_t len);
void cleanup();
void sigHandler(int s);
bool protocolIdMatch(char *buffer);
bool notMine(char *buffer);

// Domain socket path & descriptor
#define SOCKET_PATH "./wipacket.sock"
char *socketPath;
int domainSocket;

// Ethernet protocol ID
#define PROTOCOL_IDENTIFIER 0x9f77

// Max payload length
#define PAYLOAD_LENGTH 1486

// ESSID and frequency for network
#define ESSID "WiPacket"
#define FREQUENCY 2412
static char *essid;
static int frequency;

// The ethernet broadcast address
const unsigned char broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// Socket descriptor for network access
static int netSocket;
// Interface name
static char *if_name;
static size_t if_name_len;
// Interface index of selected interface
static int if_index;
// Hardware (MAC) address of selected interface
static unsigned char hw_addr[ETHER_ADDR_LEN];
// A sockaddr_ll struct for creating frame headers
static struct sockaddr_ll broadcast = {0};

// Verbose and quiet
bool verbose = false;
bool quiet = false;

// Whether to keep running
bool run = true;

int main(int argc, char **argv) {
    // Get interface name from command line argument
    extern char *optarg;
    extern int optind;

    int eflag = 0;
    int fflag = 0;
    int sflag = 0;
    int rflag = 0; // set if program is in rx
    int o, err, invo = 0;
    while ((o = getopt(argc, argv, "e:f:s:v::q::")) != -1) {
        switch (o) {
            case 'e':
                eflag = 1;
                essid = optarg;
                break;
            case 'f':
                fflag = 1;
                frequency = atoi(optarg);
                break;
            case 's':
                sflag = 1;
                socketPath = optarg;
                break;
            case 'v':
                verbose = true;
                quiet = false;
                break;
            case 'q':
                quiet = true;
                verbose = false;
                break;
            case 'r':
                if (tflag) err = 2;
                rflag = 1;
                break;
            case 't':
                if (rflag) err = 2;
                tflag = 1;
                break;
            case '?':
                invo = o;
                err = 1;
                break;
        }
    }

    if ((optind+1) > argc || err) {
        if ((optind+1) > argc) {
            printf("No interface specified\n");
        }
        switch (err) {
            case 1:
                printf("Invalid option '-%c'\n", invo);
            case 2:
                printf("Impossible to set program both send [-t] and receive [-r]\n");
            //printf("Invalid option specified\n");
        }
        printf("Usage: wipacket [-e essid] [-f frequency] [-s socket_path] [-t|r] [-v] [-q] interface\n");
        exit(1);
    } else {
        if_name = argv[optind];
        if_name_len = strlen(if_name);
    }


    if (!eflag) essid = ESSID;
    if (!fflag) frequency = FREQUENCY;
    if (!sflag) socketPath = SOCKET_PATH;
   
    init();
    configureInterface();
    registerSockets();

    if (!quiet) printf("Interface is ready!\n");

    // Socket select setup
    struct timeval timeout;
    int timeout_msec = 5;
    
    //////////////////////

    char incomingBuffer[PAYLOAD_LENGTH];
    char packetBuffer[PAYLOAD_LENGTH];// = malloc(PAYLOAD_LENGTH);
    while (run) {
        int connection;
        struct sockaddr_un remote;
        socklen_t structLen = sizeof(remote);
        if (verbose) printf("Waiting for connection...\n");
        connection = accept(domainSocket, (struct sockaddr*)&remote, &structLen);
        if (connection == -1) {
            if (run) printf("Error while accepting client connection\n");
            cleanup();
            exit(1);
        } else {
            if (verbose) printf("Accepted client connection\n");
            int dReadLength = 0;
            int nReadLength = 0;
            bool connectionOk = true;

            while (connectionOk) {
                fd_set netSet;
                fd_set domainSet;

                FD_ZERO(&netSet);
                FD_SET(netSocket, &netSet);
                FD_ZERO(&domainSet);
                FD_SET(connection, &domainSet);
                //////////////////////////////////////////////////
                //// Read data from domain socket ////////////////
                timeout.tv_sec = 0;
                timeout.tv_usec = timeout_msec*1000;
                int domainSocketReady = select(connection+1, &domainSet, NULL, NULL, &timeout);
                if (domainSocketReady < 0) {
                    if (verbose) printf("Could not query domain socket status\n");
                }
                if (domainSocketReady == 1) {
                    dReadLength = recv(connection, incomingBuffer, PAYLOAD_LENGTH, 0);
                    if (dReadLength < 0) {
                        if (!quiet) printf("Error while reading from client\n");
                        connectionOk = false;
                        close(connection);
                    } else {
                        if (dReadLength > 0) {
                            transmit(incomingBuffer, dReadLength);
                        }
                        if (dReadLength == 0) {
                            if (verbose) printf("Client disconnect\n");
                            connectionOk = false;
                            close(connection);
                        }
                    }
                }

                //////////////////////////////////////////////////
                //// Read data from net socket ///////////////////
                timeout.tv_sec = 0;
                timeout.tv_usec = timeout_msec*1000;
                int netSocketReady = select(netSocket+1, &netSet, NULL, NULL, &timeout);
                if (netSocketReady < 0) {
                    if (verbose) printf("Could not query net socket status\n");
                }
                if (netSocketReady == 1) {
                    struct sockaddr saddr;
                    int saddr_size = sizeof(saddr);

                    nReadLength = recvfrom(netSocket, packetBuffer, PAYLOAD_LENGTH+14, 0, &saddr, (socklen_t*)&saddr_size);
                    if (nReadLength > 0) {
                        if (protocolIdMatch(packetBuffer) && notMine(packetBuffer)) {
                            if (send(connection, packetBuffer+14, nReadLength-14, 0) < 0) {
                                if (verbose) printf("Error writing packet to domain socket\n");
                                connectionOk = false;
                                close(connection);
                            }
                        }
                    }
                }

            }
        }

    }

    cleanup();
    exit(0);
}

bool protocolIdMatch(char *buffer) {
    if ((PROTOCOL_IDENTIFIER >> 8) == (unsigned char)buffer[12] &&
        (PROTOCOL_IDENTIFIER & 0xff) == (unsigned char)buffer[13]) {
        return true;
    } else {
        return false;
    }

}

bool notMine(char *buffer) {
    if ((unsigned char)buffer[6] != hw_addr[0] ||
        (unsigned char)buffer[7] != hw_addr[1] ||
        (unsigned char)buffer[8] != hw_addr[2] ||
        (unsigned char)buffer[9] != hw_addr[3] ||
        (unsigned char)buffer[10] != hw_addr[4] ||
        (unsigned char)buffer[11] != hw_addr[5]) return true;
    return false;
}

void init() {
    // Register signal handler
    struct sigaction handler;
    handler.sa_handler = sigHandler;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = 0;
    sigaction(SIGINT, &handler, NULL);

    // Open raw AF_PACKET socket
    netSocket = socket(AF_PACKET, SOCK_RAW, htons(PROTOCOL_IDENTIFIER));

    if (netSocket == -1) {
        printf("Error creating network socket. Running as unprivileged user?\n");
        exit(1);
    }

    // Get info for selected interface
    struct ifreq interfaceRequest;
    size_t max_if_name_len = sizeof(interfaceRequest.ifr_name);
    if (if_name_len > max_if_name_len) {
        printf("Interface name is too long\n");
        exit(1);
    }

    // Copy name into interface request
    memcpy(interfaceRequest.ifr_name, if_name, if_name_len);
    interfaceRequest.ifr_name[if_name_len] = 0;

    // Get interface info
    struct ifreq *requestPointer = &interfaceRequest;
    interfaceInfo(netSocket, requestPointer);
    
    //printf("Configuring %s (interface index %d)\n", if_name, if_index);
    if (!quiet) printf("Loading %s (%.2x:%.2x:%.2x:%.2x:%.2x:%.2x if_index=%d)\n" , if_name, hw_addr[0], hw_addr[1], hw_addr[2], hw_addr[3], hw_addr[4], hw_addr[5], if_index);

    prepeareBroadcastHeader();
}

void registerSockets() {
    struct sockaddr_un local;
    int len;

    domainSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (domainSocket == -1) {
        printf("Could not create domain socket\n");
        cleanup();
        exit(1);
    }



    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, socketPath);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    
    if (bind(domainSocket, (struct sockaddr *)&local, len) == -1) {
        printf("Could not bind to domain socket\n");
        cleanup();
        exit(1);
    }

    if (listen(domainSocket, 1) == -1) {
        printf("Unable to start listening on domain socket\n");
        cleanup();
        exit(1);
    }
}

bool transmit(char *payload, size_t len) {
    size_t frameLen = len+2*ETHER_ADDR_LEN+2;
    char *buffer = malloc(frameLen);

    memcpy(buffer, broadcast_addr, ETHER_ADDR_LEN);
    memcpy(buffer+ETHER_ADDR_LEN, &hw_addr, ETHER_ADDR_LEN);
    memcpy(buffer+ETHER_ADDR_LEN*2+2, payload, len);
    memcpy(buffer+ETHER_ADDR_LEN*2, &broadcast.sll_protocol, 2);

    int result = sendto(netSocket, buffer, frameLen, 0, (struct sockaddr*)&broadcast, sizeof(broadcast));

    free(buffer);
    
    if (result == -1) {
        return false;
    } else {
        return true;
    }
}

void interfaceInfo(int sd, struct ifreq *req) {
    // Query for interface index
    if (ioctl(sd, SIOCGIFINDEX, req)==-1) {
        printf("%s\n", strerror(errno));
        exit(1);
    }
    if_index = req->ifr_ifindex;

    // Query for hardware address
    if (ioctl(sd, SIOCGIFHWADDR, req)==-1) {
        printf("%s\n", strerror(errno));
        exit(1);
    }
    unsigned char *mac = (unsigned char *)req->ifr_hwaddr.sa_data;
    memcpy(hw_addr, mac, ETHER_ADDR_LEN);
}

void prepeareBroadcastHeader() {
    broadcast.sll_family = AF_PACKET;
    broadcast.sll_ifindex = if_index;
    broadcast.sll_halen = ETHER_ADDR_LEN;
    broadcast.sll_protocol = htons(PROTOCOL_IDENTIFIER);
    memcpy(broadcast.sll_addr, broadcast_addr, ETHER_ADDR_LEN);
}

void configureInterface() {
    printf("Configuring %s (%.2x:%.2x:%.2x:%.2x:%.2x:%.2x) for ibss\n" , if_name, hw_addr[0], hw_addr[1], hw_addr[2], hw_addr[3], hw_addr[4], hw_addr[5], if_index);
    if_down(if_name);
    if_enable_ibss(if_name);
    if_up(if_name);
    if_mtu(if_name, PAYLOAD_LENGTH+14);
    if_join_ibss(if_name, essid, frequency);
    if_promisc(if_name);
}

void cleanup() {
    close(netSocket);
    close(domainSocket);
    unlink(socketPath);
}

void sigHandler(int s) {
    if (s==2) {
        if (!quiet) printf("\nCaught SIGINT, exiting...\n");
        run = false;
    }
}
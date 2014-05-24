#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <iwlib.h>

#include <net/ethernet.h>
#include <linux/if_packet.h>

#include "if_helper.h"
//#include <linux/if_ether.h>
//#include <linux/if_arp.h>


// Forward declarations
void init();
void configureInterface();
void interfaceInfo(int sd, struct ifreq *req);
void prepeareBroadcastHeader();
void cleanup();

// Ethernet protocol ID
#define PROTOCOL_IDENTIFIER 0x9f77

// ESSID and frequency for network
#define ESSID "wipacket"
#define FREQUENCY 2412

// The ethernet broadcast address
const unsigned char broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// Socket descriptor for network access
static int netSocket;
// Socket for controlling 802.11 hardware
static int iwSocket;
// Interface name
static char *if_name;
static size_t if_name_len;
// Interface index of selected interface
static int if_index;
// Hardware (MAC) address of selected interface
static unsigned char *hw_addr;
// A sockaddr_ll struct for creating frame headers
static struct sockaddr_ll broadcast = {0};

int main(int argc, char **argv) {
    // Get interface name from command line argument
    if (argc > 1) {
        if_name = argv[1];
        if_name_len = strlen(if_name);
    } else {
        printf("No interface specified\nUsage: wipacket [interface]\n");
        exit(1);
    }

    init();
    configureInterface();

    cleanup();
    exit(0);
}

void init() {
    // Open raw AF_PACKET socket
    netSocket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (netSocket == -1) {
        printf("Error creating network socket\n");
        exit(1);
    }

    // Open iw control socket
    iwSocket = iw_sockets_open();
    if (iwSocket == -1) {
        printf("Error creating hardware control socket\n");
        exit(1);
    }

    // Get info for selected interface
    struct ifreq interfaceRequest;
    size_t max_if_name_len = sizeof(interfaceRequest.ifr_name);
    if (if_name_len > max_if_name_len) {
        printf("Interface name is too long (max length is %lu)\n", max_if_name_len);
        exit(1);
    }

    // Copy name into interface request
    memcpy(interfaceRequest.ifr_name, if_name, if_name_len);
    interfaceRequest.ifr_name[if_name_len] = 0;

    // Get interface info
    struct ifreq *requestPointer = &interfaceRequest;
    interfaceInfo(netSocket, requestPointer);
    
    printf("Selecting interface: %s (interface index %d)\n", if_name, if_index);
    printf("Current physical address : %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n" , hw_addr[0], hw_addr[1], hw_addr[2], hw_addr[3], hw_addr[4], hw_addr[5]);

    prepeareBroadcastHeader();
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
    hw_addr = (unsigned char *)req->ifr_hwaddr.sa_data;
}

void prepeareBroadcastHeader() {
    broadcast.sll_family = AF_PACKET;
    broadcast.sll_ifindex = if_index;
    broadcast.sll_halen = ETHER_ADDR_LEN;
    broadcast.sll_protocol = htons(PROTOCOL_IDENTIFIER);
    memcpy(broadcast.sll_addr, broadcast_addr, ETHER_ADDR_LEN);
}

void configureInterface() {
    int status;
    printf("Configuring interface %s...\n", if_name);
    if_down(if_name);
    if_enable_ibss(if_name);
    if_up(if_name);
    if_join_ibss(if_name, ESSID, FREQUENCY);
}

void cleanup() {
    close(netSocket);
    close(iwSocket);
}
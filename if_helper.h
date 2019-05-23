#include <stdio.h>
#include <stdlib.h>

typedef enum ocbbw_e {_5MHZ, _10MHZ} ocbbw_t;

int if_up(char *name);
int if_down(char *name);
int if_mtu(char *name, int mtu);
int if_promisc(char *name);
int if_enable_ibss(char *name);
int if_join_ibss(char *name, char *essid, int frequency);
int if_enable_ocb(char *name);
int if_join_ocb(char *name, int frequency, ocbbw_t bw);
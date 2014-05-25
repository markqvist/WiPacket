#include <stdio.h>
#include <stdlib.h>

int if_up(char *name);
int if_down(char *name);
int if_promisc(char *name);
int if_enable_ibss(char *name);
int if_join_ibss(char *name, char *essid, int frequency);
#include <arpa/inet.h>
#include <errno.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct route {
  uint32_t addr;
  uint32_t mask;
  uint32_t gw;
  int outIfIndex;
  struct route *next;
} route_t;

// global route table
route_t *rt = NULL;

int load_routes();

int route_table_size();

bool lpm_lookup(char *dest, route_t **match);
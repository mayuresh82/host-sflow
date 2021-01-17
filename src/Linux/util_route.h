#include <arpa/inet.h>
#include <errno.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
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
  unsigned char maskLen;
  uint32_t gw;
  int outIfIndex;
  struct route *next;
} route_t;

int load_routes(void);

int route_table_size(void);

int lpm_lookup(uint32_t dst, route_t **match);

#if defined(__cplusplus)
extern "C" {
#endif

#include "util_route.h"

int rtnl_receive(int fd, struct msghdr *msg, int flags) {
  int len;
  do {
    len = recvmsg(fd, msg, flags);
  } while (len < 0 && (errno == EINTR || errno == EAGAIN));
  if (len < 0) {
    perror("Netlink receive failed");
    return -errno;
  }
  if (len == 0) {
    perror("EOF on netlink");
    return -ENODATA;
  }
  return len;
}

static int rtnl_recvmsg(int fd, struct msghdr *msg, char **answer) {
  struct iovec *iov = msg->msg_iov;
  char *buf;
  int len;
  iov->iov_base = NULL;
  iov->iov_len = 0;
  len = rtnl_receive(fd, msg, MSG_PEEK | MSG_TRUNC);
  if (len < 0) {
    return len;
  }
  buf = malloc(len);
  if (!buf) {
    perror("malloc failed");
    return -ENOMEM;
  }
  iov->iov_base = buf;
  iov->iov_len = len;
  len = rtnl_receive(fd, msg, 0);
  if (len < 0) {
    free(buf);
    return len;
  }
  *answer = buf;
  return len;
}

void parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len) {
  memset(tb, 0, sizeof(struct rtattr *) * (max + 1));
  while (RTA_OK(rta, len)) {
    if (rta->rta_type <= max) {
      tb[rta->rta_type] = rta;
    }
    rta = RTA_NEXT(rta, len);
  }
}

static inline int rtm_get_table(struct rtmsg *r, struct rtattr **tb) {
  __u32 table = r->rtm_table;
  if (tb[RTA_TABLE]) {
    table = *(__u32 *)RTA_DATA(tb[RTA_TABLE]);
  }
  return table;
}

int open_netlink() {
  struct sockaddr_nl saddr;
  int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sock < 0) {
    perror("Failed to open netlink socket");
    return -1;
  }
  memset(&saddr, 0, sizeof(saddr));
  saddr.nl_family = AF_NETLINK;
  saddr.nl_pid = getpid();
  if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
    perror("Failed to bind to netlink socket");
    close(sock);
    return -1;
  }
  return sock;
}

int do_route_dump_requst(int sock) {
  struct {
    struct nlmsghdr nlh;
    struct rtmsg rtm;
  } nl_request;
  nl_request.nlh.nlmsg_type = RTM_GETROUTE;
  nl_request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  nl_request.nlh.nlmsg_len = sizeof(nl_request);
  nl_request.nlh.nlmsg_seq = time(NULL);
  nl_request.rtm.rtm_family = AF_INET;
  return send(sock, &nl_request, sizeof(nl_request), 0);
}

int add_route(struct nlmsghdr *nl_header_answer) {
  struct rtmsg *r = NLMSG_DATA(nl_header_answer);
  int len = nl_header_answer->nlmsg_len;
  struct rtattr *tb[RTA_MAX + 1];
  int table;
  char buf[256];
  len -= NLMSG_LENGTH(sizeof(*r));
  if (len < 0) {
    perror("Wrong message length");
    return false;
  }
  parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);
  table = rtm_get_table(r, tb);
  if (r->rtm_family != AF_INET && table != RT_TABLE_MAIN) {
    return false;
  }
  if (!tb[RTA_DST]) {
    return false;
  }
  struct in_addr *rt_in_addr = (struct in_addr *)(RTA_DATA(tb[RTA_DST]));
  route_t *new_route = (route_t *)malloc(sizeof(route_t));
  new_route->addr = rt_in_addr->s_addr;
  new_route->mask = (0xFFFFFFFF >> (32 - r->rtm_dst_len));

  if (tb[RTA_GATEWAY]) {
    struct in_addr *rt_gw = (struct in_addr *)(RTA_DATA(tb[RTA_GATEWAY]));
    new_route->gw = rt_gw->s_addr;
  }
  if (tb[RTA_OIF]) {
    new_route->outIfIndex = *(__u32 *)RTA_DATA(tb[RTA_OIF]);
  }
  new_route->next = rt;
  rt = new_route;
  return 0;
}

int load_routes() {
  int nl_sock = open_netlink();

  if (do_route_dump_requst(nl_sock) < 0) {
    perror("Failed to perfom request");
    close(nl_sock);
    return -1;
  }

  struct sockaddr_nl nladdr;
  struct iovec iov;
  struct msghdr msg = {
      .msg_name = &nladdr,
      .msg_namelen = sizeof(nladdr),
      .msg_iov = &iov,
      .msg_iovlen = 1,
  };
  char *buf;
  int dump_intr = 0;
  int status = rtnl_recvmsg(nl_sock, &msg, &buf);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  int msglen = status;

  while (NLMSG_OK(h, msglen)) {
    if (h->nlmsg_flags & NLM_F_DUMP_INTR) {
      fprintf(stderr, "Dump was interrupted\n");
      free(buf);
      return -1;
    }

    if (nladdr.nl_pid != 0) {
      continue;
    }

    if (h->nlmsg_type == NLMSG_ERROR) {
      perror("netlink reported error");
      free(buf);
    }
    add_route(h);

    h = NLMSG_NEXT(h, msglen);
  }

  free(buf);
  close(nl_sock);
  return status;
}

int route_table_size() {
  int len;
  route_t *temp = rt;
  while (temp != NULL) {
    len++;
    temp = temp->next;
  }
  return len;
}

bool lpm_lookup(uint32_t dest, route_t **match) {
  bool matched = false;
  route_t *temp = rt;
  while (temp != NULL) {
    matched = (temp->addr & mask) == (dest & mask);
    if ((matched) && (temp->mask > (*match)->mask)) {
      *match = temp;
    }
    temp = temp->next;
  }
  return matched;
}
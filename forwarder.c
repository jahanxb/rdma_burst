#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "xfer_rdma.h"
#include "slabs_buffer.h"

#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#endif

#define SPLICE_SIZE (64*1024)

// SOME GLOBALS
struct xfer_data sdata = {
  .port             = 18515,
  .ib_port          = 1,
  .tx_depth         = 16,
  .use_cma          = 1,
  .servername       = NULL,
  .ib_dev           = NULL,
  .cm_channel       = NULL,
  .cm_id            = NULL,
  .local_priv       = NULL,
  .local_priv_size  = 0,
  .remote_priv      = NULL,
  .remote_priv_size = 0
};

struct xfer_data cdata = {
  .port             = 18515,
  .ib_port          = 1,
  .tx_depth         = 16,
  .use_cma          = 1,
  .servername       = NULL,
  .ib_dev           = NULL,
  .cm_channel       = NULL,
  .cm_id            = NULL,
  .local_priv       = NULL,
  .local_priv_size  = 0,
  .remote_priv      = NULL,
  .remote_priv_size = 0
};

pthread_mutex_t total_mutex;
pthread_cond_t report_cond;
pthread_mutex_t report_mutex;
uint64_t total_bytes;
int RUN;
static int page_size;
struct timespec startup;

// some xfer structs
struct mdata {
  uint64_t buflen;
  uint64_t fsize;
  uint32_t slab_order;
  uint32_t slab_parts;
};

struct xfer_config {
  int ssock;
  int csock;

  struct xfer_context *sctx;
  struct xfer_context *cctx;

  unsigned sent;
  unsigned send_queued;

  int fd;
  int pipe[2];

  char *cntl;
  char *host;
  char *fname;
  char *xsp_hop;
  int port;
  int use_sdp;
  int interval;
  int time;
  uint64_t buflen;

  void *buf;
  uint64_t bytes;
  psdSLAB *slab;
  int slab_order;
  int slab_parts;
  int tx_depth;
  double bandwidth;
};

void do_stop() {
  if (RUN)
    RUN = 0;
  else
    exit(-1);
}

void diep(char *s) {
  perror(s);
  exit(1);
}

int socket_client_connect(struct xfer_config *cfg, char *host) {
  struct sockaddr_in serveraddr;
  struct hostent *server;

  int s, slen=sizeof(serveraddr);
  int ai_family;

  if (cfg->use_sdp)
    ai_family = AF_INET_SDP;
  else
    ai_family = AF_INET;

  if ((s=socket(ai_family, SOCK_STREAM, 0)) == -1)
    diep("socket");

  server = gethostbyname(host);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host as %s\n", host);
    diep("gethostbyname");
  }

  bzero(&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = ai_family;
  bcopy(server->h_addr, &serveraddr.sin_addr.s_addr, server->h_length);
  serveraddr.sin_port = htons(cfg->port);

  if (connect(s, (struct sockaddr*)&serveraddr, slen) < 0)
    diep("connect");

  return s;
}

int socket_server_start(struct xfer_config *cfg) {
  struct sockaddr_in serveraddr;
  int lfd;
  int ai_family;

  if (cfg->use_sdp)
    ai_family = AF_INET_SDP;
  else
    ai_family = AF_INET;

  lfd = socket(ai_family, SOCK_STREAM, 0);

  bzero(&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = ai_family;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons(cfg->port);

  if (bind(lfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
    diep("bind");

  listen(lfd, 1024);

  return lfd;
}

#ifndef WITH_XSP
double difftv(struct timeval *start, struct timeval *end) {
  double retval;

  retval = end->tv_sec - start->tv_sec;

  if(end->tv_usec >= start->tv_usec) {
    retval += ((double)(end->tv_usec - start->tv_usec)) / 1e6;
  }
  else {
    retval -= 1.0;
    retval += ((double)(end->tv_usec + 1e6) - start->tv_usec) / 1e6;
  }

  return retval;
}
#else
double difftv(struct timeval *start, struct timeval *end);
#endif

char* print_bytes(uint64_t b, int bits) {
  char ret[64];
  char val = 'B';
  int bb = 1;

  if (bits) {
    bb = 8;
    val = 'b';
  }

  if (b > 1e9)
    sprintf(ret, "%.2f G%c", (double)b/1e9*bb, val);
  else if (b > 1e6)
    sprintf(ret, "%.2f M%c", (double)b/1e6*bb, val);
  else if (b > 1e3*100)
    sprintf(ret, "%.2f K%c", (double)b/1e3*100*bb, val);
  else
    sprintf(ret, "%d %cytes", (int)b, val);

  return strdup(ret);
}

void print_bw(struct timeval *s, struct timeval *e, uint64_t b) {
  uint64_t rate = (uint64_t)b/difftv(s, e);
  printf("[0.0-%.1f sec]\t%14s\t%14s/s\n", difftv(s, e),
         print_bytes(b, 0), print_bytes(rate, 1));
}

int vmsplice_to_fd(struct xfer_config *cfg, int fd, void *buf, size_t len) {
  int n;
  uint64_t bytes_left = len;
  uint64_t send_amt;
  char *rbuf = buf;
  struct iovec iov;

  page_size = SPLICE_SIZE;

  while (bytes_left > 0) {

    if (bytes_left > page_size) {
      send_amt = page_size;
    }
    else {
      send_amt = bytes_left;
    }

    iov.iov_base = rbuf;
    iov.iov_len = send_amt;

    n = vmsplice(cfg->pipe[1], &iov, 1, 0);
    if (n < 0) {
      fprintf(stderr, "vmsplice failed: %s", strerror(errno));
      return -1;
    }

    n = splice(cfg->pipe[0], 0, fd, 0, send_amt, 0);
    if (n < 0) {
      fprintf(stderr, "splice failed: %s", strerror(errno));
      return -1;
    }

    rbuf += n;
    bytes_left -= n;
  }
  total_bytes += len;
  return len;
}

void *time_thread(void *arg) {
  int *time = arg;

  struct timespec sleep_time;
  struct timespec remaining_time;

  sleep_time.tv_sec = *time;
  sleep_time.tv_nsec = 0;

  nanosleep(&sleep_time, &remaining_time);

  RUN = 0;

  pthread_exit(NULL);
}

void *bw_report_thread(void *arg) {
  int *interval = arg;
  int step;

  struct timeval curr_time, prev_time;
  struct timespec sleep_time;
  struct timespec remaining_time;

  uint64_t prev_bytes, diff_bytes;

  pthread_mutex_lock(&report_mutex);
  pthread_cond_wait(&report_cond, &report_mutex);
  pthread_mutex_unlock(&report_mutex);

  sleep_time.tv_sec = *interval;
  sleep_time.tv_nsec = 0;

  step = 0;

  while (1) {
    prev_bytes = total_bytes;
    gettimeofday(&prev_time, NULL);
    nanosleep(&sleep_time, &remaining_time);

    diff_bytes = (total_bytes - prev_bytes);
    gettimeofday(&curr_time, NULL);

    uint64_t rate = (uint64_t)diff_bytes/difftv(&prev_time, &curr_time);
    printf("[%.1f-%.1f sec]\t%14s\t%14s/s\n", (float)step, (float)(step + *interval),
           print_bytes(diff_bytes, 0), print_bytes(rate, 1));
    step += *interval;
  }
}

void *rdma_poll_thread(void *arg) {
  struct xfer_config *cfg = arg;
  XFER_RDMA_buf_handle *hptr;
  XFER_RDMA_poll_info pinfo;
  struct message msg;
  int n;

  while (1) {
    xfer_rdma_wait_os_event(cfg->cctx, &pinfo);

    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg->slab, PSB_CURR);

    msg.type = MSG_DONE;
    n = send(cfg->csock, &msg, sizeof(struct message), 0);
    if (n <= 0) {
      printf("RDMA control channel failed\n");
      break;
    }

    pthread_mutex_lock(&total_mutex);
    cfg->sent--;
    cfg->send_queued -= hptr->local_size;
    pthread_mutex_unlock(&total_mutex);

    psd_slabs_buf_read_swap(cfg->slab, 0);
  }

  pthread_exit(NULL);
}

void rdma_slab_bufs_unreg(struct xfer_config *cfg) {
  XFER_RDMA_buf_handle *handle;
  int buf_count;
  int i;

  buf_count = psd_slabs_buf_get_pcount(cfg->slab);
  for (i=0; i < buf_count; i++) {
    handle = (XFER_RDMA_buf_handle*)psd_slabs_buf_get_priv_data_ind(cfg->slab, i);
    xfer_rdma_unregister_buffer(handle);
  }
}

int rdma_slab_bufs_reg(struct xfer_config *cfg, struct xfer_context *ctx) {
  int buf_count;
  int i;

  // slab buf
  cfg->slab = psd_slabs_buf_create(cfg->slab_order, cfg->slab_parts, 1);
  if (!cfg->slab) {
    fprintf(stderr, "could not allocate SLAB buffer\n");
    return -1;
  }

  buf_count = psd_slabs_buf_get_pcount(cfg->slab);
  printf("Created SLAB buffer with SIZE: %lu PARTITIONS: %d\n",
         psd_slabs_buf_get_size(cfg->slab), buf_count);

  for (i=0; i < buf_count; i++) {
    XFER_RDMA_buf_handle *handle;

    handle = xfer_rdma_alloc_handle();
    if (!handle) {
      fprintf(stderr, "could not allocate RDMA buf handle\n");
      return -1;
    }

    handle->buf = psd_slabs_buf_addr_ind(cfg->slab, i);
    handle->local_size = psd_slabs_buf_get_psize(cfg->slab);
    if (xfer_rdma_register_buffer(ctx, handle) != 0) {
      fprintf(stderr, "could not register buf ptr\n");
      return -1;
    }
    psd_slabs_buf_set_priv_data_ind(cfg->slab, handle, i);
  }

  return 0;
}

void *do_rdma_client(void *arg) {
  struct xfer_config *cfg = arg;
  int i, s, n;
  struct xfer_context *ctx = NULL;
  XFER_RDMA_buf_handle *hptr;
  pthread_t pthr;
  struct message msg;
  struct timeval start_time, end_time;
  struct timespec now;
  uint64_t bytes_sent;
  uint64_t slab_bytes;
  uint64_t bytes_allowed;
  double dtmp;

  struct mdata pdata = {
    .buflen = cfg->buflen,
    .fsize = cfg->bytes,
    .slab_order = cfg->slab_order,
    .slab_parts = cfg->slab_parts
  };

  // connect RDMA control conn
  s = socket_client_connect(cfg, cfg->cntl);

  cfg->csock = s;

  // setup the RDMA connect struct
  cdata.servername = cfg->host;
  cdata.local_priv = &pdata;
  cdata.local_priv_size = sizeof(struct mdata);

  xfer_rdma_init(&cdata);

  recv(s, &msg, sizeof(struct message), MSG_WAITALL);

  ctx = xfer_rdma_client_connect(&cdata);
  if (!ctx) {
    fprintf(stderr, "could not get client context\n");
    goto exit;
  }

  cfg->cctx = ctx;

  // exchange pointers
  for (i = 0; i < cfg->slab_parts; i++) {
    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg->slab, PSB_CURR);

    hptr->ctx = ctx;

    if (xfer_rdma_register_buffer(ctx, hptr) != 0) {
      fprintf(stderr, "could not register buf ptr\n");
      goto exit;
    }

    xfer_rdma_wait_buffer(hptr);
    xfer_rdma_send_done(hptr);
    printf("raddr: %p, laddr: %p, size: %lu\n", hptr->remote_mr->addr,
           hptr->local_mr->addr, hptr->local_size);
    psd_slabs_buf_curr_swap(cfg->slab);
  }

  printf("Metadata exchange complete\n");
  printf("Starting CQ poller\n");

  pthread_create(&pthr, NULL, rdma_poll_thread, cfg);

  clock_gettime(CLOCK_REALTIME, &startup);
  cfg->sent = 0;
  cfg->send_queued = 0;
  RUN = 1;
  while (RUN) {
    // wait for the next available buffer to send
    psd_slabs_buf_wait_curr(cfg->slab, PSB_READ);

    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg->slab, PSB_CURR);

    //printf("sending into %p from %p\n", hptr->remote_mr->addr, hptr->local_mr->addr);

    if (cfg->bandwidth == 0.0) {
      bytes_allowed = 0xFFFFFFFFFFFFFFFF;
    }
    else {
      clock_gettime(CLOCK_REALTIME, &now);
      dtmp = now.tv_nsec - startup.tv_nsec;
      dtmp = dtmp / 1000000000.0;
      dtmp = dtmp + (now.tv_sec - startup.tv_sec);
      dtmp = dtmp * (float)cfg->bandwidth;
      dtmp = dtmp * 1000000.0;
      bytes_allowed = dtmp;
    }

    pthread_mutex_lock(&total_mutex);
    if ((cfg->sent < cfg->slab_parts) &&
        (cfg->sent < cfg->tx_depth) &&
        (total_bytes + cfg->send_queued) < bytes_allowed) {

      //printf("POSTING: %p\n", hptr->local_mr->addr);
      xfer_rdma_post_os_put(&hptr, 1);
      psd_slabs_buf_curr_swap(cfg->slab);

      cfg->send_queued += hptr->local_size;
      cfg->sent++;
    }
    pthread_mutex_unlock(&total_mutex);

    if (cfg->send_queued == 0)
      usleep(100);
  }

exit:
  pthread_exit(NULL);
}

void *do_rdma_server(void *arg) {
  struct xfer_config *cfg = arg;
  int i, s, n, lfd, clilen;
  pthread_t cthr;
  struct mdata *pdata;
  struct xfer_context *ctx;
  XFER_RDMA_buf_handle *hptr;

  struct message msg;
  struct sockaddr_in cliaddr;
  struct timeval start_time, end_time;
  uint64_t bytes_recv;
  uint64_t slab_bytes;

  clilen = sizeof(cliaddr);

  lfd = socket_server_start(cfg);

  printf("Waiting for RDMA control conn...");
  fflush(stdout);
  s = accept(lfd, (struct sockaddr *)&cliaddr, (socklen_t*)&clilen);
  printf("done\n");

  cfg->ssock = s;

  xfer_rdma_init(&sdata);

  // sync with the client
  msg.type = MSG_READY;
  send(s, &msg, sizeof(struct message), 0);

  ctx = xfer_rdma_server_connect(&sdata);
  if (!ctx) {
    fprintf(stderr, "could not get client context\n");
    goto exit;
  }

  // get remote slab info
  pdata = sdata.remote_priv;
  cfg->slab_order = pdata->slab_order;
  cfg->slab_parts = pdata->slab_parts;

  rdma_slab_bufs_reg(cfg, ctx);

  // exchange pointers
  for (i = 0; i < cfg->slab_parts; i++) {
    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg->slab, PSB_CURR);
    xfer_rdma_post_buffer(hptr);
    xfer_rdma_wait_done(hptr);
    psd_slabs_buf_curr_swap(cfg->slab);
  }

  printf("Metadata exchange complete\n");
  printf("Connecting to next hop...\n");

  pthread_create(&cthr, NULL, do_rdma_client, cfg);

  gettimeofday(&start_time, NULL);

  if (cfg->interval)
    pthread_cond_signal(&report_cond);

  bytes_recv = 0;

  while (1) {
    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg->slab, PSB_CURR);

    n = recv(s, &msg, sizeof(struct message), MSG_WAITALL);
    if (n <= 0) {
      fprintf(stderr, "RDMA control conn failed\n");
      diep("recv");
    }

    if (msg.type == MSG_STOP)
      break;

    if (msg.type == MSG_DONE)
    {}

    psd_slabs_buf_write_swap(cfg->slab, 0);

    //psd_slabs_buf_curr_swap(cfg->slab);

    bytes_recv += hptr->local_size;
    total_bytes = bytes_recv;
  }

  gettimeofday(&end_time, NULL);
  print_bw(&start_time, &end_time, bytes_recv);

  rdma_slab_bufs_unreg(cfg);
  xfer_rdma_finalize(&sdata);

  // let the client close first
  n = recv(s, &msg, sizeof(struct message), 0);
  close(s);


  // send a message to next hop
  msg.type = MSG_STOP;
  n = send(cfg->csock, &msg, sizeof(struct message), 0);
  if (n < 0) {
    fprintf(stderr, "RDMA control channel failed\n");
    diep("send");
  }

  xfer_rdma_finalize(&cdata);
  close(cfg->csock);

exit:
  pthread_exit(NULL);
}

int do_rdma_forward(struct xfer_config *cfg) {

  pthread_t sthr, cthr;

  pthread_create(&sthr, NULL, do_rdma_server, cfg);
  pthread_join(sthr, NULL);
}

int do_socket_forward(struct xfer_config *cfg) {
  return 0;
}

// TODO
static void usage(const char *argv0) {
  printf("Usage:\n");
  printf("  %s [options]\n", argv0);
  printf("\n");
  printf("Options:\n");
  printf("  -p, --port=<port>      listen on/connect to port <port> (default 18515)\n");
  printf("  -D, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
  printf("  -I, --ib-port=<port>   use port <port> of IB device (default 1)\n");
  printf("  -l, --length=<size>    size of message to exchange (default 1MB)\n");
  printf("  -t, --tx-depth=<dep>   size of tx queue (default 100)\n");
  printf("  -S, --sl=<sl>          SL (default 0)\n");
  printf("  -b, --bidirectional    measure bidirectional bandwidth (default unidirectional)\n");
  printf("  -i, --interval=<sec>   update interval in seconds\n");
  printf("  -t  --time=<sec>       duration of test in seconds\n");
  printf("  -f  --file=<path>      infile (client) | outfile (server)\n");
  printf("  -z, --sdp              use SDP\n");
  printf("  -r, --rdma             use RDMA\n");
  printf("  -y, --cntl=<host>      RDMA control channel\n");
  printf("  -c, --client=<host>	 connect to destination host (data channel)\n");
  printf("  -s, --server           run as server\n");
  printf("  -o, --order=<exp>      SLAB buffer order (2^x)\n");
  printf("  -a, --parts=<part>     SLAB partitions\n");
  printf("  -x, --xsp_hop=<host>   XSP path signaling\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    exit(-1);
  }

  page_size = sysconf(_SC_PAGESIZE);

  struct xfer_config cfg  = {
    .cntl = NULL,
    .host = "127.0.0.1",
    .fname = NULL,
    .xsp_hop = NULL,
    .port = 9930,
    .use_sdp = 0,
    .bytes = 0,
    .interval = 0,
    .time = 10,
    .buflen = 1024*1024*64,
    .slab_order = 20,
    .slab_parts = 16,
    .tx_depth = 250,
    .bandwidth = 0.0

  };

  pthread_t rthr;
  pthread_t tthr;

  int fd = -1;
  int c;
  int client = 1;
  int use_rdma = 0;
  int len;
  unsigned mult = 1;

  while((c = getopt(argc, argv, "zrsl:p:n:c:i:t:f:D:I:x:o:a:y:d:B:")) != -1) {
    switch(c) {
    case 'l':
      len = strlen(optarg);
      if (optarg[len-1] == 'G') {
        mult = 1024*1024*1024;
        optarg[len-1] = '\0';
      }
      else if (optarg[len-1] == 'M') {
        mult = 1024*1024;
        optarg[len-1] = '\0';
      }
      cfg.buflen = atoi(optarg) * mult;
      break;

    case 'p':
      cfg.port = atoi(optarg);
      break;

    case 's':
      client = 0;
      break;

    case 'c':
      cfg.host = strdup(optarg);
      break;

    case 'z':
      cfg.use_sdp = 1;
      break;

    case 'n':
      cfg.bytes = atol(optarg);
      break;

    case 'r':
      use_rdma = 1;
      break;

    case 'i':
      cfg.interval = atoi(optarg);
      break;

    case 't':
      cfg.time = atoi(optarg);
      break;

    case 'f':
      cfg.fname = strdup(optarg);
      break;

    case 'x':
      cfg.xsp_hop = strdup(optarg);
      break;

    case 'o':
      cfg.slab_order = atoi(optarg);
      break;

    case 'a':
      cfg.slab_parts = atoi(optarg);
      break;

    case 'y':
      cfg.cntl = strdup(optarg);
      break;

    case 'd':
      cfg.tx_depth = atoi(optarg);
      break;

    case 'B':
      cfg.bandwidth = atof(optarg);
      break;

    default:
      usage(argv[0]);
      exit(-1);
    }
  }

  // setup splice pipe
  int ret = pipe(cfg.pipe);
  if (ret < 0) {
    fprintf(stderr, "pipe failed: %s", strerror(errno));
    exit(-1);
  }

  // standard buf
  cfg.buf = memalign(page_size, cfg.buflen * sizeof(char));

  printf("Using a buffer of size %lu with %d partitions of size %lu\n",
         1UL << cfg.slab_order, cfg.slab_parts,
         (1UL << cfg.slab_order)/cfg.slab_parts);

  pthread_mutex_init(&total_mutex, NULL);

  if (cfg.interval) {
    pthread_mutex_init(&report_mutex, NULL);
    pthread_cond_init(&report_cond, NULL);
    pthread_create(&rthr, NULL, bw_report_thread, &cfg.interval);
  }

  if (cfg.time)
    pthread_create(&tthr, NULL, time_thread, &cfg.time);

  if (!cfg.buf && !cfg.slab) {
    fprintf(stderr, "setting cfg buffer failed\n");
    close(fd);
    return -1;
  }

  if (cfg.cntl == NULL)
    cfg.cntl = cfg.host;

  if (use_rdma)
    do_rdma_forward(&cfg);
  else
    do_socket_forward(&cfg);

  if (cfg.interval)
    pthread_cancel(rthr);

  return 0;
}

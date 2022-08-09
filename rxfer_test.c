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
#include <sys/sendfile.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <math.h>
#include <err.h>

#include <dirent.h>



#ifdef HAVE_RDMA
#include "xfer_rdma.h"
#endif

#if defined(linux)
#define HAVE_SETAFFINITY
#include <sched.h>
#endif

#ifndef CPU_SETSIZE
#undef HAVE_SETAFFINITY
#endif

#include "slabs_buffer.h"

#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#endif

#define BLOCK_SIZE (1<<23)

// SOME GLOBALS

struct xfer_data data = {
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
  .remote_priv_size = 0,
  
};
int numo=0;
int explore_dir(char *);

int explore_dir(char *foldername) {
    int count=0;
    DIR *d;
    char *pathdir;
    struct dirent *dir;
    char path[1000];
    strcpy(path,foldername);
    d = opendir(path);
    char full_path[1000];
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            //Condition to check regular file.
            if(dir->d_type==DT_REG){
              ++count;
              
                //full_path[0]='\0';
                // strcat(full_path,path);
                // strcat(full_path,"/");
                // strcat(full_path,dir->d_name);
                //printf("%s\n",full_path);
            }
            
        }
        closedir(d);
    }
    return count;
    
}

static struct timespec startup;
static pthread_cond_t report_cond;
static pthread_mutex_t report_mutex;
static size_t total_bytes;
static size_t send_queued;
static int RUN;
static int sent;
static long page_size;
static long splice_size;
static int fdnull = -1;

#ifdef HAVE_SETAFFINITY
static int ncores = 1;                 /* number of CPU cores */
static cpu_set_t cpu_set;              /* processor CPU set */
#endif

// some xfer structs
struct mdata {
  size_t buflen;
  size_t fsize;
  uint32_t slab_order;
  uint32_t slab_parts;
  
};

struct xfer_config {
  int server;
  int fd;
  int cntl_sock;
  int pipe[2];

  char *cntl;
  char *host;
  char *fname;


  char *xsp_hop;
  int port;
  int use_sdp;
  int interval;
  int time;
  size_t buflen;

  void *buf;
  size_t bytes;
  psdSLAB *slab;
  int slab_order;
  int slab_parts;
  int tx_depth;
  double bandwidth;
  struct xfer_context *ctx;
  int use_splice;
  int use_rdma;
  int affinity;
  int num_files;
};


int mmap_flags;
struct stat stat_buf;
size_t fsize;





  pthread_t rthr;
  pthread_t tthr;

  int fd = -1;
  int c;
  int len;
  unsigned mult = 1;


  struct xfer_config cfg  = {
    .server = 0,
    .cntl = NULL,
    .host = "127.0.0.1",
    .fname = NULL,
    .xsp_hop = NULL,
    .port = 9930,
    .use_sdp = 0,
    .bytes = 0,
    .interval = 0,
    .time = 10,
    .buflen = (1<<24),
    .slab_order = 0,
    .slab_parts = 4,
    .tx_depth = 16,
    .bandwidth = 0.0,
    .ctx = NULL,
    .use_splice = 0,
    .use_rdma = 0,
    .affinity = -1,
    .num_files = 1
  };




char *randstring(size_t length) {

    static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";        
    char *randomString = NULL;

    if (length) {
        randomString = malloc(sizeof(char) * (length +1));

        if (randomString) {            
            for (int n = 0;n < length;n++) {            
                int key = rand() % (int)(sizeof(charset) -1);
                randomString[n] = charset[key];
            }

            randomString[length] = '\0';
        }
    }
    
    return randomString;
}



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

  if ((s=socket(ai_family, SOCK_STREAM, 0)) == -1) {
    
    diep("socket");

  }
  server = gethostbyname(host);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host as %s\n", host);
    diep("gethostbyname");
  }

  bzero(&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = ai_family;
  bcopy(server->h_addr, &serveraddr.sin_addr.s_addr, server->h_length);
  serveraddr.sin_port = htons(cfg->port);

  if (connect(s, (struct sockaddr*)&serveraddr, slen) < 0){
    
    diep("connect");
  
  }

  cfg->cntl_sock = s;
  printf("\n inside socket : %d \n",s);

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
  printf("\n lfd value on socket server start: %d \n",lfd);

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

char* print_bytes(double b, int bits) {
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

void print_bw(struct timeval *s, struct timeval *e, size_t b) {
  double rate = (double)b/difftv(s, e);
  printf("[0.0-%.1f sec]\t%14s\t%14s/s\tbytes: %lu\n", difftv(s, e),
         print_bytes(b, 0), print_bytes(rate, 1), b);
}

int do_sendfile(struct xfer_config *cfg, int ifd, int ofd, size_t len) {
  int n;
  size_t bytes_left = len;
  size_t send_amt;

  while (bytes_left > 0) {

    if (bytes_left > BLOCK_SIZE) {
      send_amt = BLOCK_SIZE;
    }
    else {
      send_amt = bytes_left;
    }

    n = sendfile(ofd, ifd, NULL, send_amt);
    if (n < 0) {
      fprintf(stderr, "sendfile failed: %s\n", strerror(errno));
      return -1;
    }

    bytes_left -= n;
    total_bytes += n;
  }
  return len;
}

int splice_fds(struct xfer_config *cfg, int ifd, int ofd, size_t len) {
  int n;
  size_t bytes_left = len;
  size_t send_amt;

  while (bytes_left > 0) {

    if (bytes_left > splice_size) {
      send_amt = splice_size;
    }
    else {
      send_amt = bytes_left;
    }

    n = splice(ifd, 0, cfg->pipe[1], 0, send_amt, SPLICE_F_MORE);
    if (n < 0) {
      fprintf(stderr, "src splice failed: %s\n", strerror(errno));
      return -1;
    }

    n = splice(cfg->pipe[0], 0, ofd, 0, n, SPLICE_F_MORE);
    if (n < 0) {
      fprintf(stderr, "dst splice failed: %s\n", strerror(errno));
      return -1;
    }

    bytes_left -= n;
    total_bytes += len;
  }
  return len;
}

int vmsplice_to_fd(struct xfer_config *cfg, int fd, void *buf, size_t len) {
  int n;
  size_t bytes_left = len;
  size_t send_amt;
  char *rbuf = buf;
  struct iovec iov;

  while (bytes_left > 0) {

    if (bytes_left > splice_size) {
      send_amt = splice_size;
    }
    else {
      send_amt = bytes_left;
    }

    iov.iov_base = rbuf;
    iov.iov_len = send_amt;

    n = vmsplice(cfg->pipe[1], &iov, 1, SPLICE_F_MORE);
    if (n < 0) {
      fprintf(stderr, "vmsplice failed: %s", strerror(errno));
      return -1;
    }

    n = splice(cfg->pipe[0], 0, fd, 0, send_amt, SPLICE_F_MORE);
    if (n < 0) {
      fprintf(stderr, "splice failed: %s", strerror(errno));
      return -1;
    }

    rbuf += n;
    bytes_left -= n;
    total_bytes += len;
  }
  return len;
}

void *fread_thread(void *arg) {
  struct xfer_config *cfg = arg;
  size_t bytes_read;
  size_t slab_bytes;
  char *slab_buf_addr;
  int n;

  bytes_read = 0;
  while ((bytes_read < cfg->bytes)) {
    slab_bytes = psd_slabs_buf_count_bytes_free(cfg->slab, PSB_WRITE);
    if (slab_bytes == 0) {
      psd_slabs_buf_write_swap(cfg->slab, 0);
      slab_bytes = psd_slabs_buf_count_bytes_free(cfg->slab, PSB_WRITE);
    }

    // get remainder if necessary
    if ((cfg->bytes - bytes_read) < slab_bytes) {
      slab_bytes = page_size;
    }

    slab_buf_addr = psd_slabs_buf_addr(cfg->slab, PSB_WRITE);
    n = read(cfg->fd, slab_buf_addr, slab_bytes);
    if (n < 0)
      diep("read");
    if (n == 0)
      break;
    
    psd_slabs_buf_advance(cfg->slab, n, PSB_WRITE);
    bytes_read += n;
	  
    if (bytes_read == cfg->bytes) {
      psd_slabs_buf_write_swap(cfg->slab, 0);
      break;
    }
  }
  pthread_exit(NULL);
}

void *fwrite_thread(void *arg) {
  struct xfer_config *cfg = arg;
  size_t slab_bytes;
  size_t write_bytes;
  char *slab_buf_addr;
  int n;
  
  while (1) {
    slab_bytes = psd_slabs_buf_count_bytes_free(cfg->slab, PSB_READ);
    if (slab_bytes == 0) {
      psd_slabs_buf_read_swap(cfg->slab, 0);
      slab_bytes = psd_slabs_buf_count_bytes_free(cfg->slab, PSB_READ);

      if (cfg->use_rdma) {
	// send ACK
	struct message msg;
	msg.type = MSG_ACK;
	n = send(cfg->cntl_sock, &msg, sizeof(struct message), 0);
	if (n < 0) {
	  fprintf(stderr, "RDMA control channel failed\n");
	  diep("send");
	}
      }
    }

    // we got signaled with nothing to read, so exit
    if (slab_bytes == 0) {
      break;
    }
    
    // otherwise, determine how much to actually write
    if (slab_bytes < page_size)
      write_bytes = page_size;
    else
      write_bytes = slab_bytes;

    slab_buf_addr = psd_slabs_buf_addr(cfg->slab, PSB_READ);

    if (cfg->use_splice) {
      n = vmsplice_to_fd(cfg, cfg->fd, slab_buf_addr, write_bytes);
      if (n <= 0)
        break;
    }
    else {
      n = write(cfg->fd, slab_buf_addr, write_bytes);
      if (n < 0)
	diep("write");
    }
    psd_slabs_buf_advance(cfg->slab, slab_bytes, PSB_READ);
  }

  n = ftruncate(cfg->fd, cfg->bytes);
  if (n < 0)
    diep("ftruncate");

  pthread_exit(NULL);
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

  size_t prev_bytes, diff_bytes;

  pthread_mutex_lock(&report_mutex);
  pthread_cond_wait(&report_cond, &report_mutex);
  pthread_mutex_unlock(&report_mutex);

  sleep_time.tv_sec = *interval;
  sleep_time.tv_nsec = 0;

  step = 0;

  while (1) {
    prev_bytes = total_bytes;
    gettimeofday(&prev_time, NULL);
    //nanosleep(&sleep_time, &remaining_time);
    clock_nanosleep(CLOCK_REALTIME, 0, &sleep_time, &remaining_time);

    diff_bytes = (total_bytes - prev_bytes);
    gettimeofday(&curr_time, NULL);

    size_t rate = (size_t)diff_bytes/difftv(&prev_time, &curr_time);
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
  int n, unacked = 0;
  
  while (RUN) {
    xfer_rdma_wait_os_event(cfg->ctx, &pinfo);

    msg.type = MSG_DONE;
    n = send(cfg->cntl_sock, &msg, sizeof(struct message), 0);
    if (n <= 0) {
      printf("RDMA control channel failed\n");
      break;
    }

    __sync_fetch_and_add(&sent, -1);
    __sync_fetch_and_add(&send_queued, -psd_slabs_buf_get_psize(cfg->slab));

    if (cfg->fname && (++unacked >= psd_slabs_buf_get_pcount(cfg->slab))) {
      n = recv(cfg->cntl_sock, &msg, sizeof(struct message), MSG_WAITALL);
      if (n < 0) {
	fprintf(stderr, "RDMA control channel failed\n");
	diep("recv");
      }
      if (msg.type == MSG_ACK)
	--unacked;
      else
	diep("unexpected ack");
    }

    if (pinfo.id == 0xdeadbeef)
      break;

    if (cfg->fname)
      psd_slabs_buf_read_swap(cfg->slab, 0);
  }

  pthread_exit(NULL);
}

void *rdma_write_thread(void *arg) {
  struct xfer_config *cfg = arg;
  XFER_RDMA_buf_handle hndl, *hptr;
  size_t bytes_allowed, send_amt, bytes_left;
  struct message msg;
  struct timespec now;
  int n;
  double dtmp;

  clock_gettime(CLOCK_REALTIME, &startup);
  bytes_left = cfg->bytes;
  
  while (RUN) {
    if (cfg->bandwidth == 0.0) {
      bytes_allowed = 0xFFFFFFFFFFFFFFFF;
    }
    else {
      clock_gettime(CLOCK_REALTIME, &now);
      dtmp = now.tv_nsec - startup.tv_nsec;
      dtmp = dtmp / 1000000000.0;
      dtmp = dtmp + (now.tv_sec - startup.tv_sec);
      dtmp = dtmp * cfg->bandwidth;
      dtmp = dtmp * 1000000.0;
      bytes_allowed = dtmp;
    }

    if ((sent < cfg->tx_depth) &&
        (total_bytes + send_queued) < bytes_allowed) {

      // wait for the next available buffer to send when necessary
      if (cfg->fname)
	psd_slabs_buf_wait_curr(cfg->slab, PSB_READ);

      hndl = *(XFER_RDMA_buf_handle*)
	psd_slabs_buf_get_priv_data(cfg->slab, PSB_CURR);

      // adjust send amount only for file alignment
      if (cfg->fname) {
	if (bytes_left > hndl.local_size) {
	  send_amt = hndl.local_size;
	  hndl.id = 0xcafebabe;
	}
	else {
	  send_amt = bytes_left;
	  hndl.id = 0xdeadbeef;
	}
	hndl.local_size = send_amt;
      }
      else {
	send_amt = hndl.local_size;
      }
      
      hptr = &hndl;
      xfer_rdma_post_os_put(&hptr, 1);
      
      psd_slabs_buf_curr_swap(cfg->slab);
      
      bytes_left -= send_amt;
      
      __sync_fetch_and_add(&send_queued, send_amt);
      __sync_fetch_and_add(&total_bytes, send_amt);
      __sync_fetch_and_add(&sent, 1);

      // stop cleanly when transfering files
      if (cfg->fname && !bytes_left)
	break;
    }
    else {
      usleep(100);
      continue;
    }
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

int rdma_slab_bufs_reg(struct xfer_config *cfg) {
  int buf_count;
  int i;

  // slab buf
  if (cfg->server)
    cfg->slab = psd_slabs_buf_create(cfg->buflen, cfg->slab_parts, 0);
  else
    cfg->slab = psd_slabs_buf_create(cfg->buflen, cfg->slab_parts, 1);
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
    if (xfer_rdma_register_buffer(cfg->ctx, handle) != 0) {
      fprintf(stderr, "could not register buf ptr\n");
      return -1;
    }
    psd_slabs_buf_set_priv_data_ind(cfg->slab, handle, i);
  }

  return 0;
}

int do_rdma_client(struct xfer_config *cfg) {
  int i, n;
  pthread_t rthr, pthr, rwthr;
  struct xfer_context *ctx = NULL;
  XFER_RDMA_buf_handle *hptr;

  struct message msg;
  struct timeval start_time, end_time;
  size_t slab_bytes;

  struct mdata pdata = {
    .buflen = cfg->buflen,
    .fsize = cfg->bytes,
    .slab_order = cfg->slab_order,
    .slab_parts = cfg->slab_parts
  };

  // connect RDMA control conn
  socket_client_connect(cfg, cfg->cntl);

  // setup the RDMA connect struct
  data.servername = cfg->host;
  data.local_priv = &pdata;
  data.local_priv_size = sizeof(struct mdata);
  data.tx_depth = cfg->tx_depth;
  
  printf("\n data.dbdirnum : \n");
  if (xfer_rdma_init(&data)) {
    return -1;
  }

  n = recv(cfg->cntl_sock, &msg, sizeof(struct message), MSG_WAITALL);
  if (n < 0) {
    fprintf(stderr, "RDMA control channel failed\n");
    diep("recv");
  }
  
  ctx = xfer_rdma_client_connect(&data);
  if (!ctx) {
    fprintf(stderr, "could not get client context\n");
    return -1;
  }
  cfg->ctx = ctx;

  if (rdma_slab_bufs_reg(cfg))
    return -1;

  // exchange pointers
  for (i = 0; i < cfg->slab_parts; i++) {
    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg->slab, PSB_CURR);
    xfer_rdma_wait_buffer(hptr);
    xfer_rdma_send_done(hptr);
    printf("raddr: %p, laddr: %p, size: %lu\n", hptr->remote_mr->addr,
           hptr->local_mr->addr, hptr->local_size);
    psd_slabs_buf_curr_swap(cfg->slab);
  }

  printf("Metadata exchange complete\n");

  // init some variables
  total_bytes = 0;
  sent = 0;
  send_queued = 0;
  RUN = 1;

  // start the file reader thread
  if (cfg->fname)
    pthread_create(&rthr, NULL, fread_thread, (void*)cfg);

  // start RDMA threads
  pthread_create(&pthr, NULL, rdma_poll_thread, (void*)cfg);
  pthread_create(&rwthr, NULL, rdma_write_thread, (void*)cfg);

  gettimeofday(&start_time, NULL);

  if (cfg->interval)
    pthread_cond_signal(&report_cond);

  if (cfg->fname) {
    pthread_join(rthr, NULL);
    pthread_join(rwthr, NULL);
  }
  else {
    int c = psd_slabs_buf_get_pcount(cfg->slab);
    for (i=0; i<c; i++)
      cfg->slab->entries[i]->status |= PSB_SEND_READY;
    pthread_join(rwthr, NULL);
  }

  pthread_join(pthr, NULL);

  msg.type = MSG_STOP;
  n = send(cfg->cntl_sock, &msg, sizeof(struct message), 0);
  if (n < 0) {
    fprintf(stderr, "RDMA control channel failed\n");
    diep("send");
  }

  gettimeofday(&end_time, NULL);
  print_bw(&start_time, &end_time, total_bytes);

  rdma_slab_bufs_unreg(cfg);
  xfer_rdma_finalize(&data);

  close(cfg->cntl_sock);

  return 0;
}

int do_rdma_server(struct xfer_config *cfg) {
  int i, n, lfd, clilen;
  pthread_t wthr, pthr;
  struct mdata *pdata;
  struct xfer_context *ctx = NULL;
  XFER_RDMA_buf_handle *hptr;

  struct message msg;
  struct sockaddr_in cliaddr;
  struct timeval start_time, end_time;
  size_t bytes_recv = 0;
  size_t slab_bytes;

  clilen = sizeof(cliaddr);

  lfd = socket_server_start(cfg);

  printf("Waiting for RDMA control conn...");
  fflush(stdout);
  cfg->cntl_sock = accept(lfd, (struct sockaddr *)&cliaddr, (socklen_t*)&clilen);
  printf("[connection from: %s]\n", inet_ntoa(cliaddr.sin_addr));

  if (xfer_rdma_init(&data)) {
    return -1;
  }

  // sync with the client
  msg.type = MSG_READY;
  printf("\n sizeof struct message %ld \n",sizeof(struct message));
  n = send(cfg->cntl_sock, &msg, sizeof(struct message), 0);
  
  if (n <= 0) {
    fprintf(stderr, "RDMA control conn failed\n");
    diep("send");
  }      
  
  ctx = xfer_rdma_server_connect(&data);
  if (!ctx) {
    fprintf(stderr, "could not get client context\n");
    return -1;
  }
  cfg->ctx = ctx;

  // get remote slab info
  pdata = data.remote_priv;
  cfg->buflen = pdata->buflen;
  cfg->bytes = pdata->fsize;
  cfg->slab_order = pdata->slab_order;
  cfg->slab_parts = pdata->slab_parts;
 
  
  if (rdma_slab_bufs_reg(cfg))
    return -1;

  // exchange pointers
  for (i = 0; i < cfg->slab_parts; i++) {
    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg->slab, PSB_CURR);
    xfer_rdma_post_buffer(hptr);
    xfer_rdma_wait_done(hptr);
    psd_slabs_buf_curr_swap(cfg->slab);
  }
  
  printf("Metadata exchange complete\n");

  if (cfg->fname) {
    pthread_create(&wthr, NULL, fwrite_thread, (void*)cfg);
  }

  gettimeofday(&start_time, NULL);

  if (cfg->interval)
    pthread_cond_signal(&report_cond);

  while (1) {
    n = recv(cfg->cntl_sock, &msg, sizeof(struct message), MSG_WAITALL);
    if (n <= 0) {
      fprintf(stderr, "RDMA control conn failed\n");
      diep("recv");
    }

    if (msg.type == MSG_STOP)
      break;

    if (msg.type == MSG_DONE) {
      n = psd_slabs_buf_get_psize(cfg->slab);
      if (cfg->bytes && ((n + bytes_recv) > cfg->bytes))
	n = (cfg->bytes - bytes_recv);

      if (cfg->fname) {
	psd_slabs_buf_advance(cfg->slab, n, PSB_WRITE);
	psd_slabs_buf_write_swap(cfg->slab, 0);
      }
      
      bytes_recv += n;
      total_bytes = bytes_recv;
    }
  }

  // signal file write thread with 0-sized slab to stop
  psd_slabs_buf_write_swap(cfg->slab, 0);
  if (cfg->fname) {
    pthread_join(wthr, NULL);
  }
  
  gettimeofday(&end_time, NULL);
  print_bw(&start_time, &end_time, total_bytes);

  rdma_slab_bufs_unreg(cfg);
  xfer_rdma_finalize(&data);

  // let the client close first
  n = recv(cfg->cntl_sock, &msg, sizeof(struct message), 0);
  close(cfg->cntl_sock);

  return 0;
}



int do_socket_client(struct xfer_config *cfg) {
  pthread_t rthr;

  struct timeval start_time, end_time;

  int s, n;
  size_t send_len;
  size_t slab_bytes;
  size_t bytes_sent;
  char *buf;
  
  s = socket_client_connect(cfg, cfg->host);
  
  gettimeofday(&start_time, NULL);
  
  if (cfg->fname) {
    struct mdata msg;
    msg.fsize = cfg->bytes;
    n = send(s, &msg, sizeof(struct mdata), 0);
    if (n <= 0) {
      diep("send");
    }
    
    pthread_create(&rthr, NULL, fread_thread, (void*)cfg);
  }
    
  if (cfg->interval)
    pthread_cond_signal(&report_cond);
  
  bytes_sent = 0;
  if (cfg->bytes) {
    while (bytes_sent < cfg->bytes) {
      slab_bytes = psd_slabs_buf_count_bytes_free(cfg->slab, PSB_READ);
      if (slab_bytes == 0) {
        psd_slabs_buf_read_swap(cfg->slab, 0);
        slab_bytes = psd_slabs_buf_count_bytes_free(cfg->slab, PSB_READ);
      }

      // we got signaled with nothing to read, so exit
      if (slab_bytes == 0) {
        fprintf(stderr, "nothing in buffer, done\n");
        break;
      }

      buf = psd_slabs_buf_addr(cfg->slab, PSB_READ);

      if ((cfg->bytes - bytes_sent) < slab_bytes)
        send_len = (cfg->bytes - bytes_sent);
      else
        send_len = slab_bytes;

      if (cfg->use_splice) {
        n = vmsplice_to_fd(cfg, s, buf, send_len);
        if (n <= 0)
          diep("vmsplice()");
      }
      else {
        n = send(s, buf, send_len, 0);
        if (n <= 0)
          diep("send()");
      }
      psd_slabs_buf_advance(cfg->slab, n, PSB_READ);
      bytes_sent += n;
      total_bytes = bytes_sent;
    }
    pthread_join(rthr, NULL);
  }
  else {
    RUN = 1;
    while (RUN) {
      buf = (char *) psd_slabs_buf_addr(cfg->slab, PSB_CURR);
      slab_bytes = psd_slabs_buf_get_psize(cfg->slab);

      if (cfg->use_splice) {
        n = vmsplice_to_fd(cfg, s, buf, slab_bytes);
        if (n <= 0)
          diep("vmsplice()");
      }
      else {
        n = send(s, buf, slab_bytes, 0);
        if (n <= 0)
          diep("send()");
      }
      psd_slabs_buf_curr_swap(cfg->slab);
      bytes_sent += n;
      total_bytes = bytes_sent;
    }
  }

  gettimeofday(&end_time, NULL);
  print_bw(&start_time, &end_time, bytes_sent);

  close(s);

  return 0;
}

int do_socket_server(struct xfer_config *cfg) {
  pthread_t wthr;
  struct timeval start_time, end_time;

  struct sockaddr_in cliaddr;
  int s, n, lfd, clilen;

  size_t bytes_recv;
  size_t slab_bytes;
  char *buf;

  lfd = socket_server_start(cfg);

  clilen = sizeof(cliaddr);

  while (1) {
    s = accept(lfd, (struct sockaddr *)&cliaddr, (socklen_t*)&clilen);
    printf("[connection from: %s]\n", inet_ntoa(cliaddr.sin_addr));

    gettimeofday(&start_time, NULL);

    if (cfg->fname) {
      struct mdata msg;
      n = recv(s, &msg, sizeof(struct mdata), MSG_WAITALL);
      if (n < 0) {
	diep("recv");
      }
      cfg->bytes = msg.fsize;
      pthread_create(&wthr, NULL, fwrite_thread, (void*)cfg);
    }
    
    if (cfg->interval)
      pthread_cond_signal(&report_cond);

    bytes_recv = 0;
    if (cfg->fname) {
      while (1) {
        slab_bytes = psd_slabs_buf_count_bytes_free(cfg->slab, PSB_WRITE);
        if (slab_bytes == 0) {
          psd_slabs_buf_write_swap(cfg->slab, 0);
          slab_bytes = psd_slabs_buf_count_bytes_free(cfg->slab, PSB_WRITE);
        }

        buf = psd_slabs_buf_addr(cfg->slab, PSB_WRITE);

	n = recv(s, buf, slab_bytes, 0);
  //printf("\n n = %d \n",n);
	
  if (n < 0) {
    
	  perror("recv:");
          break;
	}

	if (!n) {
          psd_slabs_buf_write_swap(cfg->slab, 0);
          break;
        }

        psd_slabs_buf_advance(cfg->slab, n, PSB_WRITE);
        bytes_recv += n;
        total_bytes = bytes_recv;
      }
      // signal write thread to stop
      psd_slabs_buf_write_swap(cfg->slab, 0);
      pthread_join(wthr, NULL);
    }
    else {
      while (1) {
        buf = (char *) psd_slabs_buf_addr(cfg->slab, PSB_CURR);
        slab_bytes = psd_slabs_buf_get_psize(cfg->slab);

        if (cfg->use_splice) {
          // from socket to /dev/null
          splice_fds(cfg, s, fdnull, slab_bytes);
        }
        else {
          n = recv(s, buf, slab_bytes, 0);
          if (n <= 0)
            break;
        }
        psd_slabs_buf_curr_swap(cfg->slab);
        bytes_recv += n;
        total_bytes = bytes_recv;
      }
    }

    gettimeofday(&end_time, NULL);
    print_bw(&start_time, &end_time, bytes_recv);

    close(s);
  }

  return 0;
}

// TODO
static void usage(const char *argv0) {
  printf("Usage:\n");
  printf("  %s [options]\n", argv0);
  printf("\n");
  printf("Options:\n");
  printf("  -p <port#>             listen on/connect to control port <port> (default 9930)\n");
  printf("  -q <port#>             RDMA CMA port (default 18515)\n");
  printf("  -l <size [M,G]>        size of message to exchange (default 1MB)\n");
  printf("  -n <bytes>             number of bytes to transfer (override file size)\n");
  printf("  -i <secs>              update interval in seconds\n");
  printf("  -t <secs>              duration of test in seconds\n");
  printf("  -f <path>              infile (client) | outfile (server)\n");
  printf("  -y <host>              RDMA control channel\n");
  printf("  -c <host>              connect to destination host (data channel)\n");
  printf("  -o <num>               SLAB buffer order (2^x)\n");
  printf("  -a <num>               SLAB partitions\n");
  printf("  -x <host/port>         XSP path signaling\n");
  printf("  -B <num>               bandwidth limit (MB/s)\n");
  printf("  -A <num>               cpu affinity (core number)\n");
  printf("  -d <num>               size of tx queue (default 16)\n");
  printf("  -z                     use SDP\n");
  printf("  -r                     use RDMA\n");
  printf("  -S                     use SPLICE\n");
  printf("  -s                     run as server\n");

  printf("  -ff                     Files in folder (client) \n");
}

//




//





int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    exit(-1);
  }
  
  page_size = sysconf(_SC_PAGESIZE);







 if ((fdnull = open("/dev/null", O_WRONLY)) == -1) {
  
    fprintf(stderr, "Could not open '/dev/null': %d: %s\n", errno, strerror(errno));
    return -1;
  }

  while((c = getopt(argc, argv, "A:Szrsq:l:p:n:c:i:t:f:D:I:x:o:a:y:d:B:")) != -1) {
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
      else if (optarg[len-1] == 'K') {
        mult = 1024;
        optarg[len-1] = '\0';
      }
      cfg.buflen = atoi(optarg) * mult;
      break;

    case 'q':
#ifdef HAVE_RDMA
      data.port = atoi(optarg);
#endif
      break;
    case 'p':
      cfg.port = atoi(optarg);
      break;
    case 'A':
      cfg.affinity = atoi(optarg);
      break;
    case 'S':
      cfg.use_splice = 1;
      break;
    case 's':
      cfg.server = 1;
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
      cfg.use_rdma = 1;
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
    // case 'F':
    //   cfg.foldername = strdup(optarg);
    //   break;
    default:
      usage(argv[0]);
      exit(-1);
    }
  }
 int count = 0;
  if (cfg.fname) {
    if(!cfg.server){
    
    
    numo = explore_dir(cfg.fname);
    printf("\n Total no. of files : %d \n",numo);
    // new comment
  //      int i, n;
  // pthread_t rthr, pthr, rwthr;
  // struct xfer_context *ctx = NULL;
  // XFER_RDMA_buf_handle *hptr;

  // struct message msg;
  // struct timeval start_time, end_time;
  // size_t slab_bytes;
  // end - new comment   
    
    cfg.num_files = numo;
    

      DIR *d;
      char *pathdir;
      struct dirent *dir;
      char path[1000];
      // printf("\n %s",cfg.fname);
      strcpy(path,cfg.fname);
      // printf("\n %s\n",path);
      d = opendir(path);
      
      char full_path[1000];

    struct sockaddr_in serveraddr;
    struct hostent *server;

    int s, slen=sizeof(serveraddr);
    int ai_family;

    if (cfg.use_sdp) {
      ai_family = AF_INET_SDP;
    }
    else
    {
      ai_family = AF_INET;
    }

    // if ((s=socket(ai_family, SOCK_STREAM, 0)) == -1) {
    
    //   diep("socket");

    // }
    // server = gethostbyname(cfg.host);
    // if (server == NULL) {
    //   fprintf(stderr,"ERROR, no such host as %s\n", cfg.host);
    //   diep("gethostbyname");
    // }

    // bzero(&serveraddr, sizeof(serveraddr));
    // serveraddr.sin_family = ai_family;
    // bcopy(server->h_addr, &serveraddr.sin_addr.s_addr, server->h_length);
    // serveraddr.sin_port = htons(cfg.port);

    // if (connect(s, (struct sockaddr*)&serveraddr, slen) < 0){
    
    //   diep("connect");
  
    // }

    // cfg.cntl_sock = s;
    // printf("\n inside socket : %d \n",s);


    /******************/
    // if ((s=socket(ai_family, SOCK_STREAM, 0)) == -1) {
    
    //   diep("socket");

    // }


      if (d)
      {
        
        int cnt = 0;
        // int ssrock;
        // ssrock = socket_client_connect(&cfg, cfg.cntl);
        // printf("\n ssrock value : %d \n",ssrock);
          while ((dir = readdir(d)) != NULL)
            { 
              s=socket(ai_family, SOCK_STREAM, 0);
              //Condition to check regular file.
               if(dir->d_type==DT_REG){
                //cfg.bytes = cfg.bytes * cnt;
                pthread_t rthr;
                pthread_t tthr;

                int fd = -1;
                int c;
                int len;
                unsigned mult = 1;

#ifndef HAVE_RDMA
  if (cfg.use_rdma) {
    fprintf(stderr, "Please compile with RDMA support.\n");
    exit(1);
  }
#endif


#ifdef HAVE_SETAFFINITY
  if (cfg.affinity >= 0) {
    if ((ncores = sysconf(_SC_NPROCESSORS_CONF)) <= 0)
      err(1, "sysconf: couldn't get _SC_NPROCESSORS_CONF");
    CPU_ZERO(&cpu_set);
    CPU_SET(cfg.affinity, &cpu_set);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) != 0)
      err(1, "couldn't change CPU affinity");
  }
#endif

  // setup splice pipe
  int ret = pipe(cfg.pipe);
  if (ret < 0) {
    fprintf(stderr, "pipe failed: %s", strerror(errno));
    exit(-1);
  }

  // maximime effective pipe buffer
  int rc = 0;
  do {
    splice_size = (long) fcntl(cfg.pipe[0], F_GETPIPE_SZ);
    if (splice_size < 0) {
      fprintf(stderr, "fcntl failed: %s", strerror(errno));
      exit(1);
    }
    rc = fcntl(cfg.pipe[0], F_SETPIPE_SZ, splice_size*2);
  } while (rc >= 0); 
  
  // determine our buffer size if using order
  if (cfg.slab_order > 0)
    cfg.buflen = (1UL << cfg.slab_order);

  if (!cfg.use_rdma) {
    
    if (cfg.server)
      cfg.slab = psd_slabs_buf_create(cfg.buflen, cfg.slab_parts, 0);
    else
    {
      
      cfg.slab = psd_slabs_buf_create(cfg.buflen, cfg.slab_parts, 1);
    }
    if (!cfg.slab) {
      fprintf(stderr, "could not allocate SLAB buffer\n");
      return -1;
    }
  }

  printf("Using a SLaBS buffer of size %u with %d partitions of size %u\n",
         (unsigned)floor(cfg.buflen/cfg.slab_parts)*cfg.slab_parts,
         cfg.slab_parts,
         (unsigned)floor(cfg.buflen/cfg.slab_parts));

  if (cfg.interval) {
    
    pthread_mutex_init(&report_mutex, NULL);
    pthread_cond_init(&report_cond, NULL);
    pthread_create(&rthr, NULL, bw_report_thread, &cfg.interval);
#ifdef HAVE_SETAFFINITY
    if (cfg.affinity >= 0) {
      CPU_ZERO(&cpu_set);
      CPU_SET(cfg.affinity+1%ncores, &cpu_set);
      if (pthread_setaffinity_np(rthr, sizeof(cpu_set_t), &cpu_set) != 0)
        err(1, "couldn't change THREAD affinity");
    }
#endif
  }

  if (cfg.time && !cfg.fname)
  {
    
    pthread_create(&tthr, NULL, time_thread, &cfg.time);
  }

                  printf("\n inside condition \n");
                  full_path[0]='\0';
                  strcat(full_path,path);
                  strcat(full_path,"/");
                  strcat(full_path,dir->d_name);
                  printf("%s\n",full_path);
                  strcpy(cfg.fname,full_path);
                  //strcat(cfg.fname,'\0');
                  //strcat(cfg.fname,full_path);
             // printf("\n cfg new fname: %s \n",cfg.fname);
             // printf("\n");
              fd = open(cfg.fname, O_RDONLY | O_DIRECT);
              mmap_flags = PROT_READ | PROT_WRITE;
            
            // else {
            //   fd = open(cfg.fname, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, S_IRUSR | S_IWUSR);
            //   mmap_flags = PROT_READ | PROT_WRITE;
            // }

            if (fd < 0) {
              fprintf(stderr, "could not open file\n");
              return -1;
            }
            
          
            fstat(fd, &stat_buf);
            fsize = stat_buf.st_size;
            if (!cfg.bytes) {
              cfg.bytes = fsize;
            }
            printf("file size: %lu (to transfer: %lu)\n", fsize, cfg.bytes);
          
          cfg.fd = fd;
          printf("\n after connect RDMA & !cfg.server \n");

          // } // end of if-condition
          //   }
          // closedir(d);
      
      /******************/
     
      
       if (cfg.cntl == NULL) {
        
         cfg.cntl = cfg.host;
   
       }

  // check if we have anything to send/recv at this point

  if (!cfg.server && cfg.fname && !cfg.bytes) { // this comment 
    //printf("\n After sending first file , i am here  \n");
  
    goto exitc;
  }
  //printf("\n a b repeat \n");
  // [passed]
  if (!cfg.server) {
    //signal(SIGINT, do_stop);
    //do_rdma_client(&cfg);
  
  // printf("\n a b repeat \n");
  // this comment
    int i, n;
  pthread_t rthr, pthr, rwthr;
  struct xfer_context *ctx = NULL;
  XFER_RDMA_buf_handle *hptr;

  struct message msg;
  struct timeval start_time, end_time;
  size_t slab_bytes;
  // end - this comment 

  struct mdata pdata = {
    .buflen = cfg.buflen,
    .fsize = cfg.bytes,
    .slab_order = cfg.slab_order,
    .slab_parts = cfg.slab_parts,

  };
  // pass
  // connect RDMA control conn
  
  
    server = gethostbyname(cfg.host);
    if (server == NULL) {
      fprintf(stderr,"ERROR, no such host as %s\n", cfg.host);
      diep("gethostbyname");
    }

    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = ai_family;
    bcopy(server->h_addr, &serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(cfg.port);

    if (connect(s, (struct sockaddr*)&serveraddr, slen) < 0){
    
      diep("connect");
  
    }

    cfg.cntl_sock = s;
    printf("\n inside socket : %d \n",s);




  // int ssrock;
  // if (cnt==0){
  // ssrock = socket_client_connect(&cfg, cfg.cntl);
  // }
  // else {
  //   continue;
  // }
  // printf("\n ssrock value : %d \n",ssrock);


  // setup the RDMA connect struct
  data.servername = cfg.host;
  data.local_priv = &pdata;
  data.local_priv_size = sizeof(struct mdata);
  data.tx_depth = cfg.tx_depth;

  if (xfer_rdma_init(&data)) {
    return -1;
  }
 //
  n = recv(cfg.cntl_sock, &msg, sizeof(struct message), MSG_WAITALL);
  if (n < 0) {
    fprintf(stderr, "RDMA control channel failed\n");
    diep("recv");
  }
  //printf("\n before connection ctx = xfer_rdma_client_connect(&data) \n ");
  ctx = xfer_rdma_client_connect(&data);
  
  if (!ctx) {
    fprintf(stderr, "could not get client context\n");
    return -1;
  }
  
  cfg.ctx = ctx;
  
  if (rdma_slab_bufs_reg(&cfg)) {
    
    return -1; }
 
  // exchange pointers
  printf("\n before slab exchange pointer the file : %s \n",cfg.fname);
  
  for (i = 0; i < cfg.slab_parts; i++) {
    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg.slab, PSB_CURR);
    xfer_rdma_wait_buffer(hptr);
    xfer_rdma_send_done(hptr);
    printf("raddr: %p, laddr: %p, size: %lu\n", hptr->remote_mr->addr,
           hptr->local_mr->addr, hptr->local_size);
    psd_slabs_buf_curr_swap(cfg.slab);

    //rdma_slab_bufs_unreg(&cfg);
    //xfer_rdma_finalize(&data);
  } 

  printf("Metadata exchange complete\n");

  count = count + 1;
  
  // if(count==100 || count==200 || count==300||count==400||count==500) {
  //   printf("\n Snooze Client after 100th file processed\n");
  //   sleep(10);
  // }
  // init some variables
  total_bytes = 0;
  sent = 0;
  send_queued = 0;
  RUN = 1;

  // start the file reader thread
  if (full_path)
    pthread_create(&rthr, NULL, fread_thread,&cfg);

  // start RDMA threads
  pthread_create(&pthr, NULL, rdma_poll_thread, &cfg);
  pthread_create(&rwthr, NULL, rdma_write_thread,&cfg);

  gettimeofday(&start_time, NULL);

  if (cfg.interval)
    pthread_cond_signal(&report_cond);

  if (full_path) {
    pthread_join(rthr, NULL);
    pthread_join(rwthr, NULL);
  }
  else {
    int c = psd_slabs_buf_get_pcount(cfg.slab);
    for (i=0; i<c; i++)
      cfg.slab->entries[i]->status |= PSB_SEND_READY;
    pthread_join(rwthr, NULL);
  }

  pthread_join(pthr, NULL);

  msg.type = MSG_STOP;
  n = send(cfg.cntl_sock, &msg, sizeof(struct message), 0);
  if (n < 0) {
    fprintf(stderr, "RDMA control channel failed\n");
    diep("send");
  }

  gettimeofday(&end_time, NULL);
  print_bw(&start_time, &end_time, total_bytes);

  rdma_slab_bufs_unreg(&cfg); // this comment
  xfer_rdma_finalize(&data); // this comment


    //psd_slabs_buf_free(cfg.slab); // new comment
    //psd_slabs_buf_reset(cfg.slab); // new comment

  // printf("\n cfg.cntl_sock : %d | ssrock : %d \n",cfg.cntl_sock,ssrock);

  printf("\n cfg.cntl_sock : %d | \n",cfg.cntl_sock);
  close(cfg.cntl_sock); // this comment
   // this comment
    
}

 exitc:
if (cfg.fname) {

    printf("\n inside exitc \n");
     //xfer_rdma_finalize(&data); // new comment
     
  //  rdma_slab_bufs_unreg(&cfg); // new comment
  // xfer_rdma_finalize(&data); // new comment
  
   //close(cfg.cntl_sock); //this comment

    psd_slabs_buf_free(cfg.slab);
    psd_slabs_buf_reset(cfg.slab);
    
    //rdma_slab_bufs_unreg(&cfg);
  //xfer_rdma_finalize(&data);
   
   close(fd); // this comment


  }

//  if (count==337) {
//   usleep(100);
//   usleep(10);
//  }
  // if(cnt<=numo && cnt!=1){
     
  //   printf("\n Files still exists in directory \n");
  //   xfer_rdma_finalize(&data);
  //   close(cfg.cntl_sock); // this comment
  //   //close(ssrock); // this comment
  





  //   // fd = 0;
  //   //psd_slabs_buf_free(cfg.slab);
  //   //psd_slabs_buf_reset(cfg.slab);
  //   close(fd);
  // }
  // else {

  // if (cfg.fname) {

  //   printf("\n inside exitc \n");
     

  //  close(cfg.cntl_sock); //this comment

  //   psd_slabs_buf_free(cfg.slab);
  //   psd_slabs_buf_reset(cfg.slab);
    
  //   //rdma_slab_bufs_unreg(&cfg);
  // //xfer_rdma_finalize(&data);
   
  //  close(fd); // this comment


  // }
  // }

  if (cfg.interval)
    pthread_cancel(rthr);

#ifdef WITH_XSP
  if (cfg.xsp_hop)
    xsp_close2(sess);
#endif

  cnt++;
  // return 0;



      /******************/
      }
      
      
      
      } // end of if-condition
            
            
            }
            
     
     close(cfg.cntl_sock); // new comment 
    
      closedir(d);

    
    
    
    
    }
    
  else 
  {
  
  /***********
   * RDMA Initial connection Manager to set how many files is Server gonna receive from Client.
   *  int looper will store the value and loop will run and create files 
   ***********/ 

  /********
   * End of Looper
   * 
   *******/


cfg.slab_parts = 10; //parition 
cfg.slab_order = 28;

int looper = 5;

int validator = 0;

char exppath[1000];
char newfoldername[1000] = {0};

strcpy(exppath,cfg.fname);
printf("\n exppath : %s \n",exppath);
//char exppath[1000] = "0";
      int i, n, lfd, clilen;
  pthread_t wthr[10000], pthr[10000];
  pthread_t rthr[10000];
  pthread_t tthr[10000];
  static pthread_cond_t report_cond[10000];
static pthread_mutex_t report_mutex[10000];


  struct mdata *pdata;
  struct xfer_context *ctx = NULL;
  XFER_RDMA_buf_handle *hptr;

  struct message msg;
  struct sockaddr_in cliaddr;
  struct timeval start_time, end_time;
  size_t bytes_recv = 0;
  size_t slab_bytes;

  clilen = sizeof(cliaddr);
  //ctx = xfer_rdma_server_connect(&data); // new comment

int count=0;
 for (int j=0;j<10001;j++) {
  
#ifndef HAVE_RDMA
  if (cfg.use_rdma) {
    fprintf(stderr, "Please compile with RDMA support.\n");
    exit(1);
  }
#endif

#ifdef HAVE_SETAFFINITY
  if (cfg.affinity >= 0) {
    if ((ncores = sysconf(_SC_NPROCESSORS_CONF)) <= 0)
      err(1, "sysconf: couldn't get _SC_NPROCESSORS_CONF");
    CPU_ZERO(&cpu_set);
    CPU_SET(cfg.affinity, &cpu_set);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) != 0)
      err(1, "couldn't change CPU affinity");
  }
#endif

  // setup splice pipe
  int ret = pipe(cfg.pipe);
  if (ret < 0) {
    fprintf(stderr, "pipe failed: %s", strerror(errno));
    exit(-1);
  }

  // maximime effective pipe buffer
  int rc = 0;
  do {
    splice_size = (long) fcntl(cfg.pipe[0], F_GETPIPE_SZ);
    if (splice_size < 0) {
      fprintf(stderr, "fcntl failed: %s", strerror(errno));
      exit(1);
    }
    rc = fcntl(cfg.pipe[0], F_SETPIPE_SZ, splice_size*2);
  } while (rc >= 0); 
  
  // determine our buffer size if using order
  if (cfg.slab_order > 0)
    cfg.buflen = (1UL << cfg.slab_order);

  if (!cfg.use_rdma) {
    if (cfg.server)
      cfg.slab = psd_slabs_buf_create(cfg.buflen, cfg.slab_parts, 0);
    else
    {
      cfg.slab = psd_slabs_buf_create(cfg.buflen, cfg.slab_parts, 1);
    }
    if (!cfg.slab) {
      fprintf(stderr, "could not allocate SLAB buffer\n");
      return -1;
    }
  }

  printf("Using a SLaBS buffer of size %u with %d partitions of size %u\n",
         (unsigned)floor(cfg.buflen/cfg.slab_parts)*cfg.slab_parts,
         cfg.slab_parts,
         (unsigned)floor(cfg.buflen/cfg.slab_parts));

  if (cfg.interval) {
    pthread_mutex_init(&report_mutex[count], NULL);
    pthread_cond_init(&report_cond[count], NULL);
    pthread_create(&rthr[count], NULL, bw_report_thread, &cfg.interval);
#ifdef HAVE_SETAFFINITY
    if (cfg.affinity >= 0) {
      CPU_ZERO(&cpu_set);
      CPU_SET(cfg.affinity+1%ncores, &cpu_set);
      if (pthread_setaffinity_np(rthr[count], sizeof(cpu_set_t), &cpu_set) != 0)
        err(1, "couldn't change THREAD affinity");
    }
#endif
  }

count = count + 1;
printf("\n File being Processed: %d \n",j);

  if (cfg.time && !cfg.fname)
    pthread_create(&tthr[count], NULL, time_thread, &cfg.time);

              char foldername[1000] = {0};
              strcpy(foldername,newfoldername);
              int mmap_flags;
              char intstring[1000];
                  // foldername[0] = '\0';
                  // newfoldername[0] = '\0';
                  // *foldername = '\0';
                  // *newfoldername = '\0';
              
              //strcpy(foldername,randstring(32));

              //snprintf(intstring, 1200, "%d", i); 
              strcpy(foldername,randstring(32));
              if(i==0){
              strcat(cfg.fname,foldername);
              }
              else {
                strcpy(cfg.fname,exppath);
                strcat(cfg.fname,foldername);
              }
                  
                  //printf("server: %s \n",cfg.fname);
                   // puts string into buffer
                  //strcpy(newfoldername,"//");
                  //strcat(newfoldername,foldername);
                  //strcat(cfg.fname,foldername);
                  
                  fd = open(cfg.fname, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, S_IRUSR | S_IWUSR);
                  mmap_flags = PROT_READ | PROT_WRITE;
                  
                  //foldername[0]= '\0';
                  strcpy(foldername,newfoldername);
                  //newfoldername[0]= '\0';


                  if (fd < 0) {
                    fprintf(stderr, "could not open file\n");
                    return -1;
                  }
                  cfg.fd = fd;
                  

                  // here you can copy the original cfg.fname path after process being done 
                  
                 
                 
                  
            

            // fd = open(cfg.fname, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, S_IRUSR | S_IWUSR);
            //       mmap_flags = PROT_READ | PROT_WRITE;
                  
            //       if (fd < 0) {
            //         fprintf(stderr, "could not open file\n");
            //         return -1;
            //       }
            // cfg.fd = fd;
            
            /******************/
       if (cfg.cntl == NULL)
    cfg.cntl = cfg.host;

  // check if we have anything to send/recv at this point
  if (!cfg.server && cfg.fname && !cfg.bytes) {
   printf("\n it comes to exit \n");
    goto exit;

  }

  if (cfg.use_rdma)
    {
      printf("\n RDMA Server: \n");
      //do_rdma_server(&cfg);
      


  if(validator==0) {
  lfd = socket_server_start(&cfg);
  }
  else {
    printf("\n Connection is already establised , Use same netconfig  : %d \n",lfd);
    
  }

  printf("Waiting for RDMA control conn...");
  fflush(stdout);
  cfg.cntl_sock = accept(lfd, (struct sockaddr *)&cliaddr, (socklen_t*)&clilen);
  printf("[connection from: %s]\n", inet_ntoa(cliaddr.sin_addr));

  if (xfer_rdma_init(&data)) {
    return -1;
  }

  // sync with the client
  msg.type = MSG_READY;
  printf("\n sizeof struct message %ld \n",sizeof(struct message));
  n = send(cfg.cntl_sock, &msg, sizeof(struct message), 0);
  
  if (n <= 0) {
    fprintf(stderr, "RDMA control conn failed\n");
    diep("send");
  }      
  
   ctx = xfer_rdma_server_connect(&data); // this comment
  if (!ctx) {
    fprintf(stderr, "could not get client context\n");
    return -1;
  }
  cfg.ctx = ctx;

  // get remote slab info
  pdata = data.remote_priv;
  cfg.buflen = pdata->buflen;
  cfg.bytes = pdata->fsize;
  cfg.slab_order = pdata->slab_order;
  cfg.slab_parts = pdata->slab_parts;
  
  if (rdma_slab_bufs_reg(&cfg))
    return -1;

  // exchange pointers
  for (i = 0; i < cfg.slab_parts; i++) {
    hptr = (XFER_RDMA_buf_handle*)
           psd_slabs_buf_get_priv_data(cfg.slab, PSB_CURR);
    xfer_rdma_post_buffer(hptr);
    xfer_rdma_wait_done(hptr);
    psd_slabs_buf_curr_swap(cfg.slab);
  }
  
  printf("Metadata exchange complete\n");
  validator = 1;

  if (cfg.fname) {
    pthread_create(&wthr[count], NULL, fwrite_thread,&cfg);
  }

  gettimeofday(&start_time, NULL);

  if (cfg.interval)
    pthread_cond_signal(&report_cond[count]);

  while (1) {
    n = recv(cfg.cntl_sock, &msg, sizeof(struct message), MSG_WAITALL);
    if (n <= 0) {
      fprintf(stderr, "RDMA control conn failed\n");
     
      diep("recv");
    }

    if (msg.type == MSG_STOP)
      break;

    if (msg.type == MSG_DONE) {
      n = psd_slabs_buf_get_psize(cfg.slab);
      if (cfg.bytes && ((n + bytes_recv) > cfg.bytes))
	n = (cfg.bytes - bytes_recv);

      if (cfg.fname) {
	psd_slabs_buf_advance(cfg.slab, n, PSB_WRITE);
	psd_slabs_buf_write_swap(cfg.slab, 0);
      }
      
      bytes_recv += n;
      total_bytes = bytes_recv;
    }
  }

  // signal file write thread with 0-sized slab to stop
  psd_slabs_buf_write_swap(cfg.slab, 0);
  if (cfg.fname) {
    pthread_join(wthr[count], NULL);
  }
  
  gettimeofday(&end_time, NULL);
  print_bw(&start_time, &end_time, total_bytes);

  rdma_slab_bufs_unreg(&cfg); // this comment 
  xfer_rdma_finalize(&data); // this comment 

  // let the client close first
  n = recv(cfg.cntl_sock, &msg, sizeof(struct message), 0);
  
  //close(cfg.cntl_sock);
  printf("\n looper value changed to what: %d \n",looper);
 exit:
  if (cfg.fname) {

    close(fd);
    printf("\n inside server cfg.fname \n");

    psd_slabs_buf_free(cfg.slab);
    psd_slabs_buf_reset(cfg.slab);
    
    // rdma_slab_bufs_unreg(&cfg); // new comment 
    // xfer_rdma_finalize(&data); // new comment 

    //psd_slabs_buf_free(cfg.slab);
    //psd_slabs_buf_reset(cfg.slab);
  }

  if (cfg.interval)
    pthread_cancel(rthr[count]);


      
    }

    
 }
// rdma_slab_bufs_unreg(&cfg); // new comment 
//   xfer_rdma_finalize(&data); // new comment 


  return 0;



      /******************/
            
      }
  
  }

}

 

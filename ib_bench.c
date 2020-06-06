/*	ib_bench.c	*/

#define IB_BENCH_VERSION_STRING "0.3 July 28, 2011"

/*	Copyright 2011, System Fabric Works, Inc., All Rights Reserved

	Compile and link under Linux and OFED using:

	cc -lrt -lrdmacm -libverbs -g -O0 -o ib_bench ib_bench.c

	cc -Wall -lrt -lrdmacm -libverbs -g -O2 -o ib_bench ib_bench.c


	This program is loosely based along the lines of the OFED perftest
	programs ib_{send,read,write}_{bw,lat} but is intended to handle the
	following:

		1) Use rdma_cm for all connection management, capable of
		   running under all OFED supported RDMA technologies.

		2) Allow the use of two HCAs simultaneously for high speeds.

		3) Allow an arbitrary number of queue pairs to be used.

		4) Allow a bandwidth limitation on the queue pairs

	The program is invoked with optional arguments (below) and then either
	nothing (indicating passive side server) or one or two IP addresses to
	connect to (indicating active side client).

	Optional arguments and their meaning:

	-p  --port N   The rdma_cm port number, Default is 18516

			The port number is used by the passive side to set up
			a listening mechanism, and by the active side to make
			the connection to the passive.  In Infiniband rdma_cm,
			the port space is disjoint between RC and UD, and is
			also disjoint from TCP/IP or UDP/IP.

	    --bind0 IP  A local IP address to bind to for first HCA

			The passive side can listen to all interfaces, which is
			the default, or it can be bound to listen to specific
			interfaces.  The active side can just connect to the
			target address, or it can request rdma_cm to make the
			connection through a specific interface as defined by
			this argument.

	    --bind1 IP  A local IP address to bind to for second HCA

     *  -m  --mtu N    The MTU to use, Default is 2048

			This parameter is informational, to tell ib_bench what
			the MTU is.  Connection MTU cannot be known until the
			connection is established.  It is possible to modify
			the MTU value for Infiniband, but this is not done.

	-c  --connection RC/UD   The connection type, Default RC

			These are the only two connection types supported by
			rdma-cm.  The UD connections only support send, and do
			not support RDMA read or write.  Performance measurement
			is different for UD.  The sender can only measure how
			many packets have been sent, while the receiver can only
			measure how many actually were received.  It is a good
			idea to capture reports from both sides and compare.

     *  -o  --operation SEND/WRITE/READ  The type of test to run, Default SEND

			The operation is always with respect to the client or
			active side, connecting to the server or passive side.
			Data moves from client to server for send and write,
			and from server to client for read.  One other choice
			is READWRITE (or WRITEREAD) which will alternate
			connections between doing a read and write to use
			bidirectional bandwidth.

     *  -l  --latency  Show performance as latency instead of bandwidth

			Latency measurements are round trip, from entry into
			send queue until exit from completion queue.  The
			transmit queue depth impacts the apparent latency.

     *  -s  --size N   The message size to use, Default is 65536

			All transfers use the same size.  The size is for the
			actual useful data payload, and does not include any
			required overhead on the transport.  The size for UD
			connections may not be larger than the MTU.  For RC
			the messages will be segmented as required.

     *  -a  --all      Use all available message sizes in powers of 2

			This option will automatically change the message size
			every time a report is printed.  If no reports are done,
			the message size will not change.  Note that the change
			in size only applies to new messages.  The ones that
			are already in the transmit queue remain at whatever
			size they were when they were queued.  It is difficult
			to get useful performance information on any particular
			size message when this is used.

     *  -t  --tx-depth N  The depth of the transmit queue, Default 250

			This can have a significant impact on the ability of
			a interface to keep the pipeline full for a single
			connection.  Also, since latency is measured from the
			time the request is queued until the request completes,
			a depth larger than 1 will show longer latency than the
			latency of a single request.

     *  -r  --rx-depth N  The depth of the receive queue, Default 500

			This number should be kept large enough for the server
			to always have a receive posted on every connection, or
			the performance will be depressed by receiver delay.

     *  -u  --qp-timeout N  The timeout for the queue pairs

			Carried over from other tests, currently unused.

     *  -n  --number N    The number of queue pairs to test, Default 1

			There appears to be a limit of just over 28,000 rdma-cm
			connections from one system.  The default value is
			actually 2 if there are two servers specified by the
			client, with at least one created for each server.

	-d  --duration N  The time to run the tests in seconds, Default forever

			The test will be terminated after this amount of time.
			The countdown does not start until all connections have
			been established.  If the duration is a multiple of the
			report time, the report will print before termination.

	    --report N    The time between reports, Default 6 seconds

			Performance data is continuously tracked once all of
			the connections have been established.  An asynchronous
			thread prints out this information at the specified
			interval.  The data shows the performance since the
			last report to the left of a // mark, and the total
			performance data since the beginning to the right.
			Due to the asynchronous nature, the device and queue
			pair performance information may not exactly match, as
			operations may complete as the report is being printed.
			The report shows an average except for where the labels
			"min" and "max" appear.  These are the single transfer
			minimum and maximum values.  This measures completions
			that have happened in the reporting period.

	    --cqperqp     Create one CQ for each QP instead of one per device

			There is a choice of a single completion queue for each
			interface, resulting in one or two queues, or else a
			completion queue for each connection.  The default is
			one per device.

     *      --max_rd_atomic N   Responder resource control, default 4

			The value set in the initiator_depth field for
			rdma_accept, or the responder_resources field for
			rdma_connect.

     *      --max_dest_rd_atomic N   Responder resource control, default 4

			The value set in the responder_resources field for
			rdma_accept, or the initiator_depth field for
			rdma_connect.

     *      --min_rnr_timer N  Minimum RNR NAK timer

			Carried over from other tests, currently unused.

     *      --timeout N        Local ack timeout

			Carried over from other tests, currently unused.

     *      --retry_cnt N      Retry count, default value 6

			The value set in the retry_count field for
			rdma_connect or rdma_accept

     *      --rnr_retry N      RNR retry, default value 6

			The value set in the rnr_retry_count field for
			rdma_connect or rdma_accept

	-b  --bandwidth F  The bandwidth limit for each connection in MB/second

	-h  --help         Display the usage help text

	Note: The --connection type and the --port number must match on both
	      sides to make the connection.

	      The optional --bind0 and --bind1 are local to each side.

	      The --duration and --report values are local to each side so they
	      can differ.  Note that --report 0 means do not report.

	      The --cqperqp option is local to each side so they can differ.

	      The rest of the parameters, marked with an *,  are ignored on the
	      passive side, as they are overwritten by the active's incoming
	      connection.  If they are given, the message "Arguments given to
	      passive side that will be overwritten by active connection." is
	      written to standard output as a reminder, to support running
	      both sides with (mostly) the same arguments.

	      The passive side will exit when all connections have been shut
	      down for RC connections.  For UD connections this cannot be
	      detected and the passive side will have to be stopped with some
	      kind of signal, like that generated by control-C.  If no
	      connections are ever seen in RC mode, then the passive side will
	      never exit, since it will never see the connections shut down.

	      The performance report includes totals which are calculated as
	      lists are processed.  The device report happens first, and then
	      the set of connections are processed.  Data continues to flow
	      during this processing, so the total from the queue pair report
	      may be higher than the device report, especially when there are
	      a large number of connections.  This difference is very small,
	      and is left intact because it helps assure that connections are
	      progressing as expected.

	      The performance report shows the rate for the last reporting
	      period, followed by //, followed by the cumulative rate since
	      all of the connections were established.  Each line has a unique
	      string to facilitate awk/grep/etc extraction of information.

	      In latency mode, the performance report also shows the all-time
	      single-message minimum and maximum latency for each device.  The
	      latency is measured from the time the message is posted to the
	      send queue until the time the message is acknowledged as seen
	      by the appearance of the work completion queue entry.  This is
	      full end-to-end round trip latency, continuously measured for
	      all messages.

*/

#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <byteswap.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <pthread.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <signal.h>
#include <sys/statfs.h>
#include <linux/fs.h>
#include <sched.h>

#ifdef WITH_XSP
#include "libxsp_client.h"
#endif

#define DEFAULT_PORT 18516	/* Default port number used for rdma_connect */

#define DEFAULT_MTU 2048	/* Default MTU to use */

#define DEFAULT_SIZE 65536	/* Default message size to use */

#define MAX_LISTEN_QUEUE 256	/* Number of incoming connects to queue */

#define MAX_CONNECT_QUEUE 192	/* Number of outgoing connects in progress */

/* Option values, see head of this file for the meaning of each one */

/* XSP hop */
char *xsp_hop = NULL;

/* -p  --port N   The rdma_cm port number to listen on or connect to */
int arg_port = DEFAULT_PORT;

/* --bind0 IP  A local IP address to bind to */
/* --bind1 IP  A local IP address to bind to */
char *arg_bind0 = 0;		/* Text version of bind0 local IP address */
char *arg_bind1 = 0;
struct sockaddr_in sa_bind0;	/* Binary version of bind0 local IP address */
struct sockaddr_in sa_bind1;

/* -m  --mtu N    The MTU to use */
int arg_mtu = DEFAULT_MTU;

/* -c  --connection RC/UD   The connection type */
#define BENCH_CONN_RC 1
#define BENCH_CONN_UD 2
int arg_connection = BENCH_CONN_RC;

/* -o  --operation SEND/WRITE/READ  The type of test to run */
#define BENCH_SEND  1		/* Doing send tests */
#define BENCH_WRITE  2		/* Doing rdma write tests */
#define BENCH_READ  4		/* Doing rdma read tests */
#define BENCH_RDWR  8		/* Doing rdma read and write tests */
int arg_operation = BENCH_SEND;

/* -l  --latency  Show performance as latency instead of bandwidth */
int arg_latency = 0;

/* -s  --size N   The message size to use */
#define BENCH_MIN_MSG 1		/* Minumum message size */
#define BENCH_MAX_MSG (2<<29)	/* Maximum message size */
int arg_size = DEFAULT_SIZE;	/* Test message size */

/* -a  --all      Use all available message sizes in powers of 2 */
int arg_all = 0;

/* -t  --tx-depth N  The depth of the transmit queue on sender queue pair */
int arg_tx_depth = 250;

/* -r  --rx-depth N  The depth of the receive queue on receiver queue pair */
int arg_rx_depth = 500;

/* -u  --qp-timeout N  The timeout for the queue pairs */
int arg_qp_timeout = 0;

/* -n  --number N    The number of queue pairs to test */
int arg_number = 0;

/* -d  --duration N  The amount of time to run the tests in seconds */
int arg_duration = 0;

/* --report N    The amount of time between performance reports (sec) */
int arg_report = 6;

/* --cqperqp     Create one CQ for each QP instead of one per device */
int arg_cqperqp = 0;

/* --max_rd_atomic N   Responder resource control */
int arg_max_rd_atomic = 4;

/* --max_dest_rd_atomic N   Responder resource control */
int arg_max_dest_rd_atomic = 4;

/* --min_rnr_timer N  Minimum RNR NAK timer */
int arg_min_rnr_timer = 0;

/* --timeout N        Local ack timeout */
int arg_timeout = 0;

/* --retry_cnt N      Retry count */
int arg_retry_cnt = 6;

/* --rnr_retry N      RNR retry */
int arg_rnr_retry = 6;

/* -b  --bandwidth F  The bandwidth limit for each connection in MB/second */
float arg_bandwidth = 0.0;

/* This global variable allows a test to see if an option was actually given
   on the command, or if the value it has is from the defaults.  */

int options_present = 0; /* Bit set from masks below when option is given */
#define ARG_port 1
#define ARG_bind0 2
#define ARG_bind1 4
#define ARG_mtu 8
#define ARG_connection 16
#define ARG_operation 32
#define ARG_latency 64
#define ARG_size 128
#define ARG_all 256
#define ARG_tx_depth 512
#define ARG_rx_depth 1024
#define ARG_qp_timeout 2048
#define ARG_number 4096
#define ARG_duration 8192
#define ARG_max_rd_adomic 16384
#define ARG_max_dest_rd_atomic 32768
#define ARG_min_rnr_timer 65536
#define ARG_timeout 131072
#define ARG_retry_cnt 262144
#define ARG_rnr_retry 524288
#define ARG_bandwidth 1048576
#define ARG_report 2097152
#define ARG_cqperqp 4194304


/* Structure used during connection to communicate test parameters from   */
/* the initiation (client) side to the responder (server) side.   */
/* The maximum size of the structure is 56 bytes for Infiniband.  */

/* Currently this structure is 52 bytes for x86_64 architectures */

typedef struct {
  uint8_t version;    /* Identify version of structure in first byte */
#define IB_BENCH_VERSION 1	/* Change when the structure changes */
  uint8_t arg_all;	/* Send all arguments to the other side */
  uint8_t arg_latency;
  uint8_t arg_operation;
  uint16_t arg_max_dest_rd_atomic;
  uint16_t arg_max_rd_atomic;
  uint16_t arg_min_rnr_timer;
  uint16_t arg_mtu;
  uint16_t arg_retry_cnt;
  uint16_t arg_rnr_retry;
  uint16_t arg_rx_depth;
  uint16_t arg_timeout;
  uint16_t arg_tx_depth;
  uint32_t arg_number;
  uint32_t arg_qp_timeout;
  uint32_t arg_size;
  uint32_t rkey;		/* These 3 returned during accept */
  uint32_t remote_addr_low;
  uint32_t remote_addr_high;
#define ud_send_qkey rkey	/* For UD SEND, reuse fields */
#define ud_send_qpn remote_addr_low
  float arg_bandwidth;
} ib_bench_conn_data;


/* Global Variables */

char *prog_name;		/* Holds a copy of argv[0] */

int not_exiting = 1;		/* True until we are exiting */

char *connect_to_0 = 0;		/* Text version of destination IP address */
char *connect_to_1 = 0;
struct sockaddr_in sa_connect_to_0; /* Binary of connect_to_0 IP address */
struct sockaddr_in sa_connect_to_1;

struct verbs_per_dev {
  /* Elements created for all connections on a
  				   single rdma device */
  struct verbs_per_dev *next;  /* Pointer to the next device's info */
  struct ibv_context *verbs_dev; /* Identifies the unique device */
  char *dev_name;		/* Holds the name of the device */
  struct ibv_pd *pd;	/* Protection domain for this device */
  struct ibv_mr *mr;	/* Memory region for all possible areas */
  struct ibv_cq *cq;	/* Completion queue for device if not cqperqp */
  struct ibv_device_attr da;  /* ibv_query_device attributes */
  uint64_t min_latency;	/* Minimum single message latency in us */
  uint64_t max_latency;	/* Maximum single message latency in us */
  uint64_t total_latency;	/* Total latency of all messages in us */
  uint64_t total_bytes;	/* Total number of bytes transferred */
  uint64_t total_messages;  /* Total number of messages transferred */
  uint64_t lastd_latency;	/* Message latency at last perf display */
  uint64_t lastd_bytes;	/* Number of bytes at last perf display */
  uint64_t lastd_messages;  /* Number of messages at last perf display */
} *verbs_per_devs;		/* Pointer to the first device's information */

struct connection {		/* Information on the rdma_cm connections */
  struct connection *next;  /* Pointer to the next connection */
  struct connection *type_next;  /* Next connection of the same type */
  int state;		/* Current state of the connection */
#define CONN_DISCONNECTED 0	/* Not currently connected */
#define CONN_CONNECTING   1	/* Connection in progress */
#define CONN_RUNNING 	  2	/* Connection completed */
  int operation;		/* Holds BENCH_SEND, BENCH_WRITE, BENCH_READ, */
  /* or BENCH_RDWR for this connection */
  struct sockaddr_in sin;	/* Resolved address and socket number */
  struct rdma_cm_id *cm_id;  /* The rdma_cm ID for this connection */
  struct timespec *sentts;   /* Array of time values for sends done */
  uint32_t *sentsz;	/* Array of sizes for sends done */
  int next_send_idx;	/* Post send index into above arrays */
  int next_wc_idx;	/* Completion index into above arrays */
  struct verbs_per_dev *vpd;  /* The pd/cq/mr for this connection */
  struct ibv_cq *cq;	/* Completion queue for this QP if cqperqp */
  struct ibv_ah *other_ah;  /* UD target AH */
  uint32_t other_qp_num;	/* UD target QP number */
  uint32_t other_qkey;	/* UD target qkey */
  uint32_t rkey;		/* RDMA rkey for accessing passive side */
  uint64_t remote_addr;	/* RDMA passive side virtual address */
  uint32_t send_queued;	/* Total number of outstanding send WRs */
  uint32_t bytes_queued;	/* Total bytes in outstanding send WRs */
  uint64_t min_latency;	/* Minimum single message latency in us */
  uint64_t max_latency;	/* Maximum single message latency in us */
  uint64_t total_latency;	/* Total latency of all messages in us */
  uint64_t total_bytes;	/* Total number of bytes transferred */
  uint64_t total_messages;  /* Total number of messages transferred */
  uint64_t lastd_latency;	/* Message latency at last perf display */
  uint64_t lastd_bytes;	/* Number of bytes at last perf display */
  uint64_t lastd_messages;  /* Number of messages at last perf display */
} *connections;			/* Pointer to first entry for all IDs */

struct connection listen0, listen1;	/* Dummys for incoming connects */

int conn_disconnected = 0;	/* Count of QPs in CONN_DISCONNECTED state */
int conn_connecting = 0;	/* Count of QPs in CONN_CONNECTING state */
int conn_running = 0;		/* Count of QPs in CONN_RUNNING state */

ib_bench_conn_data conn_data;	/* Same connection data for every connect */

pthread_mutex_t con_mutex =	/* Lock multithread access to connection info */
  PTHREAD_MUTEX_INITIALIZER;

pthread_t event_thread_id;	/* Holds the thread ID for the event thread */

pthread_t display_thread_id;	/* Holds the thread ID for display thread */

struct rdma_event_channel *event_chan;  /* Single event channel for all */

int pagesize;		/* Holds the page size for this system */

struct timespec completion_time;  /* Time of day for completion loop start */

void *datamembase;	/* Points to allocated memory area */
off_t datamemsize;	/* Size of allocated memory area */

struct timespec startup;  /* The time when transfers started */

int first_incoming_connection = 1;	/* The first connection is accepted */
/* with its parameters, the rest are verified to be */
/* using the same parameters. */

/* Function to print an error message and exit */

void err_exit(const char *text) {
  not_exiting = 0;
  fprintf(stderr,
          "\nConnections:  Disconnected %d  Connecting %d  Running %d"
          "  Total %d\n",
          conn_disconnected, conn_connecting, conn_running,
          conn_disconnected + conn_connecting + conn_running);
  fprintf(stderr, "%s side: %s\n", connect_to_1 ? "Client" : "Server",
          text);
  /* This is a good place for a breakpoint if you are trying to track
     down the reason for an error exit */
  exit(1);
}

/* Function to catch SIGINT and error exit */

void catch_sigint(int x) {
  err_exit("Interrupted...");
}

/* Function to output the usage help text */

void usage() {
  fprintf(stderr,
          "\nUsage: %s [options] [serverIP1 [serverIP2]]\n%sVersion %s\n\n", prog_name,
          "Options:\n"
          "-p  --port N     The rdma_cm port number, Default is 18516\n"
          "    --bind0 IP   A local IP address to bind to for first HCA\n"
          "    --bind1 IP   A local IP address to bind to for second HCA\n"
          "-m  --mtu N      The MTU to use, Default is 2048\n"
          "-c  --connection RC/UD   The connection type, Default RC\n"
          "-o  --operation SEND/WRITE/READ  The type of test to run, Default SEND\n"
          "-l  --latency    Show performance as latency instead of bandwidth\n"
          "-s  --size N     The message size to use, Default is 65536\n"
          "-a  --all        Use all available message sizes in powers of 2\n"
          "-t  --tx-depth N  The depth of the transmit queue, Default 250\n"
          "-r  --rx-depth N  The depth of the receive queue, Default 500\n"
          "-u  --qp-timeout N  The timeout for the queue pairs\n"
          "-n  --number N   The number of queue pairs to test, Default 1\n"
          "-d  --duration N  The time to run the tests in seconds, Default forever\n"
          "    --report N   The time between performance reports, Default 6 seconds\n"
          "    --cqperqp     Create one CQ for each QP instead of one per device\n"
          "    --max_rd_atomic N   Responder resource control, Default 4\n"
          "    --max_dest_rd_atomic N   Responder resource control, Default 4\n"
          "    --min_rnr_timer N  Minimum RNR NAK timer\n"
          "    --timeout N  Local ack timeout\n"
          "    --retry_cnt N  Retry count, Default 6\n"
          "    --rnr_retry N  RNR retry, Default 6\n"
          "-b  --bandwidth F  The bandwidth limit for each connection in MB/second\n"
          "-x  --xsp_hop host/port  The address of the XSP daemon\n"
          "-h  --help         Display the usage help text\n\n"
          "Note: The --connection type must match on active and passive sides.\n"
          "      The optional --bind0 and --bind1 are local to each side.\n"
          "      All other parameters are ignored on the passive side, as they are\n"
          "      overwritten by the active's incoming connection.\n\n",
          IB_BENCH_VERSION_STRING);
}

/* Function to report that the command line is invalid and exit */

void usage_exit() {
  usage();
  err_exit("Invalid command line option/value\n");
}

/* Function to display the errno with descriptive text and exit */

void perror_exit(const char *text) {
  not_exiting = 0;
  perror(text);
  err_exit("Giving up...");
}

/* Function to print the time of day and a message */

void timestamp_msg(char *text) {
  char ts[20];
  time_t t;

  t = time(NULL);
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));
  printf("%s  %s\n", ts, text);
  fflush(stdout);
}

/* This function will find or create the struct verbs_per_dev needed for the
   device that a connection is using.  If it does not exist, it will be
   created.  Depending on cqperqp, there will be one CQ created for the
   device, or a CQ will be created for the connection. */

void get_vpd_pd_cq_mr(struct connection *cn) {
  struct verbs_per_dev *vpd;
  int cqsize;

  for (vpd = verbs_per_devs; vpd; vpd = vpd->next)
    if (cn->cm_id->verbs == vpd->verbs_dev) break;

  if (vpd == NULL) {
    vpd = calloc(1, sizeof(*vpd));
    if (vpd == NULL)
      err_exit("get_vpd_pd_cq_mr() cannot allocate memory");

    vpd->verbs_dev = cn->cm_id->verbs;
    if (ibv_query_device(vpd->verbs_dev, &(vpd->da)))
      err_exit("get_vpd_pd_cq_mr() cannot ibv_query_device");

    vpd->dev_name =
      strdup(ibv_get_device_name(vpd->verbs_dev->device));

    vpd->pd = ibv_alloc_pd(vpd->verbs_dev);
    if (vpd->pd == NULL)
      err_exit("get_vpd_pd_cq_mr() cannot allocate PD");

    vpd->mr = ibv_reg_mr(vpd->pd, datamembase, datamemsize,
                         IBV_ACCESS_REMOTE_WRITE
                         | IBV_ACCESS_LOCAL_WRITE
                         | IBV_ACCESS_REMOTE_READ);
    if (vpd->mr == NULL)
      err_exit("get_vpd_pd_cq_mr() cannot create MR");

    vpd->min_latency = 0x7FFFFFFFFFFFFFFF;
    vpd->next = verbs_per_devs;
    verbs_per_devs = vpd;
  }

  if (connect_to_0)
    cqsize = arg_tx_depth;	/* active, sending side */
  else
    cqsize = arg_rx_depth;	/* passive, receiving side */
  cqsize = cqsize + 4;		/* Allow management packets */
  if (arg_cqperqp == 0) {		/* One CQ for all QPs on this device */
    if (vpd->cq == NULL) {
      cqsize = cqsize * arg_number;	/* Times number QPs */
      if (cqsize > vpd->da.max_cqe)	/* Clip CQ size */
        cqsize = vpd->da.max_cqe;
      vpd->cq = ibv_create_cq(vpd->verbs_dev, cqsize,
                              NULL, NULL, 0);
      if (vpd->cq == NULL)
        err_exit("get_vpd_pd_cq_mr() cannot create CQ");
    }
  }
  else {
    if (cqsize > vpd->da.max_cqe)	/* Clip CQ size to maximum */
      cqsize = vpd->da.max_cqe;
    cn->cq = ibv_create_cq(vpd->verbs_dev, cqsize, NULL, NULL, 0);
    if (cn->cq == NULL)
      err_exit("get_vpd_pd_cq_mr() cannot create CQ");
  }

  cn->vpd = vpd;
}

/* This function accepts an incoming connection */

void incoming_connect(struct rdma_cm_event e, void *private_data) {
  ib_bench_conn_data *cdata = private_data;
  struct connection *cn;
  struct rdma_conn_param conp;
  struct ibv_qp_init_attr iqpa;
  struct ibv_recv_wr wr, *bad_wr;
  struct ibv_sge sge;
  int i, j;

  if (e.param.conn.private_data_len < sizeof(ib_bench_conn_data))
    err_exit("Incoming connect with too little connect data");
  if (cdata->version != IB_BENCH_VERSION)
    err_exit("Incoming connect with wrong version number");
  if (first_incoming_connection) {
    first_incoming_connection = 0;
    arg_all = cdata->arg_all;
    arg_latency = cdata->arg_latency;
    arg_operation = cdata->arg_operation;
    arg_max_dest_rd_atomic = cdata->arg_max_dest_rd_atomic;
    arg_max_rd_atomic = cdata->arg_max_rd_atomic;
    arg_min_rnr_timer = cdata->arg_min_rnr_timer;
    arg_mtu = cdata->arg_mtu;
    arg_retry_cnt = cdata->arg_retry_cnt;
    arg_rnr_retry = cdata->arg_rnr_retry;
    arg_rx_depth = cdata->arg_rx_depth;
    arg_timeout = cdata->arg_timeout;
    arg_tx_depth = cdata->arg_tx_depth;
    arg_bandwidth = cdata->arg_bandwidth;
    arg_number = cdata->arg_number;
    arg_qp_timeout = cdata->arg_qp_timeout;
    arg_size = cdata->arg_size;
  }
  else if (arg_all != cdata->arg_all
           || arg_latency != cdata->arg_latency
           || arg_operation != cdata->arg_operation
           || arg_max_dest_rd_atomic != cdata->arg_max_dest_rd_atomic
           || arg_max_rd_atomic != cdata->arg_max_rd_atomic
           || arg_min_rnr_timer != cdata->arg_min_rnr_timer
           || arg_mtu != cdata->arg_mtu
           || arg_retry_cnt != cdata->arg_retry_cnt
           || arg_rnr_retry != cdata->arg_rnr_retry
           || arg_rx_depth != cdata->arg_rx_depth
           || arg_timeout != cdata->arg_timeout
           || arg_tx_depth != cdata->arg_tx_depth
           || arg_bandwidth != cdata->arg_bandwidth
           || arg_number != cdata->arg_number
           || arg_qp_timeout != cdata->arg_qp_timeout
           || arg_size != cdata->arg_size) {
    err_exit("Incoming connect with different parameters");
  }

  cn = calloc(1, sizeof(struct connection));
  if (cn == NULL)
    err_exit("Cannot allocate connection structure");
  cn->state = CONN_CONNECTING;
  pthread_mutex_lock(&con_mutex);
  conn_connecting++;
  cn->next = connections;
  connections = cn;
  pthread_mutex_unlock(&con_mutex);
  memcpy(&(cn->sin), rdma_get_peer_addr(e.id),
         sizeof(struct sockaddr_in));
  cn->cm_id = e.id;
  e.id->context = cn;
  if (cn->sin.sin_port != rdma_get_dst_port(e.id))
    err_exit("incoming_connect() port number mismatch");

  /* Setup normal PD, CQ, and MR for the device.  These
     are common to all connections */

  get_vpd_pd_cq_mr(cn);

  /* Create the queue pair for receiving the messages */

  memset(&iqpa, 0, sizeof(iqpa));
  /* incoming connection, this is passive side, so this only */
  /* receives data */
  iqpa.cap.max_send_wr = 1;
  iqpa.cap.max_recv_wr = arg_rx_depth;
  iqpa.cap.max_send_sge = 1;
  iqpa.cap.max_recv_sge = 1;
  iqpa.qp_context = cn;
  iqpa.sq_sig_all = 1;
  if (arg_connection == BENCH_CONN_RC)
    iqpa.qp_type = IBV_QPT_RC;
  else
    iqpa.qp_type = IBV_QPT_UD;
  if (arg_cqperqp == 0) {		/* One CQ for all QPs on this device */
    iqpa.send_cq = cn->vpd->cq;
    iqpa.recv_cq = cn->vpd->cq;
  }
  else {
    iqpa.send_cq = cn->cq;
    iqpa.recv_cq = cn->cq;
  }
  j = rdma_create_qp(cn->cm_id, cn->vpd->pd, &iqpa);
  if (j) {
    if (j != -1) errno = j;
    perror_exit("Cannot create incoming QP");
  }

  /* Post the receive buffers to the QP */

  sge.addr = (uint64_t)datamembase;
  sge.length = datamemsize;
  sge.lkey = cn->vpd->mr->lkey;
  for (i = 0; i < arg_rx_depth; i++) {
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uint64_t)cn;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    j = ibv_post_recv(cn->cm_id->qp, &wr, &bad_wr);
    if (j) perror_exit("Cannot ibv_post_recv for connection");
  }

  /* Now we accept the connection */

  /* incoming connection, this is passive side, so the */
  /* values are reversed from the outgoing connection */
  memset(&conp, 0, sizeof(conp));
  conp.retry_count = arg_retry_cnt;
  conp.rnr_retry_count = arg_rnr_retry;
  conp.responder_resources = arg_max_dest_rd_atomic;
  conp.initiator_depth = arg_max_rd_atomic;
  conp.private_data = cdata;
  conp.private_data_len = sizeof(ib_bench_conn_data);
  if (arg_connection == BENCH_CONN_UD) {
    struct ibv_qp_attr qpa;
    struct ibv_qp_init_attr qpia;

    j = ibv_query_qp(cn->cm_id->qp, &qpa, IBV_QP_QKEY, &qpia);
    if (j) {
      errno = j;
      perror_exit("Cannot ibv_query_qp for connection");
    }
    cdata->ud_send_qkey = qpa.qkey;
    cdata->ud_send_qpn = cn->cm_id->qp->qp_num;
    cn->state = CONN_RUNNING;	/* For UD, we will not see */
    pthread_mutex_lock(&con_mutex); /* RDMA_CM_EVENT_ESTABLISHED */
    conn_connecting--;
    conn_running++;
    pthread_mutex_unlock(&con_mutex);
  }
  else {
    cdata->rkey = cn->vpd->mr->rkey;
    cdata->remote_addr_low = (uint32_t)((uint64_t)datamembase);
    cdata->remote_addr_high = (uint32_t)((uint64_t)datamembase>>32);
  }
  j = rdma_accept(cn->cm_id, &conp);
  if (j) {
    if (j != -1) errno = j;
    perror_exit("Cannot rdma_accept incoming connection");
  }
}


/* This function is the top level for the event thread that processes all
   rdma_cm events posted to this process.  The associated rdma_cm id is
   always created with a context value that is a pointer to the struct
   connection defining the state of each rdma_cm connection.  This is
   examined to determine the proper course of action for the particular
   event reported. */

void * event_thread(void *ignored) {
  struct rdma_cm_event *event;
  struct rdma_cm_event e;
  void *private_data = 0;
  size_t max_private_data_len = 0;
  struct connection *cn;
  struct ibv_qp_init_attr iqpa;
  struct rdma_conn_param conp;
  struct connection **lh;
  int j;

  /* Endless loop gathering whatever events are presented */

  while (not_exiting && !rdma_get_cm_event(event_chan, &event)) {

    /* Make a copy of the event and ack it so we won't hang */

    memcpy(&e, event, sizeof(e));
    if (e.param.conn.private_data_len > max_private_data_len) {
      if (private_data) free(private_data);
      max_private_data_len = e.param.conn.private_data_len;
      private_data = malloc(max_private_data_len);
      if (private_data == NULL)
        err_exit("malloc() failed in event_thread()");
    }
    if (e.param.conn.private_data_len)
      memcpy(private_data, e.param.conn.private_data,
             e.param.conn.private_data_len);
    rdma_ack_cm_event(event);

    /* Find our struct connection and save last event/status */

    cn = e.id->context;
    if (e.event == RDMA_CM_EVENT_CONNECT_REQUEST)
      cn = e.listen_id->context;

    /* Dispatch based on the event */

    switch (e.event) {

    case RDMA_CM_EVENT_ADDR_RESOLVED:	/* event 0 */
      /* rdma_resolve_addr() work is done */

      j = rdma_resolve_route(cn->cm_id, 1000);
      if (j) {
        if (j != -1) errno = j;
        perror_exit(
          "Cannot start rdma_resolve_route process");
      }
      break;

    case RDMA_CM_EVENT_ADDR_ERROR:		/* event 1 */
      /* rdma_resolve_addr() work has failed.  We must destroy the
         rdma_cm ID used, as that is the only way to reset it. */

      err_exit("Unexpected RDMA_CM_EVENT_ADDR_ERROR");
      break;

    case RDMA_CM_EVENT_ROUTE_RESOLVED:	/* event 2 */
      /* rdma_resolve_route(() work is done */

      /* Setup normal PD, CQ, and MR for the device.  These
         are common to all connections, and the MR maps the
         shared memory segment. */

      get_vpd_pd_cq_mr(cn);

      /* Create the queue pair */

      /* This is the active side of things, the sender */

      memset(&iqpa, 0, sizeof(iqpa));
      iqpa.cap.max_send_wr = arg_tx_depth;
      iqpa.cap.max_recv_wr = 1;
      iqpa.cap.max_send_sge = 1;
      iqpa.cap.max_recv_sge = 1;
      iqpa.qp_context = cn;
      iqpa.sq_sig_all = 1;
      if (arg_connection == BENCH_CONN_RC)
        iqpa.qp_type = IBV_QPT_RC;
      else
        iqpa.qp_type = IBV_QPT_UD;
      if (arg_cqperqp == 0) {	/* One CQ for all QPs */
        iqpa.send_cq = cn->vpd->cq;
        iqpa.recv_cq = cn->vpd->cq;
      }
      else {
        iqpa.send_cq = cn->cq;
        iqpa.recv_cq = cn->cq;
      }
      j = rdma_create_qp(cn->cm_id, cn->vpd->pd, &iqpa);
      if (j) {
        if (j != -1) errno = j;
        perror_exit(
          "Cannot create QP");
      }

      /* Now we make the connection to the passive side */

      memset(&conp, 0, sizeof(conp));
      conp.retry_count = arg_retry_cnt;
      conp.rnr_retry_count = arg_rnr_retry;
      conp.responder_resources = arg_max_rd_atomic;
      conp.initiator_depth = arg_max_dest_rd_atomic;
      conp.private_data = &conn_data;
      conp.private_data_len = sizeof(conn_data);
      j = rdma_connect(cn->cm_id, &conp);
      if (j) {
        if (j != -1) errno = j;
        perror_exit(
          "Cannot start rdma_connect process");
      }
      break;

    case RDMA_CM_EVENT_ROUTE_ERROR:		/* event 3 */
      /* rdma_resolve_route() work has failed.  We must destroy the
         rdma_cm ID used, as that is the only way to reset it. */

      err_exit("RDMA_CM_EVENT_ROUTE_ERROR");
      break;

    case RDMA_CM_EVENT_CONNECT_REQUEST:	/* event 4 */
      /* Incoming connection request */

#ifdef SHOW_INCOMING_CONNECTS
    {
      struct sockaddr *a = rdma_get_peer_addr(e.id);
      uint16_t d = rdma_get_dst_port(e.id); /* remote port */
      uint16_t s = rdma_get_src_port(e.id); /* local port */
      uint16_t f = a->sa_family;
      struct sockaddr_in *i = (struct sockaddr_in *)a;
      char *aa = inet_ntoa(i->sin_addr);
      printf("Connect from AF %d addr %s port %d"
             " coming to my port %d\n",
             f, aa, ntohs(d), ntohs(s));
    }
#endif
    incoming_connect(e, private_data);
    break;

    case RDMA_CM_EVENT_CONNECT_RESPONSE:	/* event 5 */
      /* rdma_connect() work is done and there is no QP yet */

      /* This should never happen, since we create the QP
         before calling rdma_connect() */
      err_exit("Unexpected RDMA_CM_EVENT_CONNECT_RESPONSE");

    case RDMA_CM_EVENT_CONNECT_ERROR:	/* event 6 */
      /* Connection establishment failed */

      /* This is very unusual, and there is no standard test
         case developed, but handle it gracefully. */
      err_exit("RDMA_CM_EVENT_CONNECT_ERROR");
      break;

    case RDMA_CM_EVENT_UNREACHABLE:		/* event 7 */
      /* rdma_connect() failed with unreachable or unresponsive */
      /* This can also happen when attempting rdma_accept() */

      err_exit("RDMA_CM_EVENT_UNREACHABLE");
      break;

    case RDMA_CM_EVENT_REJECTED:		/* event 8 */
      /* rdma_connect() or rdma_accept() rejected by remote side */

    {
      char msg[64];

      sprintf(msg, "RDMA_CM_EVENT_REJECTED %d",
              e.status);
      err_exit(msg);
    }
    break;

    case RDMA_CM_EVENT_ESTABLISHED:		/* event 9 */
      /* rdma_connect() or rdma_accept() work is done */

      if (arg_connection == BENCH_CONN_UD) {
        cn->other_ah = ibv_create_ah(cn->vpd->pd,
                                     &(e.param.ud.ah_attr));
        if (cn->other_ah == NULL)
          err_exit("Cannot create UD address "
                   "handle");
        cn->other_qp_num = e.param.ud.qp_num;
        cn->other_qkey = e.param.ud.qkey;
        /* These two (3?) lines are to be removed: */
        /* cn->remote_qpn = cdata->ud_send_qpn; */
        /* cn->remote_qkey = cdata->ud_send_qkey; */
      }
      if (connect_to_0) {	/* Active side */
        ib_bench_conn_data *cdata = private_data;

        if (e.param.conn.private_data_len <
            sizeof(ib_bench_conn_data))
          err_exit("Connection established with "
                   "too little connect data");
        if (cdata->version != IB_BENCH_VERSION)
          err_exit("Connection established with "
                   "wrong version number");
        cn->rkey = cdata->rkey;
        cn->remote_addr = cdata->remote_addr_high;
        cn->remote_addr = (cn->remote_addr) << 32;
        cn->remote_addr += cdata->remote_addr_low;
      }
      cn->state = CONN_RUNNING;
      pthread_mutex_lock(&con_mutex);
      conn_connecting--;
      conn_running++;
      pthread_mutex_unlock(&con_mutex);
      break;

    case RDMA_CM_EVENT_DISCONNECTED:	/* event 10 */
      /* The connection has been disconnected */

      /* Just pull the structure from the list, don't */
      /* bother to do the hard work, since we will be */
      /* exiting soon.  This simplifies a lot of stuff, */
      /* since the data structures remain intact. */
      pthread_mutex_lock(&con_mutex);
      lh = &connections;
      while (*lh != cn && *lh) lh = &((*lh)->next);
      if (*lh == cn) *lh = (*lh)->next;
      switch (cn->state) {
      case CONN_DISCONNECTED:
        break;
      case CONN_CONNECTING:
        conn_connecting--;
        conn_disconnected++;
        break;
      case CONN_RUNNING:
        conn_running--;
        conn_disconnected++;
        break;
      }
      cn->state = CONN_DISCONNECTED;
      pthread_mutex_unlock(&con_mutex);
      break;

    case RDMA_CM_EVENT_DEVICE_REMOVAL:	/* event 11 */
      /* The local RDMA device has been removed */

      /* If we are going to handle this, we have to shut
         down all connections using the device. */
      err_exit("Unexpected RDMA_CM_EVENT_DEVICE_REMOVAL");

    case RDMA_CM_EVENT_MULTICAST_JOIN:	/* event 12 */
      /* rdma_join_multicast() work is done */

      err_exit("Unexpected RDMA_CM_EVENT_MULTICAST_JOIN");
      break;

    case RDMA_CM_EVENT_MULTICAST_ERROR:	/* event 13 */
      /* The specified multicast group is no longer accessible */

      err_exit("Unexpected RDMA_CM_EVENT_MULTICAST_ERROR");
      break;

    case RDMA_CM_EVENT_ADDR_CHANGE:		/* event 14 */
      /* The network device changed its HW address */

      /* If we are going to handle this, we have to shut
         down all connections using the device. */
      err_exit("Unexpected RDMA_CM_EVENT_ADDR_CHANGE");

    case RDMA_CM_EVENT_TIMEWAIT_EXIT:	/* event 15 */
      /* The QP is now ready to be reused */

      /* We may need to handle this more gracefully, but to
         do a proper job we need a test case. */
      err_exit("Unexpected RDMA_CM_EVENT_TIMEWAIT_EXIT");
    }
  }

  if (not_exiting)
    err_exit("Cannot get a response from rdma_get_cm_event");
  return 0;
}

/* Show performance in bandwidth terms */

void display_performance_bw() {
  struct timespec now;
  double secs_running;
  double csecs_running;
  static double lastd_secs_running = 0.0;
  double total_mbs, minimum_mbs, maximum_mbs, mbs;
  double total_cmbs, minimum_cmbs, maximum_cmbs, cmbs;
  struct connection *cn;
  struct verbs_per_dev *vpd;
  int num_running, num_conns;

  clock_gettime(CLOCK_REALTIME, &now);
  csecs_running = now.tv_nsec - startup.tv_nsec;
  csecs_running = csecs_running / 1000000000.0;
  csecs_running = csecs_running + (now.tv_sec - startup.tv_sec);
  secs_running = csecs_running - lastd_secs_running;
  lastd_secs_running = csecs_running;

  printf("Device Performance:\n");
  total_mbs = 0.0;
  total_cmbs = 0.0;
  for (vpd = verbs_per_devs; vpd; vpd = vpd->next) {
    mbs = (double)(vpd->total_bytes - vpd->lastd_bytes);
    cmbs = (double)(vpd->total_bytes);
    vpd->lastd_bytes = vpd->total_bytes;
    vpd->lastd_messages = vpd->total_messages;
    mbs = mbs / 1000000.0 / secs_running;
    cmbs = cmbs / 1000000.0 / csecs_running;
    printf("  %15s  %f MB/s  //  %f MB/s\n",
           vpd->dev_name, mbs, cmbs);
    total_mbs += mbs;
    total_cmbs += cmbs;
  }
  if (verbs_per_devs && verbs_per_devs->next)
    printf("  %15s  %f MB/s  //  %f MB/s\n",
           "DevTotal", total_mbs, total_cmbs);

  printf("Queue Pair Performance:\n");
  total_mbs = 0.0;
  total_cmbs = 0.0;
  minimum_mbs = 999999999999999.0;
  maximum_mbs = 0.0;
  minimum_cmbs = 999999999999999.0;
  maximum_cmbs = 0.0;
  num_running = 0;
  num_conns = 0;
  for (cn = connections; cn; cn = cn->next) {
    num_conns++;
    cmbs = (double)(cn->total_bytes);
    mbs = (double)(cn->total_bytes - cn->lastd_bytes);
    cn->lastd_bytes = cn->total_bytes;
    cn->lastd_messages = cn->total_messages;
    mbs = mbs / 1000000.0 / secs_running;
    cmbs = cmbs / 1000000.0 / csecs_running;
    if (cn->state == CONN_RUNNING) {
      num_running++;
      if (mbs < minimum_mbs) minimum_mbs = mbs;
      if (mbs > maximum_mbs) maximum_mbs = mbs;
      if (cmbs < minimum_cmbs) minimum_cmbs = cmbs;
      if (cmbs > maximum_cmbs) maximum_cmbs = cmbs;
    }
    if (arg_number <= 8)
      printf("             QP %d  %f MB/s  //  %f MB/s\n",
             num_conns, mbs, cmbs);
    total_mbs += mbs;
    total_cmbs += cmbs;
  }
  if (connections && connections->next) {
    printf("  %15s  %f MB/s  //  %f MB/s\n",
           "QPTotal", total_mbs, total_cmbs);
    if (num_running < num_conns)	/* Won't happen since we wait */
      printf("  %15s  %f MB/s  //  %f MB/s\n",
             "Average All", total_mbs / num_conns,
             total_cmbs / num_conns);
    printf("  %15s  %f MB/s  //  %f MB/s\n",
           "Average Running", total_mbs / num_running,
           total_cmbs / num_running);
    if (arg_number > 8) {
      printf("  %15s  %f MB/s  //  %f MB/s\n",
             "Minimum", minimum_mbs, minimum_cmbs);
      printf("  %15s  %f MB/s  //  %f MB/s\n",
             "Maximum", maximum_mbs, maximum_cmbs);
    }
  }
  fflush(stdout);
}

/* Show performance in latency terms */

void display_performance_lat() {
  double total_lat, minimum_lat, maximum_lat, lat;
  double total_clat, minimum_clat, maximum_clat, clat;
  struct connection *cn;
  struct verbs_per_dev *vpd;
  int num_running, num_conns, num_devs;
  uint64_t minl, maxl;

  printf("Device Performance:\n");
  total_lat = 0.0;
  total_clat = 0.0;
  num_devs = 0;
  for (vpd = verbs_per_devs; vpd; vpd = vpd->next) {
    lat = (double)(vpd->total_latency - vpd->lastd_latency)
          / (double)(vpd->total_messages - vpd->lastd_messages);
    clat = (double)(vpd->total_latency)
           / (double)(vpd->total_messages);
    vpd->lastd_latency = vpd->total_latency;
    vpd->lastd_messages = vpd->total_messages;
    printf("  %15s  %.1f usec  //  %.1f usec  min %llu  max %llu\n",
           vpd->dev_name, lat, clat,
           (long long unsigned int)vpd->min_latency,
           (long long unsigned int)vpd->max_latency);
    total_lat += lat;
    total_clat += clat;
    num_devs++;
  }
  if (verbs_per_devs && verbs_per_devs->next)
    printf("  %15s  %.1f usec  //  %.1f usec\n",
           "DevAverage", total_lat / (double)num_devs,
           total_clat / (double)num_devs);

  printf("Queue Pair Performance:\n");
  total_lat = 0.0;
  total_clat = 0.0;
  minimum_lat = 999999999999999.0;
  maximum_lat = 0.0;
  minimum_clat = 999999999999999.0;
  maximum_clat = 0.0;
  minl = 0x7FFFFFFFFFFFFFFF;
  maxl = 0;
  num_running = 0;
  num_conns = 0;
  for (cn = connections; cn; cn = cn->next) {
    num_conns++;
    clat = (double)(cn->total_latency)
           / (double)(cn->total_messages);
    lat = (double)(cn->total_latency - cn->lastd_latency)
          / (double)(cn->total_messages - cn->lastd_messages);
    cn->lastd_latency = cn->total_latency;
    cn->lastd_messages = cn->total_messages;
    if (cn->state == CONN_RUNNING) {
      num_running++;
      if (lat < minimum_lat) minimum_lat = lat;
      if (lat > maximum_lat) maximum_lat = lat;
      if (clat < minimum_clat) minimum_clat = clat;
      if (clat > maximum_clat) maximum_clat = clat;
    }
    if (cn->min_latency < minl) minl = cn->min_latency;
    if (cn->max_latency > maxl) maxl = cn->max_latency;
    if (arg_number <= 8)
      printf("             QP %d  %.1f usec  "
             "min %llu  max %llu  //  %.1f usec\n",
             num_conns, lat,
             (long long unsigned int)cn->min_latency,
             (long long unsigned int)cn->max_latency,
             clat);
    cn->min_latency = 0x7FFFFFFFFFFFFFFF;
    cn->max_latency = 0;
    total_lat += lat;
    total_clat += clat;
  }
  if (connections && connections->next) {
    printf("  %15s  %.1f usec  //  %.1f usec\n",
           "QPAverage", total_lat / (double)num_running,
           total_clat / (double)num_running);
    if (num_running < num_conns)	/* Won't happen since we wait */
      printf("  %15s  %.1f usec  //  %.1f usec\n",
             "Average All", total_lat / (double)num_conns,
             total_clat / (double)num_conns);
    if (arg_number > 8) {
      printf("  %15s  %.1f usec  //  %.1f usec\n",
             "Minimum", minimum_lat, minimum_clat);
      printf("  %15s  %.1f usec  //  %.1f usec\n",
             "Maximum", maximum_lat, maximum_clat);
      printf("  %15s  min %llu  max %llu\n",
             "SingleMsg",
             (long long unsigned int)minl,
             (long long unsigned int)maxl);
    }
  }
  fflush(stdout);
}

/* Print out the time of day header for a performance report and adjust size
   of messages if --all is specified. */

void display_performance_hdr() {
  char msg[128];

  if (arg_all == 0 || connect_to_0)	/* Active side knows size */
    sprintf(msg, "Report -c %s -o %s -t %d -r %d -n %d -s %d",
            arg_connection == BENCH_CONN_RC ? "RC" : "UD",
            arg_operation == BENCH_SEND ? "Send" :
            arg_operation == BENCH_WRITE ? "Write" : "Read",
            arg_tx_depth, arg_rx_depth, arg_number, arg_size);
  else					/* Passive for -a */
    sprintf(msg, "Report -c %s -o %s -t %d -r %d -n %d -a",
            arg_connection == BENCH_CONN_RC ? "RC" : "UD",
            arg_operation == BENCH_SEND ? "Send" :
            arg_operation == BENCH_WRITE ? "Write" : "Read",
            arg_tx_depth, arg_rx_depth, arg_number);
  if (arg_all) {
    arg_size += arg_size;
    if (arg_connection == BENCH_CONN_UD) {
      if (arg_size > arg_mtu) arg_size = arg_mtu;
    }
    else {
      if (arg_size > BENCH_MAX_MSG) arg_size = BENCH_MIN_MSG;
    }
  }
  timestamp_msg(msg);
}

/* Thread that watches the time, doing performance reports and stopping if a
   duration is specified. */

void * display_thread(void *ignored) {
  struct timespec interval;
  struct timespec stop_time;
  struct timespec next_printout;
  struct timespec now;

  /* Go away if passive side of RDMA reads or writes */
  if (connect_to_0 == NULL && arg_operation != BENCH_SEND) return NULL;

  /* Go away if passive side of latency reporting */
  if (connect_to_0 == NULL && arg_latency) return NULL;

  /* Go away if nobody wants us to do anything */
  if (arg_duration == 0 && arg_report == 0) return NULL;

  stop_time = startup;
  stop_time.tv_sec += arg_duration;

  next_printout = startup;
  next_printout.tv_sec += arg_report;

  while (not_exiting) {
    clock_gettime(CLOCK_REALTIME, &now);
    if (arg_report
        && (now.tv_sec > next_printout.tv_sec ||
            (now.tv_sec == next_printout.tv_sec
             && now.tv_nsec > next_printout.tv_nsec))) {

      if (conn_running > 0) {
        display_performance_hdr();
        if (arg_latency)
          display_performance_lat();
        else
          display_performance_bw();
      }
      next_printout.tv_sec += arg_report;
      continue;
    }
    if (arg_duration
        && (now.tv_sec > stop_time.tv_sec ||
            (now.tv_sec == stop_time.tv_sec
             && now.tv_nsec > stop_time.tv_nsec))) {
      not_exiting = 0;
      return 0;
    }
    if (arg_report) {
      interval.tv_nsec = next_printout.tv_nsec - now.tv_nsec;
      interval.tv_sec = next_printout.tv_sec - now.tv_sec;
      if (interval.tv_nsec < 0) {
        interval.tv_nsec += 1000000000;
        interval.tv_sec -= 1;
      }
    }
    if (arg_duration) {
      now.tv_nsec = stop_time.tv_nsec - now.tv_nsec;
      now.tv_sec = stop_time.tv_sec - now.tv_sec;
      if (now.tv_nsec < 0) {
        now.tv_nsec += 1000000000;
        now.tv_sec -= 1;
      }
      if (arg_report == 0
          || now.tv_sec < interval.tv_sec
          || (now.tv_sec == interval.tv_sec
              && now.tv_nsec < interval.tv_nsec))
        interval = now;
    }
    clock_nanosleep(CLOCK_REALTIME, 0, &interval, NULL);
  }
  return 0;
}

/* Remove one (or zero) completion from a CQ, returns the number removed */

int remove_one_wc(struct ibv_cq *cq) {
  char errmsg[128];
  int j;
  struct ibv_wc wc;
  struct ibv_recv_wr rwr, *bad_rwr;
  struct ibv_sge sge;
  struct connection *cn;
  uint32_t t;

  if (cq == NULL) return 0;	/* No CQ to poll yet */

  sge.addr = (uint64_t)datamembase;

  j = ibv_poll_cq(cq, 1, &wc);
  if (j < 0) err_exit("Failed ibv_poll_cq");
  if (j == 0) return 0;
  cn = (struct connection *)(wc.wr_id);
  if (cn->state != CONN_RUNNING) return 1;
  if (wc.status != IBV_WC_SUCCESS) {
    sprintf(errmsg, "Work Completion Failed"
            ": %d", wc.status);
    err_exit(errmsg);
  }
  cn->total_messages++;
  cn->vpd->total_messages++;
  if (connect_to_0) {
    /* Active side, show smaller queue */
    t = cn->sentsz[cn->next_wc_idx];
    cn->send_queued--;
    cn->bytes_queued -= t;
    cn->total_bytes += t;
    cn->vpd->total_bytes += t;
    if (arg_latency) {
      struct timespec now;
      uint64_t lat;

      clock_gettime(CLOCK_REALTIME, &now);
      lat = now.tv_nsec - cn->sentts[cn->next_wc_idx].tv_nsec;
      lat += (now.tv_sec - cn->sentts[cn->next_wc_idx].tv_sec)
             * 1000000000;
      lat = (lat + 500) / 1000;	/* Microsecond units */
      if (lat < cn->vpd->min_latency)
        cn->vpd->min_latency = lat;
      if (lat > cn->vpd->max_latency)
        cn->vpd->max_latency = lat;
      if (lat < cn->min_latency)
        cn->min_latency = lat;
      if (lat > cn->max_latency)
        cn->max_latency = lat;
      cn->total_latency += lat;
      cn->vpd->total_latency += lat;
    }
    cn->next_wc_idx++;
    if (cn->next_wc_idx >= arg_tx_depth)
      cn->next_wc_idx = 0;
  }
  else {
    /* Passive side, requeue receive */
    cn->total_bytes += wc.byte_len;
    cn->vpd->total_bytes += wc.byte_len;
    if (arg_connection == BENCH_CONN_UD) {
      cn->total_bytes -= 40;
      cn->vpd->total_bytes -= 40;
    }
    sge.length = datamemsize;
    sge.lkey = cn->vpd->mr->lkey;
    memset(&rwr, 0, sizeof(rwr));
    rwr.wr_id = (uint64_t)cn;
    rwr.next = NULL;
    rwr.sg_list = &sge;
    rwr.num_sge = 1;
    j = ibv_post_recv(cn->cm_id->qp, &rwr,
                      &bad_rwr);
    if (j) perror_exit("Cannot ibv_post_re"
                         "cv for repost");
  }
  return 1;
}

/* Initiate the I/O, process the CQs and wait for all to finish */

void wait_for_all_finished() {
  struct connection *cn;
  int i, j;
  uint64_t bytes_allowed;		/* Calculated bandwidth limit */
  double dtmp;
  struct timespec now;
  struct ibv_send_wr wr, *bad_wr;
  struct ibv_sge sge;
  struct verbs_per_dev *vpd;

  /* Now note the startup time and start the time-based thread */
  clock_gettime(CLOCK_REALTIME, &startup);
  j = pthread_create(&display_thread_id, NULL, display_thread, NULL);
  if (j) err_exit("Cannot create display thread");

  sge.addr = (uint64_t)datamembase;

  while (not_exiting && connections) {	/* Now wait for all to end */

    if (connect_to_0) {	/* Active side, post any sends */

      if (arg_bandwidth == 0.0) {
        bytes_allowed = 0xFFFFFFFFFFFFFFFF;
      }
      else {
        clock_gettime(CLOCK_REALTIME, &now);
        dtmp = now.tv_nsec - startup.tv_nsec;
        dtmp = dtmp / 1000000000.0;
        dtmp = dtmp + (now.tv_sec - startup.tv_sec);
        dtmp = dtmp * arg_bandwidth;
        dtmp = dtmp * 1000000.0;
        bytes_allowed = dtmp;
      }

      for (cn = connections; cn; cn = cn->next) {

        if (arg_cqperqp)  /* Empty CQ per connection */
          while (remove_one_wc(cn->cq)) ;

        while (cn->send_queued < arg_tx_depth
               && (cn->total_bytes + cn->bytes_queued)
               < bytes_allowed) {
          sge.length = arg_size;
          sge.lkey = cn->vpd->mr->lkey;
          memset(&wr, 0, sizeof(wr));
          wr.wr_id = (uint64_t)cn;
          wr.next = NULL;
          wr.sg_list = &sge;
          wr.num_sge = 1;
          switch (cn->operation) {
          case BENCH_SEND:
            if (arg_connection ==
                BENCH_CONN_UD) {
              wr.wr.ud.ah =
                cn->other_ah;
              wr.wr.ud.remote_qpn =
                cn->other_qp_num;
              wr.wr.ud.remote_qkey =
                cn->other_qkey;
            }
            wr.opcode = IBV_WR_SEND;
            break;
          case BENCH_WRITE:
            wr.wr.rdma.remote_addr =
              cn->remote_addr;
            wr.wr.rdma.rkey = cn->rkey;
            wr.opcode = IBV_WR_RDMA_WRITE;
            break;
          case BENCH_READ:
            wr.wr.rdma.remote_addr =
              cn->remote_addr;
            wr.wr.rdma.rkey = cn->rkey;
            wr.opcode = IBV_WR_RDMA_READ;
            break;
          }
          wr.send_flags = IBV_SEND_SIGNALED
                          | IBV_SEND_SOLICITED;
          wr.imm_data = 0;
          cn->sentsz[cn->next_send_idx] =
            sge.length;
          if (arg_latency)
            clock_gettime(CLOCK_REALTIME, &
                          (cn->sentts[cn->next_send_idx]));
          cn->next_send_idx++;
          if (cn->next_send_idx >= arg_tx_depth)
            cn->next_send_idx = 0;
          i = ibv_post_send(cn->cm_id->qp, &wr,
                            &bad_wr);
          if (i) err_exit("ibv_post_send failed");
          cn->send_queued++;
          cn->bytes_queued += sge.length;
        }
      }
    }
    else {		/* Passive side, check receive done */
      if (arg_cqperqp)   /* Empty CQ if per connection */
        for (cn = connections; cn; cn = cn->next)
          while (remove_one_wc(cn->cq)) ;
    }

    /* Process all completions if there is one CQ per device */

    if (arg_cqperqp == 0)
      for (vpd = verbs_per_devs; vpd; vpd = vpd->next)
        while (remove_one_wc(vpd->cq)) ;
  }
}

/* Print out the initial startup message */

void show_startup(char *what, struct sockaddr_in *s1, struct sockaddr_in *s2) {
  char *f0ad;
  char *f1ad;
  char msg[128];

  f0ad = strdup(inet_ntoa(s1->sin_addr));
  if (s2) f1ad = strdup(inet_ntoa(s2->sin_addr));
  snprintf(msg, sizeof(msg), "%s to %s%s%s", what, f0ad,
           (s1 && s2) ? " and " : "", s2 ? f1ad : "");
  timestamp_msg(msg);
  free(f0ad);
  if (s2) free(f1ad);
}

/* Run the passive side, the server receiving requests from client */

void passive_side() {
  int j;

  show_startup("Binding", &sa_bind0, arg_bind1 ? &sa_bind1 : 0);

  sa_bind0.sin_port = htons(arg_port);
  listen0.sin = sa_bind0;
  j = rdma_create_id(event_chan, &(listen0.cm_id), &listen0,
                     (arg_connection == BENCH_CONN_RC) ? RDMA_PS_TCP : RDMA_PS_UDP);
  if (j) perror_exit("Cannot create listen rdma_cm ID");
  j = rdma_bind_addr(listen0.cm_id, (struct sockaddr *)&(sa_bind0));
  if (j) perror_exit("Cannot do rdma_bind_addr on listen rdma_cm ID");
  j = rdma_listen(listen0.cm_id, MAX_LISTEN_QUEUE);
  if (j) perror_exit("Cannot do rdma_listen on rdma_cm ID");

  if (arg_bind1) {
    sa_bind1.sin_port = htons(arg_port);
    listen1.sin = sa_bind1;
    j = rdma_create_id(event_chan, &(listen1.cm_id), &listen1,
                       (arg_connection == BENCH_CONN_RC) ? RDMA_PS_TCP
                       : RDMA_PS_UDP);
    if (j) perror_exit("Cannot create listen 1 rdma_cm ID");
    j = rdma_bind_addr(listen1.cm_id,
                       (struct sockaddr *)&(sa_bind1));
    if (j) perror_exit(
        "Cannot do rdma_bind_addr on listen 1 rdma_cm ID");
    j = rdma_listen(listen1.cm_id, MAX_LISTEN_QUEUE);
    if (j) perror_exit(
        "Cannot do rdma_listen on listen 1 rdma_cm ID");
  }

  while (connections == 0) {	/* First wait for some to exist */
    sched_yield();
  }
  timestamp_msg("Connections arriving");

#ifndef DO_NOT_WAIT_FOR_ALL_TO_BE_RUNNING
  /* This would do a cleaner job of showing performance data, but would
     also mean that this program would not continue until all planned
     connections actually happened. */

  while (conn_running < arg_number)
    sched_yield();
  timestamp_msg("All incoming connections are running");
#endif

  wait_for_all_finished();
}

/* Run the active side, the client connecting to a server */

void active_side() {
  struct connection *cn;
  void *conmem;
  int i, j;

  if (arg_bind0) {
    show_startup("Binding", &sa_bind0,
                 connect_to_1 ? &sa_bind1 : 0);
    sa_bind0.sin_port = htons(0);
    sa_bind1.sin_port = htons(0);
  }

  sa_connect_to_0.sin_port = htons(arg_port);
  sa_connect_to_1.sin_port = htons(arg_port);
  show_startup("Connecting", &sa_connect_to_0,
               connect_to_1 ? &sa_connect_to_1 : 0);
  conn_data.version = IB_BENCH_VERSION;
  conn_data.arg_all = arg_all;
  conn_data.arg_latency = arg_latency;
  conn_data.arg_operation = arg_operation;
  conn_data.arg_max_dest_rd_atomic = arg_max_dest_rd_atomic;
  conn_data.arg_max_rd_atomic = arg_max_rd_atomic;
  conn_data.arg_min_rnr_timer = arg_min_rnr_timer;
  conn_data.arg_mtu = arg_mtu;
  conn_data.arg_retry_cnt = arg_retry_cnt;
  conn_data.arg_rnr_retry = arg_rnr_retry;
  conn_data.arg_rx_depth = arg_rx_depth;
  conn_data.arg_timeout = arg_timeout;
  conn_data.arg_tx_depth = arg_tx_depth;
  conn_data.arg_bandwidth = arg_bandwidth;
  conn_data.arg_number = arg_number;
  conn_data.arg_qp_timeout = arg_qp_timeout;
  conn_data.arg_size = arg_size;

  for (i = 0; i < arg_number; i++) {
    while (conn_connecting >= MAX_CONNECT_QUEUE)
      sched_yield();
    conmem = calloc(1, sizeof(uint32_t) * arg_tx_depth
                    + sizeof(struct timespec) * arg_tx_depth * arg_latency
                    + sizeof(struct connection));
    if (conmem == NULL)
      err_exit("Cannot allocate connection structure");
    cn = conmem + sizeof(uint32_t) * arg_tx_depth
         + sizeof(struct timespec) * arg_tx_depth * arg_latency;
    cn->sentts = conmem + sizeof(uint32_t) * arg_tx_depth;
    cn->sentsz = conmem;
    cn->state = CONN_CONNECTING;
    cn->operation = arg_operation;
    if (arg_operation == BENCH_RDWR) {
      if (connect_to_1) {
        if (i & 2) cn->operation = BENCH_READ;
        else cn->operation = BENCH_WRITE;
      }
      else {
        if (i & 1) cn->operation = BENCH_READ;
        else cn->operation = BENCH_WRITE;
      }
    }
    cn->min_latency = 0x7FFFFFFFFFFFFFFF;
    pthread_mutex_lock(&con_mutex);
    conn_connecting++;
    cn->next = connections;
    connections = cn;
    pthread_mutex_unlock(&con_mutex);
    j = rdma_create_id(event_chan, &(cn->cm_id), cn,
                       (arg_connection == BENCH_CONN_RC) ? RDMA_PS_TCP
                       : RDMA_PS_UDP);
    if (j) perror_exit("Cannot create connection rdma_cm ID");

    if (arg_bind0) {
      if (arg_bind1 && (i&1))
        j = rdma_bind_addr(cn->cm_id,
                           (struct sockaddr *)&(sa_bind1));
      else
        j = rdma_bind_addr(cn->cm_id,
                           (struct sockaddr *)&(sa_bind0));
      if (j) perror_exit("Cannot do rdma_bind_addr on "
                           "connection rdma_cm ID");
    }

    j = rdma_resolve_addr(cn->cm_id, NULL,
                          (connect_to_1 && (i&1))
                          ? (struct sockaddr *)&sa_connect_to_1
                          : (struct sockaddr *)&sa_connect_to_0,
                          1000);
    if (j) {
      if (j != -1) errno = j;
      perror_exit("Cannot rdma_resolve_addr for connection");
    }
  }

  /* Now that all connections have been started, wait for them to be up */

  while (conn_running < arg_number)
    sched_yield();
  timestamp_msg("All outgoing connections are running");

  wait_for_all_finished();
}


/* Main entry point, parse the command and start things up */

int main(int argc, char **argv) {
  int c, j;
  char *ep;
  struct addrinfo *r;
  struct addrinfo hints = {
    .ai_family   = AF_INET,
    .ai_socktype = SOCK_STREAM
  };
  struct sigaction action;

#if 0	/* If you change the structure or architecture, check resulting size */
  printf("The size of ib_bench_conn_data is %d\n",
         sizeof(ib_bench_conn_data));
#endif

  prog_name = argv[0];

  pagesize = getpagesize();

  while(1) {
    static struct option long_options[] = {
      { .name = "port", .has_arg = 1, .val = 'p' },
      { .name = "bind0",.has_arg = 1,.val = 1 },
      { .name = "bind1", .has_arg = 1, .val = 2 },
      { .name = "mtu", .has_arg = 1, .val = 'm' },
      { .name = "connection", .has_arg = 1, .val = 'c' },
      { .name = "operation", .has_arg = 1, .val = 'o' },
      { .name = "latency", .has_arg = 0, .val = 'l' },
      { .name = "size", .has_arg = 1, .val = 's' },
      { .name = "all", .has_arg = 0, .val = 'a' },
      { .name = "tx-depth", .has_arg = 1, .val = 't' },
      { .name = "rx-depth", .has_arg = 1, .val = 'r' },
      { .name = "qp-timeout", .has_arg = 1, .val = 'u' },
      { .name = "number", .has_arg = 1, .val = 'n' },
      { .name = "duration", .has_arg = 1, .val = 'd' },
      { .name = "max_rd_atomic", .has_arg = 1, .val = 3 },
      { .name = "max_dest_rd_atomic", .has_arg = 1, .val = 4 },
      { .name = "min_rnr_timer", .has_arg = 1, .val = 5 },
      { .name = "timeout", .has_arg = 1, .val = 6 },
      { .name = "retry_cnt", .has_arg = 1, .val = 7 },
      { .name = "rnr_retry", .has_arg = 1, .val = 8 },
      { .name = "bandwidth", .has_arg = 1, .val = 'b' },
      { .name = "report", .has_arg = 1, .val = 9 },
      { .name = "cqperqp", .has_arg = 0, .val = 10 },
      { .name = "bind",.has_arg = 1,.val = 11 },
      { .name = "xsp_hop",.has_arg = 1,.val = 12 },
      { .name = "help", .has_arg = 0, .val = 'h' },
      { 0 }
    };

    c = getopt_long_only(argc, argv,
                         "p:m:c:o:ls:at:r:u:n:d:b:x:h", long_options, NULL);
    if (c == -1)
      break;

    switch (c) {

    case 'p':		/* port */
      arg_port = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_port;
      break;

    case 1:			/* bind0 */
bind0:
      arg_bind0 = optarg;
      options_present |= ARG_bind0;
      break;

    case 2:			/* bind1 */
bind1:
      arg_bind1 = optarg;
      options_present |= ARG_bind1;
      break;

    case 'm':		/* mtu */
      arg_mtu = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_mtu;
      break;

    case 'c':		/* connection */
      if (strncasecmp("RC", optarg, 3) == 0)
        arg_connection = BENCH_CONN_RC;
      else if (strncasecmp("UD", optarg, 3) == 0)
        arg_connection = BENCH_CONN_UD;
      else
        usage_exit();
      options_present |= ARG_connection;
      break;

    case 'o':		/* operation */
      if (strncasecmp("SEND", optarg, 5) == 0)
        arg_operation = BENCH_SEND;
      else if (strncasecmp("WRITE", optarg, 6) == 0)
        arg_operation = BENCH_WRITE;
      else if (strncasecmp("READ", optarg, 5) == 0)
        arg_operation = BENCH_READ;
      else if (strncasecmp("READWRITE", optarg, 10) == 0)
        arg_operation = BENCH_RDWR;
      else if (strncasecmp("WRITEREAD", optarg, 10) == 0)
        arg_operation = BENCH_RDWR;
      else
        usage_exit();
      options_present |= ARG_operation;
      break;

    case 'l':		/* latency */
      arg_latency++;
      options_present |= ARG_latency;
      break;

    case 's':		/* size */
      arg_size = strtoul(optarg, &ep, 0);
      if (*ep
          || arg_size < BENCH_MIN_MSG
          || arg_size >  BENCH_MAX_MSG)
        usage_exit();
      options_present |= ARG_size;
      break;

    case 'a':		/* all */
      arg_all++;
      options_present |= ARG_all;
      break;

    case 't':		/* tx-depth */
      arg_tx_depth = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_tx_depth;
      break;

    case 'r':		/* rx-depth */
      arg_rx_depth = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_rx_depth;
      break;

    case 'u':		/* qp-timeout */
      arg_qp_timeout = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_qp_timeout;
      break;

    case 'n':		/* number */
      arg_number = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_number;
      break;

    case 'd':		/* duration */
      arg_duration = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_duration;
      break;

    case 3:			/* max_rd_atomic */
      arg_max_rd_atomic = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_max_rd_adomic;
      break;

    case 4:			/* max_dest_rd_atomic */
      arg_max_dest_rd_atomic = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_max_dest_rd_atomic;
      break;

    case 5:			/* min_rnr_timer */
      arg_min_rnr_timer = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_min_rnr_timer;
      break;

    case 6:			/* timeout */
      arg_timeout = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_timeout;
      break;

    case 7:			/* retry_cnt */
      arg_retry_cnt = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_retry_cnt;
      break;

    case 8:			/* rnr_retry */
      arg_rnr_retry = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_rnr_retry;
      break;

    case 'b':		/* bandwidth */
      arg_bandwidth = strtof(optarg, &ep);
      if (*ep)
        usage_exit();
      options_present |= ARG_bandwidth;
      break;

    case 9:			/* report */
      arg_report = strtoul(optarg, &ep, 0);
      if (*ep)
        usage_exit();
      options_present |= ARG_report;
      break;

    case 10:		/* cqperqp */
      arg_cqperqp++;
      options_present |= ARG_cqperqp;
      break;

    case 11:		/* bind */
      if (options_present & ARG_bind0) {
        if (options_present & ARG_bind1) {
          fprintf(stderr, "--bind but both "
                  "--bind0 and --bind1 are set.");
          usage_exit();
        }
        else {
          goto bind1;
        }
      }
      else {
        goto bind0;
      }
      break;

    case 'x':
      xsp_hop = strdup(optarg);
      break;

    case 12:                /* XPS hop */
      xsp_hop = strdup(optarg);
      break;

    case 'h':		/* usage help text */
      usage();
      exit(0);

    default:	/* Unknown option */
      usage_exit();
    }
  }

  if (optind < argc)
    connect_to_0 = argv[optind++];
  if (optind < argc)
    connect_to_1 = argv[optind++];
  if (optind < argc)
    usage_exit();

  if (arg_connection == BENCH_CONN_UD && arg_operation != BENCH_SEND) {
    fprintf(stderr, "\nOnly SEND on UD connections.\n\n");
    exit(1);
  }

  if ((options_present & ARG_size) == 0
      && arg_connection == BENCH_CONN_UD)
    arg_size = arg_mtu;

  if (arg_all)
    arg_size = BENCH_MIN_MSG;

  if (arg_connection == BENCH_CONN_UD && arg_size > arg_mtu) {
    fprintf(stderr, "\nMaximum size on UD is the MTU.\n\n");
    exit(1);
  }

  if (arg_connection == BENCH_CONN_UD && arg_latency) {
    fprintf(stderr, "\nLatency measurements only supported on RC "
            "connections.\n\n");
    exit(1);
  }

  if (arg_bind1 && !arg_bind0) {
    fprintf(stderr, "\n--bind1 must also have --bind0 option.\n\n");
    exit(1);
  }

  /* Do XSP signaling for the dataplane before the trasfer starts */
#ifdef WITH_XSP
  libxspSess *sess;
  libxspSecInfo *sec;
  libxspNetPath *path;
  libxspNetPathRule *rule;

  if (xsp_hop) {
    if (libxsp_init() < 0) {
      perror("libxsp_init(): failed");
      exit(errno);
    }

    sess = xsp_session();
    if (!sess) {
      perror("xsp_session() failed");
      exit(errno);
    }

    xsp_sess_appendchild(sess, xsp_hop, XSP_HOP_NATIVE);

    sec = xsp_sess_new_security("ezra", NULL, "/home/ezra/.ssh/id_rsa_pl.pub",
                                "/home/ezra/.ssh/id_rsa_pl", NULL);

    if (xsp_sess_set_security(sess, sec, XSP_SEC_NONE)) {
      fprintf(stderr, "could not set requested xsp security method\n");
      exit(-1);
    }

    if (xsp_connect(sess)) {
      perror("xsp_client: connect failed");
      exit(errno);
    }

    path = xsp_sess_new_net_path(XSP_NET_PATH_CREATE);
    rule = xsp_sess_new_net_path_rule(path, "OSCARS");

    if (xsp_signal_path(sess, path) != 0)
      fprintf(stderr, "could not signal path\n");
  }
#endif

  if (arg_number == 0) {
    if (connect_to_1) arg_number = 2;
    else if (connect_to_0) arg_number = 1;
  }

  if (connect_to_0 == NULL && (options_present &
                               (ARG_mtu|ARG_operation|ARG_latency|ARG_size|ARG_all
                                |ARG_tx_depth|ARG_rx_depth|ARG_qp_timeout|ARG_number
                                |ARG_duration|ARG_max_rd_adomic|ARG_max_dest_rd_atomic
                                |ARG_min_rnr_timer|ARG_timeout|ARG_retry_cnt
                                |ARG_rnr_retry|ARG_bandwidth)))
    printf("Arguments given to passive side that will be "
           "overwritten by active connection.\n");

  /* Translate textual addresses for arg_bind0 and arg_bind1 to binary */

  memset(&sa_bind0, 0, sizeof(sa_bind0));
  sa_bind0.sin_family = AF_INET;
  memset(&sa_bind1, 0, sizeof(sa_bind1));
  sa_bind1.sin_family = AF_INET;
  if (arg_bind0) {
    j = getaddrinfo(arg_bind0, NULL, &hints, &r);
    if (j < 0) err_exit(gai_strerror(j));
    if (r->ai_family != AF_INET)
      err_exit("--bind0 not proper IPv4 address");
    sa_bind0.sin_addr =
      ((struct sockaddr_in *)(r->ai_addr))->sin_addr;
  }
  if (arg_bind1) {
    j = getaddrinfo(arg_bind1, NULL, &hints, &r);
    if (j < 0) err_exit(gai_strerror(j));
    if (r->ai_family != AF_INET)
      err_exit("--bind1 not proper IPv4 address");
    sa_bind1.sin_addr =
      ((struct sockaddr_in *)(r->ai_addr))->sin_addr;
  }

  /* Translate destination addresses to binary */

  if (connect_to_0) {
    j = getaddrinfo(connect_to_0, NULL, &hints, &r);
    if (j < 0) err_exit(gai_strerror(j));
    if (r->ai_family != AF_INET)
      err_exit("Destination not proper IPv4 address");
    sa_connect_to_0.sin_family = AF_INET;
    sa_connect_to_0.sin_addr =
      ((struct sockaddr_in *)(r->ai_addr))->sin_addr;
  }
  if (connect_to_1) {
    j = getaddrinfo(connect_to_1, NULL, &hints, &r);
    if (j < 0) err_exit(gai_strerror(j));
    if (r->ai_family != AF_INET)
      err_exit("Second destination not proper IPv4 address");
    sa_connect_to_1.sin_family = AF_INET;
    sa_connect_to_1.sin_addr =
      ((struct sockaddr_in *)(r->ai_addr))->sin_addr;
  }

  /* Allocate the one and only buffer we use for everything */

  datamemsize = BENCH_MAX_MSG;
  datamemsize = datamemsize + 40;	/* Always allow for an IB GRH */
  datamembase = calloc(1, datamemsize);
  if (datamembase == NULL) err_exit("Cannot allocate buffer memory");

  /* Set up to catch SIGINT and print information */

  memset(&action, 0, sizeof(action));
  action.sa_handler = catch_sigint;
  sigaction(SIGINT, &action, NULL); /* Silently ignore errors */

  /* Create the rdma_cm event channel and start the processing thread */

  event_chan = rdma_create_event_channel();
  if (event_chan == NULL) err_exit("Cannot create rdma_cm event channel");

  j = pthread_create(&event_thread_id, NULL, event_thread, NULL);
  if (j) err_exit("Cannot create event thread");

  if (connect_to_0)
    active_side();
  else
    passive_side();

#ifdef WITH_XSP
  if (xsp_hop)
    xsp_close2(sess);
#endif

  return 0;
}

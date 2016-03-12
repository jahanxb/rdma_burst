#ifndef PSBFER_H
#define PSBFER_H

#include <stdint.h>
#include "pthread.h"

#define FALSE            0
#define TRUE             1

#define PSB_READ         0
#define PSB_WRITE        1
#define PSB_CURR         2

#define PSB_NO_ERR       0x00
#define PSB_NOT_SENT     0x01
#define PSB_SENT_NO_ACK  0x02
#define PSB_SEND_FAIL    0x04
#define PSB_READ_FAIL    0x08
#define PSB_BACKEND_DONE 0x10
#define PSB_SEND_READY   0x20
#define PSB_RECV_READY   0x40

typedef struct psd_slabs_buf_entry_t {
	uint64_t size;
	void *base;
	void *ptr;
	void *priv;
	int empty;
	uint64_t read_amount;
	uint64_t write_amount;
	int status;
} bufEntry;

typedef struct psd_slabs_xfer_buf_t
{
	bufEntry **entries;
	int p_count;
	int r_index;
	int w_index;
	int s_index;
	
	int status;
	uint64_t size;
	uint64_t p_size;
	uint64_t total_count_bytes;

	pthread_mutex_t buf_lock;
	pthread_cond_t read_cond;
	pthread_cond_t write_cond;
} psdSLAB;

extern uint64_t buf_total_bytes;
extern pthread_mutex_t buf_total_lock;
extern pthread_cond_t buf_total_cond;
extern uint64_t PSD_SLAB_SIZE;

psdSLAB *psd_slabs_buf_create(size_t size, int partitions);
void psd_slabs_buf_free(psdSLAB *slab);
void psd_slabs_buf_reset(psdSLAB *slab);

void psd_slabs_buf_set_read_index(psdSLAB *slab, int ind);
void psd_slabs_buf_set_write_index(psdSLAB *slab, int ind);

void psd_slabs_buf_unset_pstatus(psdSLAB *slab, int status, int side);
void psd_slabs_buf_set_pstatus(psdSLAB *slab, int status, int side);
int psd_slabs_buf_get_pstatus(psdSLAB *slab, int side);
void psd_slabs_buf_set_status(psdSLAB *slab, int status);
int psd_slabs_buf_get_status(psdSLAB *slab);
uint64_t psd_slabs_buf_get_size(psdSLAB *slab);
uint64_t psd_slabs_buf_get_psize(psdSLAB *slab);
uint64_t psd_slabs_buf_get_pcount(psdSLAB *slab);

void psd_slabs_buf_set_priv_data_ind(psdSLAB *slab, void *data, int ind);
void *psd_slabs_buf_get_priv_data_ind(psdSLAB *slab, int ind);
void *psd_slabs_buf_get_priv_data(psdSLAB *slab, int side);

void psd_slabs_buf_wait_curr(psdSLAB *slab, int side);
void psd_slabs_buf_curr_swap(psdSLAB *slab);
void psd_slabs_buf_read_swap(psdSLAB *slab, int total);
void psd_slabs_buf_write_swap(psdSLAB *slab, int total);

void *psd_slabs_buf_addr(psdSLAB *slab, int side);
void *psd_slabs_buf_addr_ind(psdSLAB *slab, int ind);
void psd_slabs_buf_advance_curr(psdSLAB *slab, uint64_t bytes, int side);
void psd_slabs_buf_advance(psdSLAB *slab, uint64_t bytes, int side);
uint64_t psd_slabs_buf_count_bytes(psdSLAB *slab, int side);
uint64_t psd_slabs_buf_count_bytes_free(psdSLAB *slab, int side);

#endif

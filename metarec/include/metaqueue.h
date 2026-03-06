#ifndef	_METAQUEUE_H
#define _METAQUEUE_H

#include <pthread.h>

#include "metadata_def.h"

#define METADATA_DESC_QUEUE_SIZE	10	

typedef struct {
	int initialized ;
	metadata_desc_t buf[METADATA_DESC_QUEUE_SIZE] ;

	long head, tail ;
	int full, empty ;

	pthread_mutex_t s_mut ;
	pthread_cond_t s_not_full ;
	pthread_cond_t s_not_empty ;
} meta_desc_q_t;

int meta_q_lock(meta_desc_q_t *p, int i) ;
int meta_q_unlock(meta_desc_q_t *p, int i) ;

void meta_q_init(meta_desc_q_t *q) ;
void meta_q_deinit(meta_desc_q_t *q) ;
void meta_q_enq(meta_desc_q_t *q, metadata_desc_t *x) ;
metadata_desc_t *meta_q_deq(meta_desc_q_t *q) ;
int meta_q_isempty(meta_desc_q_t *p) ;
int meta_q_isfull(meta_desc_q_t *p) ;
int meta_q_wait_for_room(meta_desc_q_t *p) ;
int meta_q_wait_for_data(meta_desc_q_t *p) ;
void meta_q_signal_notempty(meta_desc_q_t *p) ;
void meta_q_signal_notfull(meta_desc_q_t *p) ;
metadata_desc_t *meta_q_peek (meta_desc_q_t *q) ;

# endif

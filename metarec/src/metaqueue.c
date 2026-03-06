#include <stdio.h>
#include <pthread.h>
#include "metaqueue.h"

#define xprintf(a,b, c)
////fprintf(a,b,c)


int meta_q_lock(meta_desc_q_t *p, int i)
{
	int rc = -1 ;

	if ((p) && (p->initialized)) {
		rc = pthread_mutex_lock(&(p->s_mut)) ;
xprintf(stderr, "++qLock %d\n", i) ;		
	}
	return rc ;
}

int meta_q_unlock(meta_desc_q_t *p, int i)
{
	int rc = -1 ;

	if ((p) && (p->initialized)) {
		rc = pthread_mutex_unlock(&(p->s_mut)) ;
xprintf(stderr, "--qunlock %d\n", i) ;	
	}
	return rc ;
}

int meta_q_isfull(meta_desc_q_t *p)
{
	return (p != NULL) ? p->full : 0 ;
}

int meta_q_isempty(meta_desc_q_t *p)
{
	return (p != NULL) ? p->empty : 1 ;
}

int meta_q_wait_for_room(meta_desc_q_t *p)
{
	int rc = -1 ;

	if ((p) && (p->initialized)) {
		if (p->full) {
			struct timespec ts ;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 1 ;
			rc = 0;
			rc = pthread_cond_timedwait(&(p->s_not_full), &(p->s_mut), &ts);
		}
		else {
			rc = 0 ;
		}
xprintf(stderr, "??room? %d\n", 0) ;
	}
	return rc ;
}

int meta_q_wait_for_data(meta_desc_q_t *p)
{
	int rc = -1 ;
	if ((p) && (p->initialized)) {
		if (p->empty) {
			struct timespec ts ;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 1 ;
			rc = pthread_cond_timedwait(&(p->s_not_empty), &(p->s_mut), &ts);
xprintf(stderr, "??data? %d\n", 0) ;
		}
		else {
			rc = 0 ;
		}
	}
	return rc ;
}

void meta_q_init (meta_desc_q_t *q)
{
	if (q) {
		if (q->initialized) {
		    meta_q_deinit(q) ;
		}
		q->empty = 1;
		q->full = 0;
		q->head = 0;
		q->tail = 0;
		////q->mut = &q->s_mut ;
		pthread_mutex_init (&(q->s_mut), NULL);
		////q->not_full = &q->s_not_full ;
		pthread_cond_init (&(q->s_not_full), NULL);
		////q->not_empty = &q->s_not_empty ;
		pthread_cond_init (&(q->s_not_empty), NULL);
		meta_q_signal_notfull(q) ;
		q->initialized = 1 ;
	}
}

void meta_q_deinit (meta_desc_q_t *q)
{
	if ((q) && (q->initialized != 0)) {
		pthread_mutex_destroy (&(q->s_mut));
		pthread_cond_destroy (&(q->s_not_full));
		pthread_cond_destroy (&(q->s_not_empty));
		q->initialized = 0 ;
	}
}

void meta_q_enq (meta_desc_q_t *q, metadata_desc_t *in)
{
	if (q) {
		if (q->full == 0) {
			q->buf[q->tail] = *in;
			q->tail++;
			if (q->tail == METADATA_DESC_QUEUE_SIZE)
				q->tail = 0;
			if (q->tail == q->head)
				q->full = 1;
			q->empty = 0;
			pthread_cond_signal(&(q->s_not_empty));
		}
	}
	return;
}

metadata_desc_t *meta_q_deq (meta_desc_q_t *q)
{
	metadata_desc_t * out = NULL ;

	if (q) {		
		out = (q->empty) ? NULL : &q->buf[q->head];

		if (out) {
			q->head++;
			if (q->head == METADATA_DESC_QUEUE_SIZE)
				q->head = 0;			
			if (q->head == q->tail)
				q->empty = 1;							
			if (q->full) {
				q->full = 0;					
				pthread_cond_signal(&(q->s_not_full));
			}
		}
	}

	return out ;
}

metadata_desc_t *meta_q_peek (meta_desc_q_t *q)
{
	metadata_desc_t * out = 0 ;

	if (!(q->empty))
		out = &q->buf[q->head];

	return out ;
}

void meta_q_signal_notempty(meta_desc_q_t *p)
{
	pthread_cond_signal(&(p->s_not_empty));
}

void meta_q_signal_notfull(meta_desc_q_t *p)
{
	pthread_cond_signal(&(p->s_not_full));
}


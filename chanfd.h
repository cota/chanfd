/* Licensed under Simplified BSD - see LICENSE file for details */
#ifndef CHANFD_H
#define CHANFD_H

#include <stdbool.h>
#include <stddef.h>

struct chanfd *chanfd_create(size_t size, size_t n_elems);
void chanfd_destroy(struct chanfd *);

void chanfd_recv(struct chanfd *chan, void *data);
void chanfd_send(struct chanfd *chan, const void *data);

int chanfd_receiver_fd(struct chanfd *chan);
int chanfd_sender_fd(struct chanfd *chan);

bool chanfd_is_empty(struct chanfd *chan);

/* below, some type-safe send/recv helpers */

static inline void chanfd_send_int(struct chanfd *channel, const int *elem)
{
	chanfd_send(channel, elem);
}

static inline void chanfd_recv_int(struct chanfd *channel, int *elem)
{
	chanfd_recv(channel, elem);
}

static inline void chanfd_send_uint(struct chanfd *channel, const unsigned int *elem)
{
	chanfd_send(channel, elem);
}

static inline void chanfd_recv_uint(struct chanfd *channel, unsigned int *elem)
{
	chanfd_recv(channel, elem);
}

/* use these to define your own */
#define CHANFD_INLINE_SEND_STRUCT(_func, _elem, _struct_name)		\
	static inline void _func(struct chanfd *channel, const struct _struct_name *_elem) \
	{								\
		chanfd_send(channel, _elem);				\
	}

#define CHANFD_INLINE_RECV_STRUCT(_func, _elem, _struct_name)		\
	static inline void _func(struct chanfd *channel, struct _struct_name *_elem) \
	{								\
		chanfd_recv(channel, _elem);				\
	}

#endif /* CHANFD_H */

/* Licensed under Simplified BSD - see LICENSE file for details */
#include <sys/eventfd.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <errno.h>

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <ck_spinlock.h>

#include "chanfd.h"

struct unbf_channel {
	int ack_fd;
};

struct buff_channel {
	size_t in;
	size_t out;
	ck_spinlock_t lock;
};

struct chanfd {
	uint64_t elems;
	uintptr_t datap; /* tagged pointer with 'buffered' bool */
	size_t size;
	size_t max_elems;
	union {
		struct unbf_channel unbf;
		struct buff_channel buff;
	} chan;
	int sender_fd;
	int receiver_fd;
} CK_CC_ALIGN(sizeof(uintptr_t));

static inline uint8_t *to_ptr(uintptr_t val)
{
	return (uint8_t *)(val & ~1);
}

static inline bool is_buffered(struct chanfd *ch)
{
	return ch->datap & 1;
}

static inline void up(int fd)
{
	ssize_t n;
	uint64_t v = 1;

	n = write(fd, &v, sizeof(v));
	assert(n == sizeof(v));
}

static inline void down(int fd)
{
	ssize_t n;
	uint64_t v;

	n = read(fd, &v, sizeof(v));
	assert(n == sizeof(v));
}

static inline int buff_channel_init(struct chanfd *ch)
{
	struct buff_channel *bchan = &ch->chan.buff;

	ck_spinlock_init(&bchan->lock);
	return 0;
}

static inline int unbf_channel_init(struct chanfd *ch)
{
	struct unbf_channel *uchan = &ch->chan.unbf;
	int flags = EFD_CLOEXEC | EFD_SEMAPHORE;

	uchan->ack_fd = eventfd(0, flags);
	if (CK_CC_UNLIKELY(uchan->ack_fd < 0))
		return -1;
	return 0;
}

static inline void unbf_channel_destroy(struct chanfd *ch)
{
	struct unbf_channel *uchan = &ch->chan.unbf;

	close(uchan->ack_fd);
}

static inline void buff_channel_destroy(struct chanfd *ch)
{ }

/**
 * chanfd_create - create channel
 * @size: size of element to be exchanged through the channel
 * @n_elems: number of elements; 0 for unbuffered channel.
 *
 * Channels allow to explicitly transfer ownership of objects across
 * threads. Normally these objects are structs, so only a pointer to them
 * is passed through channels. Moreover, usually these structs are allocated
 * in the heap, since sharing stack data is harder to reason about.
 *
 * Receivers: they always block until there is data.
 *
 * Senders: in buffered channels (@n_elems > 0), they block only until their
 * data has been copied to the buffer. In unbuffered channels (@n_elems == 0),
 * they block until the receiver has received the value--i.e. "receiver
 * completes first."
 *
 * Returns a pointer to the newly created channel on success; NULL on error,
 * setting errno appropriately.
 *
 * Note: memory is allocated as MAP_SHARED so that forked processes can share
 * channels with their parents.
 *
 * See also: chanfd_destroy(), chanfd_send(), chanfd_recv()
 */
struct chanfd *chanfd_create(size_t size, size_t n_elems)
{
	struct chanfd *chan;
	size_t max_elems = n_elems ? n_elems : 1;
	size_t bytes = max_elems * size;
	int flags = EFD_CLOEXEC | EFD_SEMAPHORE;
	uint8_t *data;
	int rc;

	chan = mmap(NULL, sizeof(*chan), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (CK_CC_UNLIKELY(chan == MAP_FAILED))
		return NULL;

	data = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (CK_CC_UNLIKELY(data == MAP_FAILED))
		goto err_chan_data;

	chan->datap = (uintptr_t)data;
	if (CK_CC_UNLIKELY(chan->datap & 1)) {
		fprintf(stderr, "%s:%d invalid 1-byte alignment of chanfd_channel struct\n", __func__, __LINE__);
		goto err_alignment;
	}
	if (n_elems > 0)
		chan->datap |= 1;

	chan->receiver_fd = eventfd(0, flags);
	if (CK_CC_UNLIKELY(chan->receiver_fd < 0))
		goto err_receiver_fd;

	chan->sender_fd = eventfd(max_elems, flags);
	if (CK_CC_UNLIKELY(chan->sender_fd < 0))
		goto err_sender_fd;

	chan->size = size;
	chan->max_elems = max_elems;

	if (is_buffered(chan))
		rc = buff_channel_init(chan);
	else
		rc = unbf_channel_init(chan);
	if (CK_CC_UNLIKELY(rc))
		goto err_union;

	return chan;
 err_union:
	close(chan->sender_fd);
 err_sender_fd:
	close(chan->receiver_fd);
 err_receiver_fd:
 err_alignment:
	munmap(to_ptr(chan->datap), bytes);
 err_chan_data:
	munmap(chan, sizeof(*chan));
	return NULL;
}

/**
 * chanfd_destroy - destroy a channel
 * @chan: channel to destroy
 *
 * Frees all allocated data related to channel @chan.
 *
 * Note: calling this function while there are senders/receivers waiting
 * on the channel is a bug.
 *
 * See also: chanfd_create()
 */
void chanfd_destroy(struct chanfd *chan)
{
	if (chan == NULL)
		return;

	if (is_buffered(chan))
		buff_channel_destroy(chan);
	else
		unbf_channel_destroy(chan);

	munmap(to_ptr(chan->datap), chan->max_elems * chan->size);
	munmap(chan, sizeof(*chan));
}

/**
 * chanfd_receiver_fd - obtain a channel's file descriptor for "receive" monitoring
 * @ch: channel to monitor
 *
 * The returned file descriptor can be used by I/O multiplexing functions
 * such as select(2).
 *
 * NOTE: the file descriptor _must_ be put in the "read" fd set of the monitoring
 * call--putting it on _any_ other fd set is a bug.
 *
 * If the file descriptor can be read, chanfd_recv() can then be called on
 * the channel without blocking.
 *
 * See also: chanfd_sender_fd()
 */
int chanfd_receiver_fd(struct chanfd *ch)
{
	return ch->receiver_fd;
}

/**
 * chanfd_sender_fd - obtain a channel's file descriptor for "send" monitoring
 * @ch: channel to monitor
 *
 * The returned file descriptor can be used by I/O multiplexing functions
 * such as select(2).
 *
 * NOTE: As is the case for chanfd_receiver_fd(), the file descriptor _must_ be put
 * in the "read" fd set of the monitoring call--putting it on _any_ other
 * fd set is a bug.
 *
 * If the file descriptor can be read, chanfd_send() can then be called on
 * the channel without blocking.
 *
 * See also: chanfd_receiver_fd()
 */
int chanfd_sender_fd(struct chanfd *ch)
{
	return ch->sender_fd;
}

static void unbf_channel_recv(struct chanfd *ch, void *data)
{
	struct unbf_channel *uchan = &ch->chan.unbf;

	down(ch->receiver_fd);
	memcpy(data, to_ptr(ch->datap), ch->size);
	up(uchan->ack_fd);
	up(ch->sender_fd);

	ck_pr_dec_64(&ch->elems);
	ck_pr_fence_store();
}

static inline void __inc(size_t *val, size_t max_elems)
{
	*val += 1;
	if (*val == max_elems)
		*val = 0;
}

static inline void buff_channel_lock(struct chanfd *ch)
{
	struct buff_channel *bchan = &ch->chan.buff;

	if (ch->max_elems != 1)
		ck_spinlock_lock(&bchan->lock);
}

static inline void buff_channel_unlock(struct chanfd *ch)
{
	struct buff_channel *bchan = &ch->chan.buff;

	if (ch->max_elems != 1)
		ck_spinlock_unlock(&bchan->lock);
}

static void buff_channel_recv(struct chanfd *ch, void *data)
{
	struct buff_channel *bchan = &ch->chan.buff;

	down(ch->receiver_fd);

	buff_channel_lock(ch);
	memcpy(data, to_ptr(ch->datap) + bchan->out * ch->size, ch->size);
	__inc(&bchan->out, ch->max_elems);
	buff_channel_unlock(ch);

	up(ch->sender_fd);

	ck_pr_dec_64(&ch->elems);
	ck_pr_fence_store();
}

/**
 * chanfd_recv - receive data from channel
 * @ch: channel to receive from
 * @data: pointer to where the data should be copied to
 *
 * Blocks until there is data in the channel. The amount of bytes copied
 * to @data is determined upon channel creation with chanfd_create().
 *
 * See also: chanfd_receiver_fd()
 */
void chanfd_recv(struct chanfd *ch, void *data)
{
	if (is_buffered(ch))
		buff_channel_recv(ch, data);
	else
		unbf_channel_recv(ch, data);
}

static void unbf_channel_send(struct chanfd *ch, const void *data)
{
	struct unbf_channel *uchan = &ch->chan.unbf;

	down(ch->sender_fd);
	memcpy(to_ptr(ch->datap), data, ch->size);
	up(ch->receiver_fd);
	down(uchan->ack_fd);

	ck_pr_inc_64(&ch->elems);
	ck_pr_fence_store();
}

static void buff_channel_send(struct chanfd *ch, const void *data)
{
	struct buff_channel *bchan = &ch->chan.buff;

	down(ch->sender_fd);

	buff_channel_lock(ch);
	memcpy(to_ptr(ch->datap) + bchan->in * ch->size, data, ch->size);
	__inc(&bchan->in, ch->max_elems);
	buff_channel_unlock(ch);

	up(ch->receiver_fd);

	ck_pr_inc_64(&ch->elems);
	ck_pr_fence_store();
}

/**
 * chanfd_is_empty - check whether a channel is empty
 * @chan: channel to be checked
 *
 * Performs a fast, lockless peek at a channel to see whether any data has
 * been sent through it.
 *
 * An alternative is to perform a select()/poll()/epoll() call on the
 * channel's file descriptor. This is more powerful (e.g. timeout, ability
 * to wait on several channels) but is significantly slower (syscall + locks
 * within the kernel).
 *
 * Returns true when no data has been sent through the channel, false otherwise.
 */
bool chanfd_is_empty(struct chanfd *chan)
{
	ck_pr_fence_load();
	return !ck_pr_load_64(&chan->elems);
}

/**
 * chanfd_send - send data to channel
 * @ch: channel to send to
 * @data: pointer to where the data should be copied from
 *
 * This function behaves differently depending on whether @ch is buffered
 * or not; this is determined upon channel creation with chanfd_create().
 *
 * When sending on a buffered channel, the sender blocks only until its data
 * can be copied to the buffer. When sending on an unbuffered channel, the
 * sender will block until its data has been received with chanfd_recv().
 *
 * Returns 0 on success; -1 on error, setting errno appropriately.
 *
 * See also: chanfd_sender_fd()
 */
void chanfd_send(struct chanfd *ch, const void *data)
{
	if (is_buffered(ch))
		buff_channel_send(ch, data);
	else
		unbf_channel_send(ch, data);
}

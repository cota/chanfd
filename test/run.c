#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "compat.h"

#define N	16
#define LIMIT	20000

#define M	5

static __chan *test1_rsp;
static __chan *test2_chan;
static __chan *buff_rsp;
static int count;

static inline void __chan_send_int(__chan *chan, int v)
{
	__chan_send(chan, &v);
}

static void receiver_func(void *arg)
{
	__chan *chan = arg;
	__chan *send_chan;
	int v;

	/*
	 * Only one thread sends at a time, so we should be able to access
	 * count safely without races.
	 */
	for (;;) {
		__chan_recv(chan, &v);

		v++;
		count++;

		send_chan = v == LIMIT ? test1_rsp : chan;
		__chan_send(send_chan, &v);

		if (send_chan == test1_rsp)
			return;
	}
}

/*
 * Test contention on the receiver end of a blocking channel.
 */
static bool test1(void)
{
	__chan *chan;
	int i;
	int response;

	chan = __chan_create(sizeof(int), 0);
	if (chan == NULL) {
		perror(NULL);
		return false;
	}

	test1_rsp = __chan_create(sizeof(int), 0);
	if (chan == NULL) {
		perror(NULL);
		return false;
	}

	for (i = 0; i < N; i++) {
		if (__thread_create(receiver_func, chan, 64 * 1024)) {
			perror(NULL);
			return false;
		}
	}

	__chan_send_int(chan, 0);
	__chan_recv(test1_rsp, &response);

	return response == count && count == LIMIT;
}

static void test2_send_func(void *arg)
{
	int i;

	for (i = 0; i <= LIMIT; i++) {
		__chan_send(test2_chan, &i);
	}
}

static void test2_recv_func(void *arg)
{
	__chan *rsp = arg;
	int v = 0;

	while (v != LIMIT) {
		__chan_recv(test2_chan, &v);
	}
	__chan_send(rsp, &v);
}

/*
 * Test heavy contention on blocking send/receive. All N/N threads are trying
 * to send/receive at the same time.
 */
static bool test2(void)
{
	__chan **responses;
	int i;

	test2_chan = __chan_create(sizeof(int), 0);
	if (test2_chan == NULL) {
		perror(NULL);
		return false;
	}

	responses = calloc(N, sizeof(__chan *));
	if (responses == NULL) {
		perror(NULL);
		return false;
	}
	for (i = 0; i < N; i++) {
		responses[i] = __chan_create(sizeof(int), 0);
		if (responses[i] == NULL) {
			perror(NULL);
			return false;
		}
	}

	for (i = 0; i < N; i++) {
		if (__thread_create(test2_send_func, NULL, 64 * 1024)) {
			perror(NULL);
			return false;
		}

		if (__thread_create(test2_recv_func, responses[i], 64 * 1024)) {
				perror(NULL);
				return false;
		}
	}

	for (i = 0; i < N; i++) {
		int v;

		__chan_recv(responses[i], &v);
		if (v != LIMIT)
			return false;
	}
	return true;
}

static void buff_send(void *arg)
{
	__chan *chan = arg;
	int i;

	for (i = 0; i <= LIMIT; i++)
		__chan_send(chan, &i);
}

static void buff_recv(void *arg)
{
	__chan *chan = arg;
	int v = 0;

	while (v < LIMIT)
		__chan_recv(chan, &v);
	__chan_send(buff_rsp, &v);
}

/*
 * Test sender/receiver contention on a buffered channel.
 */
static bool test_buff(void)
{
	__chan *chan;
	int total = 0;
	int rc;
	int v;
	int i;

	chan = __chan_create(sizeof(int), M);
	if (chan == NULL)
		return false;

	buff_rsp = __chan_create(sizeof(int), 0);
	if (chan == NULL)
		return false;

	for (i = 0; i < M; i++) {
		rc = __thread_create(buff_send, chan, 64 * 1024);
		if (rc)
			return false;
		rc = __thread_create(buff_recv, chan, 64 * 1024);
		if (rc)
			return false;
	}
	for (i = 0; i < M; i++) {
		__chan_recv(buff_rsp, &v);
		total += v;
	}

	return total == M * LIMIT;
}

#ifdef __PLAN9__
void threadmain(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif

{
	assert(test1());
	assert(test2());
	assert(test_buff());

#ifndef __PLAN9__
	return 0;
#endif
}

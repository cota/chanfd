#ifndef COMPAT_H
#define COMPAT_H

#ifdef __PLAN9__

#include <u.h>
#include <libc.h>
#include <thread.h>

typedef Channel __chan;

static inline void __chan_send(__chan *chan, void *v)
{
	send(chan, v);
}

static inline void __chan_recv(__chan *chan, void *v)
{
	recv(chan, v);
}

static inline __chan *__chan_create(size_t size, size_t n_elems)
{
	return chancreate(size, n_elems);
}

static inline int __thread_create(void (*func)(void *), void *arg, size_t stack_size)
{
	proccreate(func, arg, stack_size);
	return 0;
}

#else /* chanfd */

#include <pthread.h>
#include <errno.h>

#include <chanfd.h>

typedef struct chanfd __chan;

struct th_args {
	void (*func)(void *);
	void *arg;
};

static inline void __chan_send(__chan *chan, void *v)
{
	chanfd_send(chan, v);
}

static inline void __chan_recv(__chan *chan, void *v)
{
	chanfd_recv(chan, v);
}

static inline __chan *__chan_create(size_t size, size_t n_elems)
{
	return chanfd_create(size, n_elems);
}

static void *th_start(void *args)
{
	struct th_args *argp = args;
	void *arg = argp->arg;
	void (*func)(void *) = argp->func;

	free(argp);
	func(arg);
	return NULL;
}

static inline int __thread_create(void (*func)(void *), void *arg, size_t stack_size)
{
	struct th_args *argp;
	pthread_attr_t attr;
	pthread_t thread;
	int rc;

	rc = pthread_attr_init(&attr);
	if (rc) {
		errno = rc;
		return -1;
	}

	rc = pthread_attr_setstacksize(&attr, stack_size);
	if (rc)
		goto err;

	rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (rc)
		goto err;

	argp = malloc(sizeof(*argp));
	if (argp == NULL) {
		rc = errno;
		goto err;
	}

	argp->func = func;
	argp->arg = arg;

	rc = pthread_create(&thread, &attr, th_start, argp);
	if (rc)
		goto err_create;

	pthread_attr_destroy(&attr);
	return 0;
 err_create:
	free(argp);
 err:
	pthread_attr_destroy(&attr);
	errno = rc;
	return -1;
}

#endif /* __PLAN9__ */

#endif /* COMPAT_H */

# chanfd

`chanfd` is a tiny C library that implements channels for inter-thread/process
communication.  These channels are similar to the channels in Plan9's
[libthread](http://plan9.bell-labs.com/magic/man2html/2/thread) or
[Go](https://golang.org/). `chanfd`, however, does not support co-routines.

### _I see. So is this yet another "Go Concurrency in C" project?_
Not quite. `chanfd` has a much narrower goal: it doesn't provide co-routines
(it works with regular threads/processes), does not take over your `main()`,
knows nothing about networking, and does not do any macro trickery to make
C look like something else.

`chanfd` is basically a simple wrapper around Linux' `eventfd(2)`, which
means you can use standard polling calls such as `select(2)` or `epoll(2)`
to wait on channels just like on any other file descriptors.

## API
```C
struct chanfd *chanfd_create(size_t size, size_t n_elems); /* pass size=0 for unbuffered channel */
void chanfd_destroy(struct chanfd *); /* Note: no 'close' operation */

void chanfd_recv(struct chanfd *chan, void *data); /* see the header file for type-safe macros */
void chanfd_send(struct chanfd *chan, const void *data);

int chanfd_receiver_fd(struct chanfd *chan); /* these can be used with select(2) etc. */
int chanfd_sender_fd(struct chanfd *chan);

bool chanfd_is_empty(struct chanfd *chan);
```
`chanfd_send` and `chanfd_recv` are expensive; they issue syscalls that take at
least one lock.  However, `chanfd_is_empty` is fast and scalable since it only
needs a SMP load fence and a load.

## Dependencies
* Linux >= v2.6.30
* [Concurrency Kit.](http://concurrencykit.org/) Run `./configure --help` for
  options regarding Concurrency Kit's installation.

## Historical Background on Channels
[Bell Labs and CSP Threads](https://swtch.com/~rsc/thread/) by Russ Cox.

## Some Alternatives
* [chan](https://github.com/tylertreat/chan)
* [eb_chan](https://github.com/davekeck/eb_chan)
* [libmill](http://libmill.org/)
* [libtask](https://swtch.com/libtask/)
* [Plan9's libthread](http://plan9.bell-labs.com/magic/man2html/2/thread)
* or just use the [Go Programming Language](https://golang.org/)

## License
Simplified BSD License -- see LICENSE.

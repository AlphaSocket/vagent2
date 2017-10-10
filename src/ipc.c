/*
 * Copyright (c) 2012-2013 Varnish Software AS
 * All rights reserved.
 *
 * Author: Kristian Lyngstøl <kristian@bohemians.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * General IPC mechanisms for use between various plugins.
 *
 * Usage:
 * Step 1: Provider has a single ipc_t *handle structure. This structure
 *         must be readable from the consumers/users. Issue ipc_init.
 * Step 2: While plugins/modules load, they issue ipc_register(handle) and
 *         store the returned value.
 * Step 3: (or 2... whichever). Provider sets handle->cb to a command
 *         handler and handle->priv respectively.
 * Step 4: Provider issues ipc_start, this returns a thread structure and
 *         the provider is open for business.
 * Step 5: A consumer/user issues ipc_run(sock, command, return), where the
 *         sock is what ipc_register() returned earlier, the command is the
 *         message to send and the return is a ipc_ret_t structure that
 *         will be used to return the status.
 * Step 6: The provider sees the message and the callback is run with the
 *         private data, command and a /different/ ipc_ret_t structure.
 *
 */


#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vcli.h>

#include "common.h"
#include "ipc.h"
#include "plugins.h"

/*
 * This is a safety net.
 *
 * IPCs are per-thread. If you have multiple threads, you need to get
 * multiple handles.
 *
 * tid_to_fd tracks what threads have used what handles (for now), and
 * asserts if you do something bad. It's slightly flawed, in that you need
 * multiple handles if you want to output some logger-stuff before you spin
 * up your thread (see modules/http.c for an example of this).
 */
static pthread_t tid_to_fd[1024];

static void
ipc_verify_sock_thread(int sock)
{

	if (sock < 1024) {
		if (tid_to_fd[sock] == 0)
			tid_to_fd[sock] = pthread_self();
		assert(tid_to_fd[sock] == pthread_self());
	}
}

/*
 * Client
 */

/*
 * Write text to a socket. Close it if we fail.
 * Returns true on success.
 */
static int
ipc_write(int sock, const char *s, int len)
{
	int l;

	ipc_verify_sock_thread(sock);
	l = write (sock, s, len);
	if (len == l)
		return (1);
	perror("Write error CLI socket");
	assert(close(sock));
	return (0);
}

/*
 * Write the command, read the result.
 * XXX: VCLI_ReadResult will allocate ret->answer. Caller MUST free it.
 */
void
ipc_send(int handle, void *data, int len, struct ipc_ret_t *ret)
{
	char buffer[12];

	assert(data);
	assert(len < 1000000000);
	assert(len >= 0);
	assert(threads_started > 0);

	snprintf(buffer,11,"%09d ",len);
	ipc_write(handle, buffer, 10);
	ipc_write(handle, data, len);

	VCLI_ReadResult(handle, &ret->status, &ret->answer, 5.0);
}

/*
 * Parse a command of arbitrary length, execute it, place result in *ret.
 */
void
ipc_run(int handle, struct ipc_ret_t *ret, const char *fmt, ...)
{
	va_list ap;
	char *buffer;
	int iret;

	assert(fmt);
	va_start(ap, fmt);
	iret = vasprintf(&buffer, fmt, ap);
	assert(iret>=0);
	va_end(ap);
	assert(buffer);
	ipc_send(handle, buffer, strlen(buffer),ret);
	free(buffer);
}

/*
 * Grab a IPC handle for a named plugin.
 * Return value is later used for ipc_run().
 * XXX: Must execute prior to plugins starting, otherwise we might use the
 * ipc structure before it's fully populated.
 */
int
ipc_register(struct agent_core_t *core, const char *name)
{
	struct agent_plugin_t *v;
	int sv[2];
	int ret;

	v  = plugin_find(core, name);

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	assert(ret == 0);
	assert(v->ipc->nlisteners < MAX_LISTENERS);
	v->ipc->listeners[v->ipc->nlisteners++] = sv[0];

	return (sv[1]);
}

/*
 * Server
 */

/*
 * A command was apparently issued.
 *
 * Commands now have 16 bytes of decimal-encoded size.
 *
 * Note that &ret must be populated with something we can free().
 */
static int
ipc_cmd(int fd, struct ipc_t *ipc)
{
	char buffer[11];
	struct ipc_ret_t ret;
	int length = 0;
	char *data;
	int i, oldi;

	i = read(fd, buffer, 10);
	assert(i == 10);
	assert(buffer[9] == ' ');
	length = atoi(buffer);
	assert(length >= 0);

	data = malloc(length+1);
	assert(data);
	for (i = 0, oldi = 0; i < length; ) {
		i += read(fd, data + i, length - i);
		assert(i != oldi);
	}

	if (i != length)
		fprintf(stderr,"Wanted %d data, got %d", length, i);
	assert(i == length);
	data[length] = '\0';
	ipc->cb(ipc->priv, data, &ret);

	VCLI_WriteResult(fd, ret.status, ret.answer);
	free(data);
	if (ret.answer)
		free(ret.answer);
	return (1);
}

/*
 * IPC main loop.
 * Just wait for data on the fds provided, then trigger ipc_cmd().
 */
static void *
ipc_loop(void *data)
{
	struct ipc_t *ipc = (struct ipc_t *)data;
	struct pollfd fds[MAX_LISTENERS+1];
	int i;
	int ret;

	for (i=0; i < ipc->nlisteners; i ++) {
		fds[i].fd = ipc->listeners[i];
		fds[i].events = POLLIN;
	}
	while (1) {
		ret = poll(fds, ipc->nlisteners, -1);
		if (ret == -1 && errno == EINTR)
			continue;
		assert(ret > 0);
		for (i=0; i < ipc->nlisteners; i++) {
			if (fds[i].revents & POLLIN) {
				ipc_cmd(fds[i].fd, ipc);
			}
		}
	}
	return (NULL);
}

/*
 * Should probably be redundant...
 */
void
ipc_init(struct ipc_t *ipc)
{

	assert(ipc != NULL);
	ipc->nlisteners = 0;
}

/*
 * Does the actual threading and returns the thread.
 */
void *
ipc_start(struct agent_core_t *core, const char *name)
{
	struct agent_plugin_t *plug;
	pthread_t *thread;

	ALLOC_OBJ(thread);
	plug = plugin_find(core, name);
	AZ(pthread_create(thread, NULL, (ipc_loop), plug->ipc));
	plug->thread = thread;
	return (thread);
}

/*
 * Sanity.
 */

void
ipc_sanity(struct agent_core_t *core)
{
	struct agent_plugin_t *plug;

	for (plug = core->plugins; plug != NULL; plug = plug->next) {
		if (plug->ipc->cb && !plug->start) {
			fprintf(stderr,
			    "Plugin %s defines a callback for the IPC,"
			    " but does not have a start function.\n"
			    "Consider setting plug->start to ipc_start in "
			    "the init-function of the plugin.\n",
			    plug->name);
			assert((plug->ipc->cb == NULL) || (plug->start != NULL));
		}
	}
}

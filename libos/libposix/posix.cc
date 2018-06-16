// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * libos/posix/posix-queue.cc
 *   POSIX implementation of Zeus queue interface
 *
 * Copyright 2018 Irene Zhang  <irene.zhang@microsoft.com>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#include "posix-queue.h"
#include "common/library.h"
#include "include/io-queue.h"

using namespace

// ================================================
// Generic interfaces to libOS syscalls
// ================================================

// create network queue
int queue(int domain, int type, int protocol)
{
    int qd = QueueType.queue(domain, type, protocol);
    if (qd > 0)
        InsertQueue(QueueType(NETWORK_Q, qd));
    return qd;
}

int bind(int qd, struct sockaddr *saddr, socklen_t size)
{
    return queues[qd].bind(saddr, size);
}

int
accept(int qd, struct sockaddr *saddr, socklen_t *size)
{
    return queues[qd].accept(saddr, size);
}

int
listen(int qd, int backlog)
{
    return queues[qd].listen(backlog);
}
        

int
connect(int qd, struct sockaddr *saddr, socklen_t size)
{
    return queues.[qd].connect(saddr, size);
}

int
open(const char *pathname, int flags)
{
    // use the fd as qd
    int qd = QueueType.open(pathname, flags);
    if (qd > 0)
        InsertQueue(PosixQueue(FILE_Q, qd));
    return qd;
}

int
open(const char *pathname, int flags, mode_t mode)
{
    // use the fd as qd
    int qd = QueueType.open(pathname, flags, mode);
    if (qd > 0)
        InsertQueue(PosixQueue(FILE_Q, qd));
    return qd;
}

int
creat(const char *pathname, mode_t mode)
{
    // use the fd as qd
    int qd = QueueType.creat(pathname, mode);
    if (qd > 0)
        InsertQueue(PosixQueue(FILE_Q, qd));
    return qd;
}
    
int
close(int qd)
{
    if (!HasQueue(qd))
        return -1;

    QueueType &queue = GetQueue(qd);
    int res = queue.close();
    RemoveQueue(qd);    
    return res;
}

int
qd2fd(int qd) {
    if (!HasQueue(qd))
        return -1;
    return queues[qd].fd();
}
    
// TODO: Actually buffer somewhere
qtoken
push(int qd, struct Zeus::sgarray &sga)
{
    if (!HasQueue(qd))
        return -1;

    QueueType &queue = GetQueue(qd);
    if (queue.type == FILE_Q)
        // pushing to files not implemented yet
        return 0;
   
    queue.Push(sga);
    return GetNewToken(qd);
}

qtoken
pop(int qd, struct Zeus::sgarray &sga)
{
 
    if (!HasQueue(qd))
        return -1;
    
    QueueType &queue = GetQueue(qd);
    if (queue.type == FILE_Q)
        // pushing to files not implemented yet
        return 0;

    queue.Receive(sga);
    return GetNewToken(qd);
}

ssize_t
wait_any(qtoken qts[], size_t num_qts)
{
    
}

ssize_t
wait_all(qtoken qts[], size_t num_qts)
{
    
}

  
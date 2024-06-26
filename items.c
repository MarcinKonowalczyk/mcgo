// +build ignore

#include <Judy.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "memcached.h"

/*
 * NOTE: we assume here for simplicity that slab ids are <=32. That's true in
 * the powers-of-2 implementation, but if that changes this should be changed
 * too
 */

#define LARGEST_ID 32
static item *heads[LARGEST_ID];
static item *tails[LARGEST_ID];
unsigned int sizes[LARGEST_ID];

void
item_init(void)
{
    for (int i = 0; i < LARGEST_ID; i++) {
        heads[i] = 0;
        tails[i] = 0;
        sizes[i] = 0;
    }
}

item *
item_alloc(char *key, int flags, time_t exptime, int nbytes)
{
    int len = strlen(key) + 1;
    if (len % 4) {
        len += 4 - (len % 4);
    }
    int ntotal = sizeof(item) + len + nbytes;

    unsigned int id = slabs_clsid(ntotal);
    if (id == 0) {
        return 0;
    }

    item *it = slabs_alloc(id);
    if (it == 0) {
        /*
         * try to get one off the right LRU
         * don't necessariuly unlink the tail because it may be locked: refcount>0
         * search up from tail an item with refcount==0 and unlink it; give up after
         * 50 tries
         */

        int tries = 50;
        item *search;

        if (id > LARGEST_ID)
            return 0;
        if (tails[id] == 0)
            return 0;

        for (search = tails[id]; tries > 0 && search; tries--, search = search->prev) {
            if (search->refcount == 0) {
                item_unlink(search);
                break;
            }
        }
        it = slabs_alloc(id);
        if (it == 0) {
            return 0;
        }
    }

    it->slabs_clsid = id;

    it->next = it->prev = 0;
    it->refcount = 0;
    it->it_flags = 0;
    it->key = (char *)&(it->end[0]);
    it->data = (void *)(it->key + len);
    strcpy(it->key, key);
    it->exptime = exptime;
    it->nbytes = nbytes;
    it->ntotal = ntotal;
    it->flags = flags;
    return it;
}

void
item_free(item *it)
{
    slabs_free(it, it->slabs_clsid);
}

void
item_link_q(item *it)
{ /* item is the new head */
    if (it->slabs_clsid > LARGEST_ID) {
        return;
    }
    item **head = &heads[it->slabs_clsid];
    item **tail = &tails[it->slabs_clsid];
    it->prev = 0;
    it->next = *head;
    if (it->next) {
        it->next->prev = it;
    }
    *head = it;
    if (*tail == 0) {
        *tail = it;
    }
    sizes[it->slabs_clsid]++;
    return;
}

void
item_unlink_q(item *it)
{
    if (it->slabs_clsid > LARGEST_ID) {
        return;
    }
    item **head = &heads[it->slabs_clsid];
    item **tail = &tails[it->slabs_clsid];
    if (*head == it) {
        *head = it->next;
    }
    if (*tail == it) {
        *tail = it->prev;
    }
    if (it->next) {
        it->next->prev = it->prev;
    }
    if (it->prev) {
        it->prev->next = it->next;
    }
    sizes[it->slabs_clsid]--;
    return;
}

int
item_link(item *it)
{
    it->it_flags |= ITEM_LINKED;
    it->time = time(0);
    judy_insert(it->key, (void *)it);

    stats.curr_bytes += it->ntotal;
    stats.curr_items += 1;
    stats.total_items += 1;

    item_link_q(it);

    return 1;
}

void
item_unlink(item *it)
{
    it->it_flags &= ~ITEM_LINKED;
    judy_delete(it->key);
    item_unlink_q(it);
    stats.curr_bytes -= it->ntotal;
    stats.curr_items -= 1;
    if (it->refcount == 0) {
        item_free(it);
    }
    return;
}

void
item_remove(item *it)
{
    if (it->refcount) {
        it->refcount--;
    }
    if (it->refcount == 0 && (it->it_flags & ITEM_LINKED) == 0) {
        item_free(it);
    }
}

void
item_update(item *it)
{
    item_unlink_q(it);
    it->time = time(0);
    item_link_q(it);
}

int
item_replace(item *it, item *new_it)
{
    item_unlink(it);
    return item_link(new_it);
}

char *
item_cachedump(unsigned int slabs_clsid, unsigned int limit, unsigned int *bytes)
{
    int memlimit = 2 * 1024 * 1024;
    int bufcurr;
    item *it;
    int len;
    unsigned int shown = 0;
    char temp[256];

    if (slabs_clsid > LARGEST_ID) {
        return 0;
    }
    it = heads[slabs_clsid];

    char *buffer = malloc(memlimit);
    if (buffer == 0) {
        return 0;
    }
    bufcurr = 0;

    while (1) {
        if (limit && shown >= limit) {
            break;
        }
        if (!it) {
            break;
        }
        sprintf(temp, "ITEM %s [%u b; %ld s]\r\n", it->key, it->nbytes - 2, it->time);
        len = strlen(temp);
        if (bufcurr + len + 5 > memlimit) {
            /* 5 is END\r\n */
            break;
        }
        strcpy(buffer + bufcurr, temp);
        bufcurr += len;
        shown++;
        it = it->next;
    }

    strcpy(buffer + bufcurr, "END\r\n");
    bufcurr += 5;

    *bytes = bufcurr;
    return buffer;
}

void
item_stats(char *buffer, int buflen)
{
    char *bufcurr = buffer;
    time_t now = time(0);

    if (buflen < 4096) {
        strcpy(buffer, "SERVER_ERROR out of memory");
        return;
    }

    for (int i = 0; i < LARGEST_ID; i++) {
        if (tails[i]) {
            bufcurr += sprintf(bufcurr, "STAT items:%u:number %u\r\nSTAT items:%u:age %ld\r\n",
                               i, sizes[i], i, now - tails[i]->time);
        }
    }
    strcpy(bufcurr, "END");
    return;
}

/* dumps out a list of objects of each size, with granularity of 32 bytes */
char *
item_stats_sizes(int *bytes)
{
    int num_buckets = 32768; /* max 1MB object, divided into 32 bytes size buckets */
    unsigned int *histogram = (unsigned int *)malloc(num_buckets * sizeof(int));
    char *buf = (char *)malloc(1024 * 1024 * 2 * sizeof(char));

    if (histogram == 0 || buf == 0) {
        if (histogram) {
            free(histogram);
        }
        if (buf) {
            free(buf);
        }
        return 0;
    }

    /* build the histogram */
    memset(buf, 0, num_buckets * sizeof(int));
    for (int i = 0; i < LARGEST_ID; i++) {
        item *iter = heads[i];
        while (iter) {
            int bucket = iter->ntotal / 32;
            if (iter->ntotal % 32) {
                bucket++;
            }
            if (bucket < num_buckets) {
                histogram[bucket]++;
            }
            iter = iter->next;
        }
    }

    /* write the buffer */
    *bytes = 0;
    for (int i = 0; i < num_buckets; i++) {
        if (histogram[i]) {
            *bytes += sprintf(&buf[*bytes], "%u %u\r\n", i * 32, histogram[i]);
        }
    }
    *bytes += sprintf(&buf[*bytes], "END\r\n");

    free(histogram);
    return buf;
}

// +build ignore

/*
 *  memcached - memory caching daemon
 *
 *       http://www.danga.com/memcached/
 *
 *  Copyright 2003 Danga Interactive, Inc.  All rights reserved.
 *
 *  Use and distribution licensed under the GNU General Public License (GPL)
 *
 *  Authors:
 *      Anatoly Vorobey <mellon@pobox.com>
 *      Brad Fitzpatrick <brad@danga.com>
 *
 *  $Id$
 */

#include <Judy.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "memcached.h"

#ifdef __APPLE__
#include <spawn.h>
#endif

static item **todelete = 0;
static int delcurr = 0;
static int deltotal = 200;

//====================================
//
//      ##  ##   ##  #####  ##    ##
//      ##  ##   ##  ##  ##  ##  ##
//      ##  ##   ##  ##  ##   ####
//  ##  ##  ##   ##  ##  ##    ##
//   ####    #####   #####     ##
//
//====================================
// associative array, using Judy
// https://judy.sourceforge.net/doc/JudySL_3x.htm

static Pvoid_t PJSLArray = (Pvoid_t)NULL;

void
judy_init(void)
{
    return;
}

void *
judy_find(char *key)
{
    Word_t *PValue;
    JSLG(PValue, PJSLArray, (const uint8_t *)key);
    if (PValue) {
        return ((void *)*PValue);
    }
    else {
        return 0;
    }
}

int
judy_insert(char *key, void *value)
{
    Word_t *PValue;
    JSLI(PValue, PJSLArray, (const uint8_t *)key);
    if (PValue) {
        *PValue = (Word_t)value;
        return 1;
    }
    else {
        return 0;
    }
}

void
judy_delete(char *key)
{
    int Rc_int;
    JSLD(Rc_int, PJSLArray, (const uint8_t *)key);
    return;
}

//====================================
//
//   ####  ######  ###  ######  ####
//  ##       ##   ## ##   ##   ##
//   ###     ##  ##   ##  ##    ###
//     ##    ##  #######  ##      ##
//  ####     ##  ##   ##  ##   ####
//
//====================================

struct stats stats;

void
stats_init(void)
{
    stats.curr_items = stats.total_items = stats.curr_conns = stats.total_conns =
        stats.conn_structs = 0;
    stats.get_cmds = stats.set_cmds = stats.get_hits = stats.get_misses = 0;
    stats.curr_bytes = stats.bytes_read = stats.bytes_written = 0;
    stats.started = time(0);
}

void
stats_reset(void)
{
    stats.total_items = stats.total_conns = 0;
    stats.get_cmds = stats.set_cmds = stats.get_hits = stats.get_misses = 0;
    stats.bytes_read = stats.bytes_written = 0;
}

//===============================================================
//
//   ####  ######  ######  ######  ####  ##   ##   #####   ####
//  ##     ##        ##      ##     ##   ###  ##  ##      ##
//   ###   #####     ##      ##     ##   #### ##  ##  ###  ###
//     ##  ##        ##      ##     ##   ## ####  ##   ##    ##
//  ####   ######    ##      ##    ####  ##  ###   #####  ####
//
//===============================================================

struct settings settings;

void
settings_init(void)
{
    settings.port = 11211;
    settings.interface.s_addr = htonl(INADDR_ANY);
    settings.maxbytes = 64 * 1024 * 1024; /* default is 64MB */
    settings.maxitems = 0;                /* no limit on no. of items by default */
    settings.maxconns = 1024; /* to limit connections-related memory to about 5MB */
    settings.verbose = 0;
}

//================================================================================
//
//   #####  ####   ##   ##  ##   ##  ######  #####  ######  ####  ####   ##   ##
//  ##     ##  ##  ###  ##  ###  ##  ##     ##        ##     ##  ##  ##  ###  ##
//  ##     ##  ##  #### ##  #### ##  #####  ##        ##     ##  ##  ##  #### ##
//  ##     ##  ##  ## ####  ## ####  ##     ##        ##     ##  ##  ##  ## ####
//   #####  ####   ##  ###  ##  ###  ######  #####    ##    ####  ####   ##  ###
//
//================================================================================

void
conn_init(void)
{
    return;
}

conn *
conn_new(int sfd, int init_state, int event_flags)
{
    conn *c = (conn *)malloc(sizeof(conn));
    if (c == 0) {
        perror("malloc()");
        return 0;
    }

    c->rbuf = c->wbuf = 0;
    c->ilist = 0;

    c->rbuf = (char *)malloc(DATA_BUFFER_SIZE);
    c->wbuf = (char *)malloc(DATA_BUFFER_SIZE);
    c->ilist = (item **)malloc(sizeof(item *) * 200);

    if (c->rbuf == 0 || c->wbuf == 0 || c->ilist == 0) {
        if (c->rbuf != 0)
            free(c->rbuf);
        if (c->wbuf != 0)
            free(c->wbuf);
        if (c->ilist != 0)
            free(c->ilist);
        free(c);
        perror("malloc()");
        return 0;
    }
    c->rsize = c->wsize = DATA_BUFFER_SIZE;
    c->isize = 200;
    stats.conn_structs++;

    c->sfd = sfd;
    c->state = init_state;
    c->rlbytes = 0;
    c->rbytes = c->wbytes = 0;
    c->wcurr = c->wbuf;
    c->rcurr = c->rbuf;
    c->icurr = c->ilist;
    c->ileft = 0;
    c->iptr = c->ibuf;
    c->ibytes = 0;

    c->write_and_go = conn_read;
    c->write_and_free = 0;
    c->item = 0;

    event_set(&c->event, sfd, event_flags, event_handler, (void *)c);
    c->ev_flags = event_flags;

    if (event_add(&c->event, 0) == -1) {
        free(c);
        return 0;
    }

    stats.curr_conns++;
    stats.total_conns++;

    return c;
}

void
conn_close(conn *c)
{
    /* delete the event, the socket and the conn */
    event_del(&c->event);

    close(c->sfd);

    if (c->item) {
        item_free(c->item);
    }

    if (c->ileft) {
        for (; c->ileft > 0; c->ileft--, c->icurr++) {
            item_remove(*(c->icurr));
        }
    }

    if (c->write_and_free) {
        free(c->write_and_free);
    }

    free(c->rbuf);
    free(c->wbuf);
    free(c->ilist);
    free(c);

    stats.curr_conns--;

    return;
}

void
out_string(conn *c, char *str)
{
    int len = strlen(str);
    if (len + 2 > c->wsize) {
        /* ought to be always enough. just fail for simplicity */
        str = "SERVER_ERROR output line too long";
        len = strlen(str);
    }

    strcpy(c->wbuf, str);
    strcat(c->wbuf, "\r\n");
    c->wbytes = len + 2;
    c->wcurr = c->wbuf;

    c->state = conn_write;
    c->write_and_go = conn_read;
    return;
}

void
maybe_out_string(conn *c, char *str, int do_out)
{
    if (do_out) {
        out_string(c, str);
    }
    else {
        c->state = conn_read;
    }
}

char *
conn_state_to_str(enum conn_states state)
{
    switch (state) {
        case conn_listening:
            return "conn_listening";
        case conn_read:
            return "conn_read";
        case conn_nread:
            return "conn_nread";
        case conn_write:
            return "conn_write";
        case conn_closing:
            return "conn_closing";
        case conn_mwrite:
            return "conn_mwrite";
        case conn_swallow:
            return "conn_swallow";
        default:
            return "unknown";
    }
}
//=======================================================
//
//  #####   #####    ####    #####  ######   ####  ####
//  ##  ##  ##  ##  ##  ##  ##      ##      ##    ##
//  #####   #####   ##  ##  ##      #####    ###   ###
//  ##      ##  ##  ##  ##  ##      ##         ##    ##
//  ##      ##   ##  ####    #####  ######  ####  ####
//
//=======================================================

/*
 * we get here after reading the value in set/add/replace commands. The command
 * has been stored in c->item_comm, and the item is ready in c->item.
 */

void
complete_nread(conn *c)
{
    item *it = c->item;
    int comm = c->item_comm;
    bool noreply = c->item_noreply;
    item *old_it;
    time_t now = time(0);

    stats.set_cmds++;

    if (strncmp((char *)(it->data) + it->nbytes - 2, "\r\n", 2) != 0) {
        out_string(c, "CLIENT_ERROR bad data chunk");
        goto free_item;
    }

    old_it = (item *)judy_find(it->key);

    if (old_it && old_it->exptime && old_it->exptime < now) {
        // Old item expired. We can't replace it.
        item_unlink(old_it);
        old_it = NULL;
    }

    if (old_it && comm == NREAD_ADD) {
        // Old item exists, and command is "add"
        item_update(old_it);
        maybe_out_string(c, "NOT_STORED", !noreply);
        goto free_item;
    }

    if (!old_it && comm == NREAD_REPLACE) {
        // No old item, and command is "replace"
        maybe_out_string(c, "NOT_STORED", !noreply);
        goto free_item;
    }

    if (old_it && (old_it->it_flags & ITEM_DELETED) &&
        (comm == NREAD_REPLACE || comm == NREAD_ADD)) {
        // deleted item, and command is "replace" or "add"
        maybe_out_string(c, "NOT_STORED", !noreply);
        goto free_item;
    }

    if (old_it) {
        // we are replacing an existing item
        item_replace(old_it, it);
        maybe_out_string(c, "STORED", !noreply);
        goto stored_item;
    }
    else {
        // we are adding a new item to the cache
        item_link(it);
        maybe_out_string(c, "STORED", !noreply);
        goto stored_item;
    }

stored_item:
    c->item = 0;
    return;

free_item:
    item_free(it);
    c->item = 0;
    return;
}

void
process_stat(conn *c, char *command)
{
    time_t now = time(0);

    if (strcmp(command, "stats") == 0) {
        char temp[768];
        pid_t pid = getpid();
        char *pos = temp;

        pos += sprintf(pos, "STAT pid %u\r\n", pid);
        pos += sprintf(pos, "STAT uptime %ld\r\n", now - stats.started);
        pos += sprintf(pos, "STAT curr_items %u\r\n", stats.curr_items);
        pos += sprintf(pos, "STAT total_items %u\r\n", stats.total_items);
        pos += sprintf(pos, "STAT bytes %llu\r\n", stats.curr_bytes);
        pos += sprintf(pos, "STAT curr_connections %u\r\n",
                       stats.curr_conns - 1); /* ignore listening conn */
        pos += sprintf(pos, "STAT total_connections %u\r\n", stats.total_conns);
        pos += sprintf(pos, "STAT connection_structures %u\r\n", stats.conn_structs);
        pos += sprintf(pos, "STAT cmd_get %u\r\n", stats.get_cmds);
        pos += sprintf(pos, "STAT cmd_set %u\r\n", stats.set_cmds);
        pos += sprintf(pos, "STAT get_hits %u\r\n", stats.get_hits);
        pos += sprintf(pos, "STAT get_misses %u\r\n", stats.get_misses);
        pos += sprintf(pos, "STAT bytes_read %llu\r\n", stats.bytes_read);
        pos += sprintf(pos, "STAT bytes_written %llu\r\n", stats.bytes_written);
        pos += sprintf(pos, "STAT limit_maxbytes %llu\r\n", settings.maxbytes);
        pos += sprintf(pos, "STAT limit_maxitems %u\r\n", settings.maxitems);
        pos += sprintf(pos, "END");
        out_string(c, temp);
        return;
    }

    if (strcmp(command, "stats reset") == 0) {
        stats_reset();
        out_string(c, "RESET");
        return;
    }

    if (strcmp(command, "stats maps") == 0) {
        char *wbuf;
        int wsize = 8192; /* should be enough */
        int fd;
        int res;

        wbuf = (char *)malloc(wsize);
        if (wbuf == 0) {
            out_string(c, "SERVER_ERROR out of memory");
            return;
        }

        fd = open("/proc/self/maps", O_RDONLY);
        if (fd == -1) {
            out_string(c, "SERVER_ERROR cannot open the maps file");
            free(wbuf);
            return;
        }

        res = read(fd, wbuf, wsize - 6); /* 6 = END\r\n\0 */
        if (res == wsize - 6) {
            out_string(c, "SERVER_ERROR buffer overflow");
            free(wbuf);
            close(fd);
            return;
        }
        if (res == 0 || res == -1) {
            out_string(c, "SERVER_ERROR can't read the maps file");
            free(wbuf);
            close(fd);
            return;
        }
        strcpy(wbuf + res, "END\r\n");
        c->write_and_free = wbuf;
        c->wcurr = wbuf;
        c->wbytes = res + 6;
        c->state = conn_write;
        c->write_and_go = conn_read;
        close(fd);
        return;
    }

    if (strncmp(command, "stats cachedump", 15) == 0) {
        char *buf;
        unsigned int bytes, id, limit = 0;
        char *start = command + 15;
        if (sscanf(start, "%u %u\r\n", &id, &limit) < 1) {
            out_string(c, "CLIENT_ERROR bad command line");
            return;
        }

        buf = item_cachedump(id, limit, &bytes);
        if (buf == 0) {
            out_string(c, "SERVER_ERROR out of memory");
            return;
        }

        c->write_and_free = buf;
        c->wcurr = buf;
        c->wbytes = bytes;
        c->state = conn_write;
        c->write_and_go = conn_read;
        return;
    }

    if (strcmp(command, "stats slabs") == 0) {
        char buffer[4096];
        slabs_stats(buffer, 4096);
        out_string(c, buffer);
        return;
    }

    if (strcmp(command, "stats items") == 0) {
        char buffer[4096];
        item_stats(buffer, 4096);
        out_string(c, buffer);
        return;
    }

    if (strcmp(command, "stats sizes") == 0) {
        int bytes = 0;
        char *buf = item_stats_sizes(&bytes);
        if (!buf) {
            out_string(c, "SERVER_ERROR out of memory");
            return;
        }

        c->write_and_free = buf;
        c->wcurr = buf;
        c->wbytes = bytes;
        c->state = conn_write;
        c->write_and_go = conn_read;
        return;
    }

    out_string(c, "ERROR");
}

void
process_command(conn *c, char *command)
{
    int comm = 0;
    int incr = 0;

    /*
     * for commands set/add/replace, we build an item and read the data
     * directly into it, then continue in nread_complete().
     */

    // printf("command: %s\n", command);
    if ((strncmp(command, "add ", 4) == 0 && (comm = NREAD_ADD)) ||
        (strncmp(command, "set ", 4) == 0 && (comm = NREAD_SET)) ||
        (strncmp(command, "replace ", 8) == 0 && (comm = NREAD_REPLACE))) {
        char s_comm[10];
        char key[256];
        time_t expire;
        int len;
        int flags;
        char s_noreply[8];
        bool noreply;

        int res = sscanf(command, "%s %s %u %lu %d %s\n", s_comm, key, &flags, &expire, &len,
                         s_noreply);
        if (!(res == 5 || res == 6) || strlen(key) == 0) {
            out_string(c, "CLIENT_ERROR bad command line format");
            return;
        }

        noreply = (res == 6 && strcmp(s_noreply, "noreply") == 0) ? 1 : 0;

        time_t now = time(0);
        item *it = item_alloc(key, flags, now + expire, len + 2);
        if (it == 0) {
            out_string(c, "SERVER_ERROR out of memory");
            /* swallow the data line */
            c->write_and_go = conn_swallow;
            c->sbytes = len + 2;
            return;
        }

        // Set the item for the continuation
        c->item = it;
        c->item_comm = comm;
        c->item_noreply = noreply;

        c->rcurr = it->data;
        c->rlbytes = it->nbytes;
        c->state = conn_nread;
        return;
    }

    if ((strncmp(command, "incr ", 5) == 0 && (incr = 1)) ||
        (strncmp(command, "decr ", 5) == 0)) {
        char s_comm[10];
        unsigned int delta;
        char key[255];
        char *ptr;
        char s_noreply[8];
        bool noreply;

        int res = sscanf(command, "%s %s %u %s\n", s_comm, key, &delta, s_noreply);
        time_t now = time(0);

        if (!(res == 3 || res == 4) || strlen(key) == 0) {
            out_string(c, "CLIENT_ERROR bad command line format");
            return;
        }

        noreply = (res == 4 && strcmp(s_noreply, "noreply") == 0) ? 1 : 0;

        item *it = judy_find(key);
        if (it && (it->it_flags & ITEM_DELETED)) {
            it = 0;
        }
        if (it && it->exptime && it->exptime < now) {
            item_unlink(it);
            it = 0;
        }

        if (!it) {
            out_string(c, "NOT_FOUND");
            return;
        }

        ptr = it->data;

        // number format is [+-]?\d+.*\r\n
        int i = 0;
        if (*ptr == '-') {
            ptr++;
            i++;
        }
        else if (*ptr == '+') {
            ptr++;
            i++;
        }

        while (*ptr && *ptr != '\r') {
            if (*ptr < '0' || *ptr > '9') {
                out_string(c, "CLIENT_ERROR cannot increment or decrement non-numeric value");
                return;
            }
            ptr++;
            i++;
        }

        if (*ptr == '\0' || *(ptr + 1) != '\n') {
            out_string(c, "CLIENT_ERROR cannot increment or decrement non-numeric value");
            return;
        }

        // Back the pointer up to the start of the numeric string
        ptr -= i;

        int value = atoi(ptr);  // get the current value

        if (incr)
            value += delta;
        else {
            value -= delta;
        }

        char temp[32];
        sprintf(temp, "%d", value);
        res = strlen(temp);
        if (res == it->nbytes - 2) { /* replace in-place */
            memcpy(it->data, temp, res);
            memset((char *)(it->data) + res, ' ', it->nbytes - res - 2);
        }
        else { /* need to realloc */
            item *new_it;
            new_it = item_alloc(it->key, it->flags, it->exptime, res + 2);
            if (new_it == 0) {
                out_string(c, "SERVER_ERROR out of memory");
                return;
            }
            memcpy(new_it->data, temp, res);
            memcpy((char *)(new_it->data) + res, "\r\n", 2);
            item_replace(it, new_it);
        }
        maybe_out_string(c, temp, !noreply);
        return;
    }

    if (strncmp(command, "get ", 4) == 0) {
        char *start = command + 4;
        char key[256];
        int next;
        int i = 0;
        item *it;
        time_t now = time(0);

        while (sscanf(start, " %s%n", key, &next) >= 1) {
            start += next;
            stats.get_cmds++;
            it = (item *)judy_find(key);
            if (it && (it->it_flags & ITEM_DELETED)) {
                it = 0;
            }
            if (it && it->exptime && it->exptime < now) {
                item_unlink(it);
                it = 0;
            }

            if (it) {
                stats.get_hits++;
                it->refcount++;
                item_update(it);
                *(c->ilist + i) = it;
                i++;
                if (i > c->isize) {
                    c->isize *= 2;
                    c->ilist = realloc(c->ilist, sizeof(item *) * c->isize);
                }
            }
            else {
                stats.get_misses++;
            }
        }
        c->icurr = c->ilist;
        c->ileft = i;
        if (c->ileft) {
            c->ipart = 0;
            c->state = conn_mwrite;
            c->ibytes = 0;
            return;
        }
        else {
            out_string(c, "END");
            return;
        }
    }

    if (strncmp(command, "delete ", 7) == 0) {
        char key[256];
        char *start = command + 7;
        char s_noreply[8];
        bool noreply;

        int res = sscanf(start, " %s %s\n", key, s_noreply);

        if (res < 1 || strlen(key) == 0) {
            out_string(c, "CLIENT_ERROR bad command line format");
            return;
        }

        noreply = (res == 2 && strcmp(s_noreply, "noreply") == 0) ? 1 : 0;

        item *it = judy_find(key);
        if (!it) {
            maybe_out_string(c, "NOT_FOUND", !noreply);
            return;
        }
        else if (it->exptime && it->exptime < time(0)) {
            // Expired
            item_unlink(it);
            maybe_out_string(c, "NOT_FOUND", !noreply);
            return;
        }
        else if (it->it_flags & ITEM_DELETED) {
            // Already deleted
            maybe_out_string(c, "NOT_FOUND", !noreply);
            return;
        }
        else {
            // Delete the item
            it->refcount++;
            /* use its expiration time as its deletion time now */
            it->exptime = time(0) + 4;
            it->it_flags |= ITEM_DELETED;
            todelete[delcurr++] = it;
            if (delcurr >= deltotal) {
                deltotal *= 2;
                todelete = realloc(todelete, sizeof(item *) * deltotal);
            }
        }
        maybe_out_string(c, "DELETED", !noreply);
        return;
    }

    if (strncmp(command, "stats", 5) == 0) {
        process_stat(c, command);
        return;
    }

    if (strcmp(command, "version") == 0) {
        out_string(c, "VERSION 2.0.1");
        return;
    }

    if (strcmp(command, "quit") == 0) {
        c->state = conn_closing;
        return;
    }

    out_string(c, "ERROR");
    return;
}

/*
 * if we have a complete line in the buffer, process it and move whatever
 * remains in the buffer to its beginning.
 */
bool
try_read_command(conn *c)
{
    char *el, *cont;

    if (!c->rbytes)
        return false;
    el = memchr(c->rbuf, '\n', c->rbytes);
    if (!el)
        return false;
    cont = el + 1;
    if (el - c->rbuf > 1 && *(el - 1) == '\r') {
        el--;
    }
    *el = '\0';

    process_command(c, c->rbuf);

    if (cont - c->rbuf < c->rbytes) { /* more stuff in the buffer */
        memmove(c->rbuf, cont, c->rbytes - (cont - c->rbuf));
    }
    c->rbytes -= (cont - c->rbuf);
    return true;
}

/*
 * read from network as much as we can, handle buffer overflow and connection
 * close.
 * return 0 if there's nothing to read on the first read.
 */
bool
try_read_network(conn *c)
{
    bool gotdata = false;
    int res;
    while (1) {
        if (c->rbytes >= c->rsize) {
            char *new_rbuf = realloc(c->rbuf, c->rsize * 2);
            if (!new_rbuf) {
                if (settings.verbose) {
                    fprintf(stderr, "Couldn't realloc input buffer\n");
                }
                c->rbytes = 0; /* ignore what we read */
                out_string(c, "SERVER_ERROR out of memory");
                c->write_and_go = conn_closing;
                return true;
            }
            c->rbuf = new_rbuf;
            c->rsize *= 2;
        }
        res = read(c->sfd, c->rbuf + c->rbytes, c->rsize - c->rbytes);
        if (res > 0) {
            stats.bytes_read += res;
            gotdata = true;
            c->rbytes += res;
            continue;
        }
        if (res == 0) {
            /* connection closed */
            c->state = conn_closing;
            return true;
        }
        if (res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            else {
                return false;
            }
        }
    }
    return gotdata;
}

bool
update_event(conn *c, int new_flags)
{
    if (c->ev_flags == new_flags) {
        return true;
    }
    if (event_del(&c->event) == -1) {
        return false;
    }
    event_set(&c->event, c->sfd, new_flags, event_handler, (void *)c);
    c->ev_flags = new_flags;
    if (event_add(&c->event, 0) == -1) {
        return false;
    }
    return true;
}

void
drive_machine(conn *c)
{
    bool exit = false;
    int sfd, flags = 1;
    socklen_t addrlen;
    struct sockaddr addr;
    conn *newc;
    int res;

    while (!exit) {
        // printf("state: %s\n", conn_state_to_str(c->state));
        switch (c->state) {
            case conn_listening:
                addrlen = sizeof(addr);
                if ((sfd = accept(c->sfd, &addr, &addrlen)) == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        perror("accept() shouldn't block");
                    }
                    else {
                        perror("accept()");
                    }
                    return;
                }
                if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
                    fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    perror("setting O_NONBLOCK");
                    close(sfd);
                    return;
                }
                newc = conn_new(sfd, conn_read, EV_READ | EV_PERSIST);
                if (!newc) {
                    if (settings.verbose)
                        fprintf(stderr, "couldn't create new connection\n");
                    close(sfd);
                    return;
                }
                exit = true;
                break;

            case conn_read:
                if (try_read_command(c)) {
                    continue;
                }
                if (try_read_network(c)) {
                    continue;
                }
                /* we have no command line and no data to read from network */
                if (!update_event(c, EV_READ | EV_PERSIST)) {
                    if (settings.verbose) {
                        fprintf(stderr, "Couldn't update event\n");
                    }
                    c->state = conn_closing;
                    break;
                }
                exit = true;
                break;

            case conn_nread:
                /* we are reading rlbytes into rcurr; */
                if (c->rlbytes == 0) {
                    complete_nread(c);
                    break;
                }
                /* first check if we have leftovers in the conn_read buffer */
                if (c->rbytes > 0) {
                    int tocopy = c->rbytes > c->rlbytes ? c->rlbytes : c->rbytes;
                    memcpy(c->rcurr, c->rbuf, tocopy);
                    c->rcurr += tocopy;
                    c->rlbytes -= tocopy;
                    if (c->rbytes > tocopy) {
                        memmove(c->rbuf, c->rbuf + tocopy, c->rbytes - tocopy);
                    }
                    c->rbytes -= tocopy;
                    break;
                }

                /*  now try reading from the socket */
                res = read(c->sfd, c->rcurr, c->rlbytes);
                if (res > 0) {
                    stats.bytes_read += res;
                    c->rcurr += res;
                    c->rlbytes -= res;
                    break;
                }
                if (res == 0) { /* end of stream */
                    c->state = conn_closing;
                    break;
                }
                if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    if (!update_event(c, EV_READ | EV_PERSIST)) {
                        if (settings.verbose)
                            fprintf(stderr, "Couldn't update event\n");
                        c->state = conn_closing;
                        break;
                    }
                    exit = true;
                    break;
                }
                /* otherwise we have a real error, on which we close the connection */
                if (settings.verbose) {
                    fprintf(stderr, "Failed to read, and not due to blocking\n");
                }
                c->state = conn_closing;
                break;

            case conn_swallow:
                /* we are reading sbytes and throwing them away */
                if (c->sbytes == 0) {
                    c->state = conn_read;
                    break;
                }

                /* first check if we have leftovers in the conn_read buffer */
                if (c->rbytes > 0) {
                    int tocopy = c->rbytes > c->sbytes ? c->sbytes : c->rbytes;
                    c->sbytes -= tocopy;
                    if (c->rbytes > tocopy) {
                        memmove(c->rbuf, c->rbuf + tocopy, c->rbytes - tocopy);
                    }
                    c->rbytes -= tocopy;
                    break;
                }

                /*  now try reading from the socket */
                res = read(c->sfd, c->rbuf, c->rsize > c->sbytes ? c->sbytes : c->rsize);
                if (res > 0) {
                    stats.bytes_read += res;
                    c->sbytes -= res;
                    break;
                }
                if (res == 0) { /* end of stream */
                    c->state = conn_closing;
                    break;
                }
                if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    if (!update_event(c, EV_READ | EV_PERSIST)) {
                        if (settings.verbose)
                            fprintf(stderr, "Couldn't update event\n");
                        c->state = conn_closing;
                        break;
                    }
                    exit = true;
                    break;
                }
                /* otherwise we have a real error, on which we close the connection */
                if (settings.verbose) {
                    fprintf(stderr, "Failed to read, and not due to blocking\n");
                }
                c->state = conn_closing;
                break;

            case conn_write:
                /* we are writing wbytes bytes starting from wcurr */
                if (c->wbytes == 0) {
                    if (c->write_and_free) {
                        free(c->write_and_free);
                        c->write_and_free = 0;
                    }
                    c->state = c->write_and_go;
                    break;
                }
                res = write(c->sfd, c->wcurr, c->wbytes);
                if (res > 0) {
                    stats.bytes_written += res;
                    c->wcurr += res;
                    c->wbytes -= res;
                    break;
                }
                if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    if (!update_event(c, EV_WRITE | EV_PERSIST)) {
                        if (settings.verbose)
                            fprintf(stderr, "Couldn't update event\n");
                        c->state = conn_closing;
                        break;
                    }
                    exit = true;
                    break;
                }
                /* if res==0 or res==-1 and error is not EAGAIN or EWOULDBLOCK,
                   we have a real error, on which we close the connection */
                if (settings.verbose) {
                    fprintf(stderr, "Failed to write, and not due to blocking\n");
                }
                c->state = conn_closing;
                break;
            case conn_mwrite:
                /*
                 * we're writing ibytes bytes from iptr. iptr alternates between
                 * ibuf, where we build a string "VALUE...", and it->data for the
                 * current item. When we finish a chunk, we choose the next one using
                 * ipart, which has the following semantics: 0 - start the loop, 1 -
                 * we finished ibuf, go to current it->data; 2 - we finished it->data,
                 * move to the next item and build its ibuf; 3 - we finished all items,
                 * write "END".
                 */
                if (c->ibytes > 0) {
                    res = write(c->sfd, c->iptr, c->ibytes);
                    if (res > 0) {
                        stats.bytes_written += res;
                        c->iptr += res;
                        c->ibytes -= res;
                        break;
                    }
                    if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        if (!update_event(c, EV_WRITE | EV_PERSIST)) {
                            if (settings.verbose) {
                                fprintf(stderr, "Couldn't update event\n");
                            }
                            c->state = conn_closing;
                            break;
                        }
                        exit = true;
                        break;
                    }
                    /* if res==0 or res==-1 and error is not EAGAIN or EWOULDBLOCK,
                       we have a real error, on which we close the connection */
                    if (settings.verbose) {
                        fprintf(stderr, "Failed to write, and not due to blocking\n");
                    }
                    c->state = conn_closing;
                    break;
                }
                else {
                    item *it;
                    /* we finished a chunk, decide what to do next */
                    switch (c->ipart) {
                        case 1:
                            it = *(c->icurr);
                            c->iptr = it->data;
                            c->ibytes = it->nbytes;
                            c->ipart = 2;
                            break;
                        case 2:
                            it = *(c->icurr);
                            item_remove(it);
                            if (c->ileft <= 1) {
                                c->ipart = 3;
                                break;
                            }
                            else {
                                c->ileft--;
                                c->icurr++;
                            }
                            /* FALL THROUGH */
                        case 0:
                            it = *(c->icurr);
                            time_t now = time(0);
                            time_t remaining = it->exptime - now;
                            sprintf(c->ibuf, "VALUE %s %ld %u\r\n", it->key, remaining,
                                    it->nbytes - 2);
                            c->iptr = c->ibuf;
                            c->ibytes = strlen(c->iptr);
                            c->ipart = 1;
                            break;
                        case 3:
                            out_string(c, "END");
                            break;
                    }
                }
                break;

            case conn_closing:
                conn_close(c);
                exit = true;
                break;
        }
    }

    return;
}

void
event_handler(int fd, short which, void *arg)
{
    conn *c = (conn *)arg;

    c->which = which;

    /* sanity */
    if (fd != c->sfd) {
        if (settings.verbose)
            fprintf(stderr, "Catastrophic: event fd doesn't match conn fd!\n");
        conn_close(c);
        return;
    }

    /* do as much I/O as possible until we block */
    drive_machine(c);

    /* wait for next event */
    return;
}

int
new_socket(void)
{
    int sfd;
    int flags;

    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket()");
        return -1;
    }

    if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 || fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("setting O_NONBLOCK");
        close(sfd);
        return -1;
    }
    return sfd;
}

int
server_socket(int port)
{
    int sfd;
    struct linger ling = {0, 0};
    struct sockaddr_in addr;
    int flags = 1;

    if ((sfd = new_socket()) == -1) {
        return -1;
    }

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
    setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));
    setsockopt(sfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = settings.interface;
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind()");
        close(sfd);
        return -1;
    }
    if (listen(sfd, 1024) == -1) {
        perror("listen()");
        close(sfd);
        return -1;
    }
    return sfd;
}

struct event deleteevent;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void
delete_handler(int fd, short which, void *arg)
{
    // printf("Running delete handler\n");
    // printf(" %d items to delete\n", delcurr);

    // Reschedule oneself in another DELTA_DELETE_SECONDS
    if (deleteevent.ev_base) {
        evtimer_del(&deleteevent);
    }
    evtimer_set(&deleteevent, delete_handler, 0);
    struct timeval t = {DELTA_DELETE_SECONDS, 0};
    evtimer_add(&deleteevent, &t);

    {
        int i, j = 0;
        time_t now = time(0);
        for (i = 0; i < delcurr; i++) {
            if (todelete[i]->exptime < now) {
                /* no longer mark it deleted. it's now expired, same as dead */
                todelete[i]->it_flags &= ~ITEM_DELETED;
                todelete[i]->refcount--;
            }
            else {
                todelete[j++] = todelete[i];
            }
        }
        delcurr = j;
    }

    return;
}
#pragma GCC diagnostic pop

void
usage(void)
{
    printf("-p <num>      port number to listen on\n");
    printf("-l <ip_addr>  interface to listen on, default is INDRR_ANY\n");
    printf("-s <num>      maximum number of items to store, default is unlimited\n");
    printf(
        "-m <num>      max memory to use for items in megabytes, default is "
        "64 MB\n");
    printf("-c <num>      max simultaneous connections, default is 1024\n");
    printf("-k            lock down all paged memory\n");
    printf("-v            verbose (print errors/warnings while in event loop)\n");
    printf("-d            run as a daemon\n");
    printf("-h            print this help and exit\n");

    return;
}

int
main(int argc, char **argv)
{
    int c;
    int l_socket;
    conn *l_conn;
    int lock_memory = 0;
    int daemonize = 0;
    struct in_addr addr = {INADDR_ANY};

    /* init settings */
    settings_init();

    /* process arguments */
    while ((c = getopt(argc, argv, "p:s:m:c:khvdl:")) != -1) {
        switch (c) {
            case 'p': {
                settings.port = atoi(optarg);
            } break;
            case 's': {
                settings.maxitems = atoi(optarg);
            } break;
            case 'm': {
                settings.maxbytes = atoi(optarg) * 1024 * 1024;
            } break;
            case 'c': {
                settings.maxconns = atoi(optarg);
            } break;
            case 'h': {
                usage();
                exit(0);
            } break;
            case 'k': {
                lock_memory = 1;
            } break;
            case 'v': {
                settings.verbose = 1;
            } break;
            case 'l': {
                settings.interface = addr;
            } break;
            case 'd': {
                daemonize = 1;
            } break;
            default: {
                fprintf(stderr, "Illegal argument \"%c\"\n", c);
                return 1;
            }
        }
    }

    /* initialize other stuff stuff */
    item_init();
    event_init();
    stats_init();
    judy_init();
    conn_init();
    slabs_init(settings.maxbytes);

    /* daemonize if requested */
    if (daemonize) {
        // int res = daemon(0, 0);
        int res = posix_spawn(0, 0, 0, 0, 0, 0);
        if (res == -1) {
            fprintf(stderr, "failed to fork() in order to daemonize\n");
            return 1;
        }
    }

    /* lock paged memory if needed */
    if (lock_memory) {
        mlockall(MCL_CURRENT | MCL_FUTURE);
    }

    /* create the listening socket and bind it */
    l_socket = server_socket(settings.port);
    if (l_socket == -1) {
        fprintf(stderr, "failed to listen\n");
        exit(1);
    }

    /* create the initial listening connection */
    if (!(l_conn = conn_new(l_socket, conn_listening, EV_READ | EV_PERSIST))) {
        fprintf(stderr, "failed to create listening connection");
        exit(1);
    }

    /* initialise deletion array and timer event */
    todelete = malloc(sizeof(item *) * deltotal);
    delete_handler(0, 0, 0); /* sets up the event */

    /* enter the loop */
    printf("Entering main event loop\n");
    event_loop(0);

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "list.h"
#include "global.h"

struct iobuf_node* alloc_iobuf_node(struct iobuf* b, int len)
{
    struct iobuf_node* n = malloc(sizeof(*n) + len);
    if (!n)
        return NULL;
    INIT_LIST_HEAD(&n->l);
    b->total_len += (n->len = len);
    list_add_tail(&n->l, &b->nodes);
    return n;
}

void free_iobuf_node(struct iobuf_node* n)
{
    if (!n)
        return;
    list_del(&n->l);
    free(n);
}

void iobuf_init(struct iobuf* buf)
{
    memset(buf, 0, sizeof(*buf));
    INIT_LIST_HEAD(&buf->nodes);
}

void iobuf_free(struct iobuf* buf)
{
    struct iobuf_node* i, *i_next;
    list_for_each_entry_safe(i, i_next, &buf->nodes, l) {
        free_iobuf_node(i);
    }
    assert(list_empty(&buf->nodes));
    iobuf_init(buf);
}

void iobuf_cpy(char* buf, struct iobuf* b)
{
    int pos = 0;
    struct iobuf_node* i;

    buf[pos] = '\0';
    list_for_each_entry(i, &b->nodes, l) {
        memcpy(&buf[pos], i->buf, i->len);
        pos += i->len;
    }
    if (buf[pos - 1] != '\0')
        buf[pos - 1] = '\0';
}

struct iobuf* alloc_iobuf()
{
    struct iobuf* buf = malloc(sizeof(*buf));
    if (!buf)
        return NULL;
    iobuf_init(buf);
    return buf;
}

void free_iobuf(struct iobuf* buf)
{
    if(!buf)
        return;
    iobuf_free(buf);
    free(buf);
}

void iobuf_append (struct iobuf *b, char * d, int len )
{
    struct iobuf_node *n = alloc_iobuf_node(b, len);
    memcpy(n->buf, d, len);
}

size_t write_cb ( void * ptr, size_t size, size_t nmemb, void * stream )
{
    __debug ( "DATA RCVD %d items of size %d \n",  nmemb, size );
    iobuf_append ( stream, ptr, nmemb*size );
    return nmemb * size;
}

#if 0
int list_queues()
{
    char* const url = "http://" HOSTNAME 
        "?Action=ListQueues"
        "&AWSAccessKeyId=RELQ7OTAGDO570Y9CMAQ&";
        //"Timestamp=2012-08-04T00%3A14%3A54.157Z";
    struct iobuf* b = alloc_iobuf();
    int ret;

    if ((ret = sqs_request_simple(b, url)))
        return ret;

    struct iobuf_node* i;
    list_for_each_entry(i, &b->nodes, l) {
        i->buf[i->len - 1] = '\0';
        printf("%s\n", i->buf);
    }

    free_iobuf(b);
    return 0;
}
#endif

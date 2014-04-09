#ifndef __GLOBAL_H__
#define __GLOBAL_H__

//
//parse recv response data
//
struct recv_msg {
    struct list_head l;
    const xmlChar *handle;
    const xmlChar *body;
};

int  parse_recv_msg(const char* xml, int len, struct list_head* l);
void free_recv_msgs(struct list_head* head);


struct iobuf_node {
    struct list_head l;
    int len;
    char buf[0];
};

struct iobuf {
    struct list_head nodes;
    int total_len;
};


#define __debug(fmt, args ...)  do {} while(0)
//#define __debug(fmt, args ...)  fprintf(stderr, fmt, ##args)

#define HOSTNAME    "mq-2:6059"
#define HOSTID      "396241819186"

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

size_t write_cb ( void * ptr, size_t size, size_t nmemb, void * stream );

void iobuf_init(struct iobuf* b);
void iobuf_free(struct iobuf* b);

void iobuf_cpy(char* buf, struct iobuf* b);

#endif

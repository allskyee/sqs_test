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
int  parse_recv_msg_check(const char* xml, int len, char* msg_id, 
        int (*check_func)(char* body, int len, void* priv), void* priv);
int  parse_send_msg(const char* xml, int len, char* msg_id);


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

//#define HOSTNAME    "mq-2:6059"
//#define HOSTID      "398690460065"
//#define ACCESSKEY   "8BSQRC7XW1FPXB522YQ9"

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

size_t write_cb ( void * ptr, size_t size, size_t nmemb, void * stream );

void iobuf_init(struct iobuf* b);
void iobuf_free(struct iobuf* b);

void iobuf_cpy(char* buf, struct iobuf* b);

#define MSGID_MAX_LEN  64

#endif

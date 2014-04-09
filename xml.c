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

struct recv_msg* alloc_recv_msg()
{
    struct recv_msg* msg = malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    INIT_LIST_HEAD(&msg->l);
    return msg;
}

void free_recv_msg(struct recv_msg* msg)
{
    if (msg->handle)
        xmlFree((void*)msg->handle);
    if (msg->body)
        xmlFree((void*)msg->body);
    list_del(&msg->l);
    free(msg);
}

void free_recv_msgs(struct list_head* head)
{
    struct recv_msg* i, *i_next = NULL;
    list_for_each_entry_safe(i, i_next, head, l) 
        free_recv_msg(i);
}

int parse_recv_msg(const char* xml, int len, struct list_head* l)
{
    int ret = -EINVAL, msg_cnt;
    struct recv_msg* i, *i_next = NULL;

    xmlDocPtr doc = xmlReadMemory(xml, len, "noname.xml", NULL, 0);
    if (!doc) {
        fprintf(stderr, "unable to read memory\n");
        return ret;
    }

    xmlNodePtr cur = xmlDocGetRootElement(doc);
    if (!cur) 
        goto free;

    //get ReceiveMessageResult
    //get ResponseMetadata
    
    INIT_LIST_HEAD(l);

    cur = xmlFirstElementChild(cur);
    msg_cnt = 0;
    while (cur) {
        if (xmlStrcmp(cur->name, (const xmlChar *)"ReceiveMessageResult"))
            goto next;

        xmlNodePtr msg = xmlFirstElementChild(cur);
        while (msg) {
            //parse msg for ReceiptHandle, Body
            xmlNodePtr msg_i = xmlFirstElementChild(msg);
            struct recv_msg* recv_msg = NULL;

            if (!(recv_msg = alloc_recv_msg())) 
                goto free;

            list_add_tail(&recv_msg->l, l);
            msg_cnt++;

            while (msg_i) {
                if (!xmlStrcmp(msg_i->name, (const xmlChar *)"ReceiptHandle")) {
                    if (recv_msg->handle)
                        goto free_msg;
                    if (!(recv_msg->handle = xmlNodeGetContent(msg_i)))
                        goto free_msg;
                }
                else if (!xmlStrcmp(msg_i->name, (const xmlChar *)"Body")) {
                    if (recv_msg->body)
                        goto free_msg;
                    if (!(recv_msg->body = xmlNodeGetContent(msg_i)))
                        goto free_msg;
                }

                msg_i = xmlNextElementSibling(msg_i);
            }

            if (recv_msg->handle == NULL || recv_msg->body == NULL) {
                fprintf(stderr, "%p %p\n", 
                    recv_msg->handle, recv_msg->body);
                goto free_msg;
            }

            msg = xmlNextElementSibling(msg);
        }

    next :
        cur = cur->next;
    }
   
    xmlFreeDoc(doc);
    return msg_cnt;
    
free_msg : 
    free_recv_msgs(l);
free : 
    xmlFreeDoc(doc);
    return ret;
}


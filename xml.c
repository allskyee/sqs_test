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

int parse_recv_msg_check(const char* xml, int len, char* msg_id, 
    int (*check_func)(char* body, int len, void* priv), void* priv)
{
    int ret = -EINVAL, msg_cnt, pos;
    struct recv_msg* i, *i_next = NULL;

    xmlDocPtr doc = xmlReadMemory(xml, len, "noname.xml", NULL, 0);
    if (!doc) {
        fprintf(stderr, "unable to read memory\n");
        return ret;
    }

    xmlNodePtr cur = xmlDocGetRootElement(doc);
    if (!cur) 
        goto free;

    cur = xmlFirstElementChild(cur);
    msg_cnt = pos = 0;
    while (cur) {
        if (xmlStrcmp(cur->name, (const xmlChar *)"ReceiveMessageResult"))
            goto next;

        xmlNodePtr msg = xmlFirstElementChild(cur);
        while (msg) {
            //parse msg for ReceiptHandle, Body
            xmlNodePtr msg_i = xmlFirstElementChild(msg);
            xmlChar* handle = NULL, *body = NULL;

            while (msg_i) {
                if (!xmlStrcmp(msg_i->name, (const xmlChar *)"ReceiptHandle")) {
                    if (handle)
                        break;
                    if (!(handle = xmlNodeGetContent(msg_i)))
                        break;
                }
                else if (!xmlStrcmp(msg_i->name, (const xmlChar *)"Body")) {
                    if (body)
                        break;
                    if (!(body = xmlNodeGetContent(msg_i)))
                        break;
                }

                msg_i = xmlNextElementSibling(msg_i);
            }

            if (!msg_i && handle && body && check_func(body, strlen(body), priv)) { 
                // sucess : check body, save msg_id
                if (msg_id) {
                    strcpy(&msg_id[pos], handle);
                    pos += (strlen(handle) + 1);
                    msg_id[pos - 1] = '\0';
                }
                msg_cnt++;
            }
            xmlFree(handle);
            xmlFree(body);

            msg = xmlNextElementSibling(msg);
        }

    next :
        cur = cur->next;
    }

    ret = msg_cnt;
   
free :
    xmlFreeDoc(doc);
    return ret;
}

int parse_send_msg(const char* xml, int len, char* msg_id)
{
    int ret = -EINVAL, msg_cnt, pos;

    xmlDocPtr doc = xmlReadMemory(xml, len, "noname.xml", NULL, 0);
    if (!doc) {
        fprintf(stderr, "unable to read memory\n");
        return ret;
    }

    xmlNodePtr cur = xmlDocGetRootElement(doc);
    if (!cur) 
        goto free;

    cur = xmlFirstElementChild(cur);
    msg_cnt = pos = 0;
    while (cur) {
        if (xmlStrcmp(cur->name, (const xmlChar *)"SendMessageResult"))
            goto next;

        xmlNodePtr msg = xmlFirstElementChild(cur);
        while (msg) {
            if (!xmlStrcmp(msg->name, (const xmlChar *)"MessageId")) {
                xmlChar* m = xmlNodeGetContent(msg);
                strcpy(&msg_id[pos], m);
                pos += (strlen(m) + 1);
                msg_id[pos - 1] = '\0';
                msg_cnt++;
                xmlFree(m);
            }
            msg = xmlNextElementSibling(msg);
        }
    next : 
        cur = cur->next;
    }

    ret = msg_cnt;

free : 
    xmlFreeDoc(doc);
    return ret;
}

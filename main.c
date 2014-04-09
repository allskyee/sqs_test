#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>

#include <linux/unistd.h>
#include <sys/syscall.h>
#include <signal.h>

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "list.h"
#include "global.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
//

int __recv_message(int n, int delete)
{
    char* resp = NULL;
    int i, pos, ret = -ENOMEM, resp_len = (1 << 20); //1MB
    struct iobuf b;
    
    CURL* ch1 = curl_easy_init();
    CURL* ch2 = curl_easy_init();
    if (!ch1 || !ch2) {
        fprintf(stderr, "unable to easy init\n");
        goto out;
    }

    if (!(resp = malloc(resp_len))) {
        fprintf(stderr, "unable to alloc memory\n");
        goto out;
    }

    iobuf_init(&b);

    curl_easy_setopt (ch1, CURLOPT_URL, 
        "http://" HOSTNAME "/" HOSTID "/test_create_queue");
    curl_easy_setopt (ch1, CURLOPT_POST, 1 );
    curl_easy_setopt (ch1, CURLOPT_POSTFIELDS, 
        "Action=ReceiveMessage&MaxNumberOfMessages=1" 
        "&AWSAccessKeyId=RELQ7OTAGDO570Y9CMAQ&");
    curl_easy_setopt (ch1, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt (ch1, CURLOPT_WRITEDATA, &b);

    curl_easy_setopt (ch2, CURLOPT_URL, 
        "http://" HOSTNAME "/" HOSTID "/test_create_queue");
    curl_easy_setopt (ch2, CURLOPT_POST, 1 );
    curl_easy_setopt (ch2, CURLOPT_POSTFIELDS, resp); 
    curl_easy_setopt (ch2, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt (ch2, CURLOPT_WRITEDATA, &b);

    for (i = 0; i < n; i++) {
        CURLcode res;
        int parsed;
        struct list_head msgs;
        struct recv_msg* recv_i;

        res = curl_easy_perform(ch1);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
            break;
        }

        //merge the response and check
        assert(resp_len > b.total_len);
        iobuf_cpy(resp, &b);
        iobuf_free(&b);
        if (strncmp(resp, "<ReceiveMessageResponse>",   
                strlen("<ReceiveMessageResponse>")) != 0) {
            break;
        }

        //printf("%s\n", resp);
        //parse received message
        parsed = parse_recv_msg(resp, strlen(resp), &msgs);
        if (parsed <= 0) {
            if (parsed == 0)
                ret = 0;//successful finish

            free_recv_msgs(&msgs);
            break;
        }

        //delete all recieved messages
        list_for_each_entry(recv_i, &msgs, l)  {
            sprintf(resp, "Action=DeleteMessage&ReceiptHandle=%s" 
                "&AWSAccessKeyId=RELQ7OTAGDO570Y9CMAQ&", recv_i->handle);

            res = curl_easy_perform(ch2);
            if (res != CURLE_OK) {
                fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
                break;
            }

            assert(resp_len > b.total_len);
            iobuf_cpy(resp, &b);
            iobuf_free(&b);
            if (strncmp(resp, "<DeleteMessageResponse>",   
                    strlen("<DeleteMessageResponse>")) != 0) {
                break;
            }

            //printf("del : %s\n", resp);
            
            parsed--;
        }

        free_recv_msgs(&msgs);

        if (parsed != 0)
            break;
    }

    if (i == n)
        ret = 0;

    printf("%d recieved \n", i);

out : 
    free(resp);
    curl_easy_cleanup(ch1);
    curl_easy_cleanup(ch2);
    return ret;
}

void* recv_message(void* tmp)
{
    int n = *((int*)tmp);
    return (void*)__recv_message(n, 0);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int __send_message(int n, int size)
{
    char* body = NULL, *resp = NULL;
    int i, pos, ret = -ENOMEM, resp_len = 1024;
    struct iobuf b;

    CURL* ch = curl_easy_init();
    if (!ch) {
        fprintf(stderr, "unable to easy init\n");
        return ret;
    }

    if (!(body = malloc(size + 256))) {
        fprintf(stderr, "unable to alloc memory\n");
        goto out;
    }

    if (!(resp = malloc(resp_len))) {
        fprintf(stderr, "unable to alloc memory\n");
        goto out;
    }

    pos = snprintf(body, 256, "Action=SendMessage&MessageBody=");
    memset(&body[pos], 'x', size);
    sprintf(&body[pos + size], "&AWSAccessKeyId=RELQ7OTAGDO570Y9CMAQ&");

    iobuf_init(&b);

    curl_easy_setopt (ch, CURLOPT_URL, 
            "http://" HOSTNAME "/" HOSTID "/test_create_queue");
    curl_easy_setopt (ch, CURLOPT_POST, 1 );
    curl_easy_setopt (ch, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt (ch, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt (ch, CURLOPT_WRITEDATA, &b);

    for (i = 0; i < n; i++) {
        CURLcode res;

        res = curl_easy_perform(ch);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
            break;
        }

        //merge the response and check
        assert(resp_len > b.total_len);
        iobuf_cpy(resp, &b);
        iobuf_free(&b);

        if (strncmp(resp, "<SendMessageResponse>",   
                strlen("<SendMessageResponse>")) != 0) {
            break;
        }

    }

    if (i == n)
        ret = 0;

out : 
    free(body);
    free(resp);
    curl_easy_cleanup(ch);
    return ret;
}

struct send_msg_args {
    int n;
    int size; //in bytes
};

void* send_message(void* tmp)
{
    struct send_msg_args* args = tmp;
    return (void*)__send_message(args->n, args->size);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int start_threads(int threads, void* (*thr)(void*), void* arg) 
{
    struct timespec start, end;
    int i, j, ret = 0;
    pthread_t t[128] = {0};

    if (threads > ARRAY_SIZE(t)) {
        fprintf(stderr, "too many threads\n");
        return -1;
    }

    //printf("%d %d\n", n, threads);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (i = 0; i < threads; i++) {
        if (pthread_create(&t[i], NULL, thr, arg)) {
            fprintf(stderr, "unable to create thread\n");
            ret = -1;
            goto thread_fail;
        }
    }
    for (i = 0; i < threads; i++) {
        int res, ret = pthread_join(t[i], &res);
        if (ret || res)
            printf("ret %d, res %d\n", ret, res);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    long long elapsed = (end.tv_sec - start.tv_sec) * 1000000000 + 
            (end.tv_nsec - start.tv_nsec);
    
    printf("%f elapsed \n", elapsed / 1000000000.0);

    return 0;

thread_fail : 
    for (j = 0; j < i; j++) {
        pthread_cancel(&t[i]);
        pthread_join(&t[i]);
    }
    return ret;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "commands send, recieve\n");
        return -1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    if (strcmp(argv[1], "send") == 0) {
        if (argc != 5) {
            fprintf(stderr, "specify [size] [msgs] [threads]\n");
            return -1;
        }
        int size = strtoul(argv[2], NULL, 10);
        int n = strtoul(argv[3], NULL, 10);
        int threads = strtoul(argv[4], NULL, 10);
        struct send_msg_args args = { n, size };
        if (!size || !n || !threads)
            return 0;
        return start_threads(threads, &send_message, &args);
    }
    else if(strcmp(argv[1], "recv") == 0) { //receive all messages
        if (argc != 4) {
            fprintf(stderr, "specify [msgs] [threads]\n");
            fprintf(stderr, "invalid usage\n");
            return -1;
        }
        int n = strtoul(argv[2], NULL, 10);
        int threads = strtoul(argv[3], NULL, 10);
        if (!n || !threads)
            return 0;
        return start_threads(threads, &recv_message, &n);
    }

    return 0;
}

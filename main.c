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
// recieve

int recv_check_body(char* body, int len, void* priv) //1 is ok, 0 is not
{
    while (--len >= 0)
        if (body[len] != 'x')
            return 0;
    return 1;
}

int __recv_message(int n, int verbose, char* msg_ids)
{
    char* resp = NULL;
    int i, pos, ret = -ENOMEM, resp_len = (1 << 20); //1MB
    struct iobuf b;

#define ERROR(fmt, args...) \
    do { if (verbose) fprintf(stderr, fmt, ##args); } while (0)
    
    CURL* ch1 = curl_easy_init();
    if (!ch1) {
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
        "&AWSAccessKeyId=" ACCESSKEY "&");
    curl_easy_setopt (ch1, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt (ch1, CURLOPT_WRITEDATA, &b);

    ret = 0;
    for (i = 0; i < n; i++) {
        CURLcode res;
        int cnt;

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

        cnt = parse_recv_msg_check(resp, strlen(resp), 
                msg_ids, recv_check_body, NULL);
        if (cnt < 0) {
            ERROR("2 %s\n", resp);
            break;
        }
        if (cnt == 0) {
            ERROR("no more message, %d received\n", i);
            break;
        }
        while (cnt--) 
            msg_ids += (strlen(msg_ids) + 1);

        ret++;
    }

out : 
    free(resp);
    curl_easy_cleanup(ch1);
    return ret;
}

void* recv_message(void* tmp)
{
    int n = *((int*)tmp);
    return (void*)__recv_message(n, 1, NULL);
}

void recv_message_save(int n)
{
    const int msg_id_len = 64;
    char* msg_ids = malloc(msg_id_len * n);
    FILE* f0 = NULL;
    int msgs;
    struct timespec start, end;
    long long elapsed;
    int ret = -ENOMEM, i, pos, max_retry;
    
    if (!msg_ids) { 
        fprintf(stderr, "unable to allocate memory\n");
        return;
    }

    if (!(f0 = fopen("recv_msg_id", "w"))) {
        goto out;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    msgs = __recv_message(n, 0, msg_ids);
    clock_gettime(CLOCK_MONOTONIC, &end);

    if (msgs < 0) {
        fprintf(stderr, "unable to receive any messages\n");
        goto out;
    }

    elapsed = (end.tv_sec - start.tv_sec) * 1000000000 + 
            (end.tv_nsec - start.tv_nsec);
    printf("%f elapsed \n", elapsed / 1000000000.0);
    printf("%d messages left\n", n - msgs);

    // print message ids
    for (i = 0, pos = 0; i < msgs; i++) {
        char* msg_id = &msg_ids[pos];
        fprintf(f0, "%s\n", msg_id);
        pos += (strlen(msg_id) + 1);
    }

    n -= msgs;
    max_retry = 10;
    while (n > 0 && max_retry) {
        msgs = __recv_message(n, 0, msg_ids);
        if (msgs <= 0) {
            fprintf(stderr, "unable to recv, sleeping 1 second, %d\n", msgs);
            max_retry--;
            sleep(1);
            continue;
        }

        max_retry = 10;

        for (i = 0, pos = 0; i < msgs; i++) {
            char* msg_id = &msg_ids[pos];
            fprintf(f0, "%s\n", msg_id);
            pos += (strlen(msg_id) + 1);
        }

        n -= msgs;
    }

out : 
    if (f0)
        fclose(f0);
    free(msg_ids);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
// send

int __send_message(int n, int size, int verbose, char* msg_ids)
{
    char* body = NULL, *resp = NULL;
    int i, pos, ret = -ENOMEM, resp_len = 1024;
    struct iobuf b;

    CURL* ch = curl_easy_init();
    if (!ch) {
        ERROR("unable to easy init\n");
        return ret;
    }

    if (!(body = malloc(size + 256))) {
        ERROR("unable to alloc memory\n");
        goto out;
    }

    if (!(resp = malloc(resp_len))) {
        ERROR("unable to alloc memory\n");
        goto out;
    }

    pos = snprintf(body, 256, "Action=SendMessage&MessageBody=");
    memset(&body[pos], 'x', size);
    sprintf(&body[pos + size], "&AWSAccessKeyId=" ACCESSKEY "&");

    iobuf_init(&b);

    curl_easy_setopt (ch, CURLOPT_URL, 
            "http://" HOSTNAME "/" HOSTID "/test_create_queue");
    curl_easy_setopt (ch, CURLOPT_POST, 1 );
    curl_easy_setopt (ch, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt (ch, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt (ch, CURLOPT_WRITEDATA, &b);

    ret = 0;
    for (i = 0; i < n; i++) {
        CURLcode res = curl_easy_perform(ch);
        if (res != CURLE_OK) {
            ERROR("curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
            break;
        }

        //merge the response and check
        assert(resp_len > b.total_len);
        iobuf_cpy(resp, &b);
        iobuf_free(&b);
        if (strncmp(resp, "<SendMessageResponse>",   
                strlen("<SendMessageResponse>")) != 0) {
            ERROR("1 %s\n", resp);
            break;
        }

        if (msg_ids) {
            int cnt = parse_send_msg(resp, strlen(resp), msg_ids);
            if (cnt < 0) {
                ERROR("2 %s\n", resp);
                break;
            }
            while (cnt--) 
                msg_ids += (strlen(msg_ids) + 1);
        }

        ret++;
    }

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
    return (void*)__send_message(args->n, args->size, 1, NULL);
}

void send_message_save(int n, int size)
{
    const int msg_id_len = 64;
    char* msg_ids = malloc(msg_id_len * n);
    int msgs, i, pos;
    struct timespec start, end;
    long long elapsed;
    FILE* f0 = NULL;
    int max_retry;

    if (!msg_ids) { 
        fprintf(stderr, "unable to allocate memory\n");
        return;
    }

    if (!(f0 = fopen("sent_msg_id", "w"))) {
        goto out;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    msgs = __send_message(n, size, 0, msg_ids);
    clock_gettime(CLOCK_MONOTONIC, &end);

    if (msgs < 0) {
        fprintf(stderr, "unable to receive any messages\n");
        goto out;
    }

    elapsed = (end.tv_sec - start.tv_sec) * 1000000000 + 
            (end.tv_nsec - start.tv_nsec);
    printf("%f elapsed \n", elapsed / 1000000000.0);
    printf("%d messages left\n", n - msgs);

    // print message ids
    for (i = 0, pos = 0; i < msgs; i++) {
        char* msg_id = &msg_ids[pos];
        fprintf(f0, "%s\n", msg_id);
        pos += (strlen(msg_id) + 1);
    }

    n -= msgs;
    max_retry = 10;
    while (n > 0 && max_retry) {
        msgs = __send_message(n, size, 0, msg_ids);
        if (msgs <= 0) {
            fprintf(stderr, "unable to send, sleeping 1 second\n");
            max_retry--;
            sleep(1);
            continue;
        }

        max_retry = 10;

        for (i = 0, pos = 0; i < msgs; i++) {
            char* msg_id = &msg_ids[pos];
            fprintf(f0, "%s\n", msg_id);
            pos += (strlen(msg_id) + 1);
        }

        n -= msgs;
    }

out : 
    if (f0)
        fclose(f0);
    free(msg_ids);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
// delete

int del_message(const char* fn)
{
    FILE* f = fopen(fn, "r");
    char* resp = NULL, line[MSGID_MAX_LEN];
    CURL* ch = NULL;
    int max_len = 1024, ret = -EINVAL, i;
    struct iobuf b;
    struct timespec start, end;
    long long elapsed;

    if (!f) {
        fprintf(stderr, "unable to open file\n");
        return -1;
    }

    ch = curl_easy_init();
    if (!ch) {
        fprintf(stderr, "unable to easy init\n");
        goto out;
    }

    if (!(resp = malloc(max_len))) {
        fprintf(stderr, "unable to alloc memory\n");
        goto out;
    }

    iobuf_init(&b);

    curl_easy_setopt (ch, CURLOPT_URL, 
        "http://" HOSTNAME "/" HOSTID "/test_create_queue");
    curl_easy_setopt (ch, CURLOPT_POST, 1 );
    curl_easy_setopt (ch, CURLOPT_POSTFIELDS, resp);
    curl_easy_setopt (ch, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt (ch, CURLOPT_WRITEDATA, &b);

    i = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (fgets(line, sizeof(line), f) != NULL) {
        CURLcode res;

        line[strlen(line) - 1] = '\0';
        sprintf(resp, "Action=DeleteMessage&ReceiptHandle=%s" 
            "&AWSAccessKeyId=" ACCESSKEY "&", line);

    retry : 
        res = curl_easy_perform(ch);
        if (res != CURLE_OK) {
            // unable to perform 
            fprintf(stderr, "unable to perform operation\n");
            sleep(1);
            goto retry;
        }

        iobuf_cpy(resp, &b);
        iobuf_free(&b);
        if (strncmp(resp, "<DeleteMessageResponse>",   
                    strlen("<DeleteMessageResponse>")) != 0) {
            // invalid resply
            fprintf(stderr, "%s\n", resp);
            sleep(1);
            goto retry;
        }

        i++;
        //if ((i % 1000) == 0)
        //    printf("%d\n", i);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = (end.tv_sec - start.tv_sec) * 1000000000 + 
            (end.tv_nsec - start.tv_nsec);
    printf("%f elapsed \n", elapsed / 1000000000.0);

    ret = 0;

out : 
    free(resp);
    if (ch)
        curl_easy_cleanup(ch);
    fclose(f);
    return ret;
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
        fprintf(stderr, "commands send, recv, send_save, recv_save, del_saved \n");
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
    else if (strcmp(argv[1], "recv") == 0) { //receive all messages
        if (argc != 4) {
            fprintf(stderr, "specify [msgs] [threads]\n");
            return -1;
        }
        int n = strtoul(argv[2], NULL, 10);
        int threads = strtoul(argv[3], NULL, 10);
        if (!n || !threads)
            return 0;
        return start_threads(threads, &recv_message, &n);
    }
    else if (strcmp(argv[1], "send_save") == 0) {
        if (argc != 4) {
            fprintf(stderr, "specify [size] [msgs] \n");
            return -1;
        }
        int size = strtoul(argv[2], NULL, 10);
        int n = strtoul(argv[3], NULL, 10);
        if (!size || !n)
            return 0;
        send_message_save(n, size);
    }
    else if (strcmp(argv[1], "recv_save") == 0) {
        if (argc != 3) {
            fprintf(stderr, "specify [msgs] \n");
            return -1;
        }
        int n = strtoul(argv[2], NULL, 10);
        if (!n)
            return 0;
        recv_message_save(n);
    }
    else if (strcmp(argv[1], "del_saved") == 0) {
        if (argc != 3) {
            fprintf(stderr, "specify [msg IDs file] \n");
            return -1;
        }
        del_message(argv[2]);
    }

    return 0;
}

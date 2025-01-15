#include "threadpool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

threadpool* create_threadpool(int num_threads_in_pool, int max_queue_size){
    // Input validation
    if (num_threads_in_pool <= 0 || num_threads_in_pool > MAXT_IN_POOL ||
        max_queue_size <= 0 || max_queue_size > MAXW_IN_QUEUE) {
        return NULL;
    }

    threadpool *tp = (threadpool*) malloc(sizeof(threadpool));
    if(tp == NULL){
        perror("malloc");
        return NULL;
    }

    memset(tp, 0, sizeof(threadpool));
    tp->max_qsize = max_queue_size;
    tp->num_threads = num_threads_in_pool;

    // Allocate thread array
    tp->threads = (pthread_t*) malloc(tp->num_threads * sizeof(pthread_t));
    if(tp->threads == NULL){
        perror("malloc");
        free(tp);
        return NULL;
    }

    // Initialize synchronization primitives
    if (pthread_mutex_init(&tp->qlock, NULL) != 0) {
        free(tp->threads);
        free(tp);
        return NULL;
    }

    if (pthread_cond_init(&tp->q_not_empty, NULL) != 0 ||
        pthread_cond_init(&tp->q_empty, NULL) != 0 ||
        pthread_cond_init(&tp->q_not_full, NULL) != 0) {
        pthread_mutex_destroy(&tp->qlock);
        free(tp->threads);
        free(tp);
        return NULL;
    }

    for (int i = 0; i < num_threads_in_pool; i++) {
        if (pthread_create(&(tp->threads[i]), NULL, do_work, tp) != 0){
            perror("create threads");
            tp->shutdown = 1; // Signal the need to clean up
            for (int j = 0; j < i; j++) {
                pthread_join(tp->threads[j], NULL); // Wait for created threads to finish
            }
            free(tp->threads);
            pthread_mutex_destroy(&(tp->qlock));
            pthread_cond_destroy(&(tp->q_not_empty));
            pthread_cond_destroy(&(tp->q_empty));
            pthread_cond_destroy(&(tp->q_not_full));
            free(tp);
            return NULL;
        }
    }

    return tp;
}

void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg){
    if(from_me == NULL || dispatch_to_here == NULL)
        return;

    // 1. create and init work_t element
    work_t* work = (work_t*) malloc(sizeof(work_t));
    if (work == NULL)
        return;
    work->routine = dispatch_to_here;
    work->arg = arg;
    work->next = NULL;

    // 2. lock the mutex
    pthread_mutex_lock(&from_me->qlock);

    // Check if we should accept new jobs
    if(from_me->dont_accept){
        pthread_mutex_unlock(&from_me->qlock);
        free(work);
        return;
    }

    // 3. if queue is full, wait
    while(from_me->qsize >= from_me->max_qsize){
        pthread_cond_wait(&from_me->q_not_full, &from_me->qlock);

        // Check again after waking up
        if(from_me->dont_accept){
            pthread_mutex_unlock(&from_me->qlock);
            free(work);
            return;
        }
    }

    // 4. add the work_t element to the queue
    if(from_me->qsize == 0){
        // Empty queue
        from_me->qhead = work;
        from_me->qtail = work;
    }
    else{
        // Add to tail
        from_me->qtail->next = work;
        from_me->qtail = work;
    }
    from_me->qsize++;

    // Signal that the queue is not empty
    pthread_cond_signal(&from_me->q_not_empty);

    // 5. Unlock mutex
    pthread_mutex_unlock(&from_me->qlock);
}

void* do_work(void* p){
    threadpool *tp = (threadpool*) p;

    while(1) {
        pthread_mutex_lock(&tp->qlock);
        while(tp->qsize == 0 && !tp->shutdown)
            pthread_cond_wait(&tp->q_not_empty, &tp->qlock);

        if(tp->shutdown){
            pthread_mutex_unlock(&tp->qlock);
            pthread_exit(NULL);
        }

        work_t *work = tp->qhead;
        if(work){
            tp->qhead = work->next;
            if(!tp->qhead)
                tp->qtail = NULL;
            tp->qsize--;
        }

        if(tp->qsize == 0 && tp->dont_accept)
            pthread_cond_signal(&tp->q_empty);
        if(tp->qsize < tp->max_qsize)
            pthread_cond_signal(&tp->q_not_full);

        pthread_mutex_unlock(&tp->qlock);

        if(work){
            (*(work->routine))(work->arg);
            free(work);
        }
    }
}

void destroy_threadpool(threadpool* destroyme){
    pthread_mutex_lock(&destroyme->qlock);

    // Set flags to signal threads to stop accepting work and shutdown
    destroyme->dont_accept = 1;

    // Wait for the queue to empty
    while(destroyme->qsize > 0) {
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
    }

    // Set the shutdown flag and signal all threads
    destroyme->shutdown = 1;
    pthread_cond_broadcast(&destroyme->q_not_empty);

    pthread_mutex_unlock(&(destroyme->qlock));

    for(int i = 0; i < destroyme->num_threads; i++){
        pthread_join(destroyme->threads[i], NULL);
    }

    // Clean up the queue (if any work remains). shouldnt be possible though.
    work_t *current = destroyme->qhead;
    while(current != NULL){
        work_t *temp = current;
        current = current->next;
        free(temp);
    }

    // Free threads array
    free(destroyme->threads);

    // Destroy mutex and condition variables
    pthread_mutex_destroy(&(destroyme->qlock));
    pthread_cond_destroy(&(destroyme->q_not_empty));
    pthread_cond_destroy(&(destroyme->q_empty));
    pthread_cond_destroy(&(destroyme->q_not_full));

    // Free the threadpool structure itself
    free(destroyme);
}


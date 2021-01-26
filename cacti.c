/*
 * znaczna część implementacji puli wątków zaczerpnięta z
 * https://nachtimwald.com/2019/04/12/thread-pool-in-c/?fbclid=IwAR3JTnR-ac-w4fw-UEdIFSCCIo7b0kGcEzrT7BzZ__JfssbKa2chJcs2Xek
 */
#include "cacti.h"

static bool interrupted;

struct actor_info {
    actor_id_t      id;
    bool            dead;
    uint            take_from;
    uint            place_at;
    message_t       queue[ACTOR_QUEUE_LIMIT];
    void            *state;
    role_t          *role;
};
typedef struct actor_info actor_info_t;

struct pool_work {
    actor_id_t       id;
    struct pool_work *next;
};
typedef struct pool_work pool_work_t;

struct thread_pool {
    pool_work_t     *work_first;
    pool_work_t     *work_last;
    pthread_mutex_t work_mutex;
    pthread_cond_t  work_cond;
    pthread_cond_t  dead_cond;
    size_t          working_cnt;
    size_t          thread_cnt;
    bool            stop;
};
typedef struct thread_pool thread_pool_t;

thread_pool_t *pool;

actor_id_t actor_count;
actor_id_t dead_actor_count;
actor_id_t actors_array_size;
actor_info_t *actors;
pthread_mutex_t actor_array_mutex;

static int actor_init(actor_id_t id, role_t *const role) {
    actors[id].dead      = false;
    actors[id].take_from = 0;
    actors[id].place_at  = 0;
    actors[id].role      = role;
    actors[id].id        = id;
    actors[id].state     = NULL;

    return 0;
}

static pool_work_t *pool_work_create(actor_id_t id) {
    pool_work_t *work;

    work       = malloc(sizeof(*work));
    work->id   = id;
    work->next = NULL;

    return work;
}

static void pool_work_destroy(pool_work_t *work) {
    if (work == NULL)
        return;
    free(work);
}

static bool pool_add_work(actor_id_t id) {
    pool_work_t *work;

    if (pool == NULL)
        return false;

    work = pool_work_create(id);
    if (work == NULL)
        return false;

    pthread_mutex_lock(&(pool->work_mutex));
    if (pool->work_first == NULL) {
        pool->work_first = work;
        pool->work_last = pool->work_first;
    } else {
        pool->work_last->next = work;
        pool->work_last = work;
    }

    pthread_cond_broadcast(&(pool->work_cond));
    pthread_mutex_unlock(&(pool->work_mutex));

    return true;
}

static pool_work_t *pool_work_get() {
    pool_work_t *work;

    if (pool == NULL)
        return NULL;

    work = pool->work_first;
    if (work == NULL)
        return NULL;

    if (work->next == NULL) {
        pool->work_first = NULL;
        pool->work_last  = NULL;
    } else {
        pool->work_first = work->next;
    }

    return work;
}

static pthread_key_t actor_key;
static void *pool_worker() {
    pool_work_t  *work;
    actor_info_t *actor_info;
    message_t    *message;
    role_t       *role;

    while (true) {
        pthread_mutex_lock(&(pool->work_mutex));
        while (pool->work_first == NULL && !pool->stop)
            pthread_cond_wait(&(pool->work_cond), &(pool->work_mutex));

        if (pool->stop)
            break;

        work = pool_work_get();
        pool->working_cnt++;
        pthread_mutex_unlock(&(pool->work_mutex));

        if (work != NULL) {
            actor_id_t id = work->id;
            pool_work_destroy(work);

            pthread_mutex_lock(&actor_array_mutex);

            actor_info = &actors[id];

            if (actor_info->dead) {
                pthread_mutex_unlock(&actor_array_mutex);
                continue;
            }

            pthread_setspecific(actor_key, (void *)(&id));

            message = &actor_info->queue[actor_info->take_from];
            role = actor_info->role;
            actor_info->take_from = (actor_info->take_from + 1) % ACTOR_QUEUE_LIMIT;

            //printf("robota aktora o id %ld wzięta z %d type = %ld\n",
            //       id, actor_info->take_from - 1, message->message_type);

            pthread_mutex_unlock(&actor_array_mutex);

            if (message->message_type == MSG_SPAWN) {
                pthread_mutex_lock(&actor_array_mutex);
                if (!interrupted) {
                    if (actor_count == actors_array_size) {
                        actors_array_size *= 2;
                        actors = realloc(actors, actors_array_size);
                    }
                    actor_init(actor_count, message->data);
                    actor_id_t send_to_id = actor_count;
                    actor_count++;

                    pthread_mutex_unlock(&actor_array_mutex);

                    message_t hello_message;
                    hello_message.message_type = MSG_HELLO;
                    hello_message.data = (void *) (&actor_info->id);
                    hello_message.nbytes = sizeof(*hello_message.data);

                    send_message(send_to_id, hello_message);
                } else
                    pthread_mutex_unlock(&actor_array_mutex);
            }
            else if (message->message_type == MSG_GODIE) {
                pthread_mutex_lock(&actor_array_mutex);

                actor_info->dead = true;
                dead_actor_count++;

                if (dead_actor_count == actor_count) {
                    pool->stop = true;
                    pthread_cond_broadcast(&(pool->work_cond));
                }

                pthread_mutex_unlock(&actor_array_mutex);
            }
            else if (message->message_type == MSG_HELLO) {
                actor_info->state = NULL;
                role->prompts[MSG_HELLO](&(actor_info->state), sizeof(message->data), message->data);
            }
            else {
                if (message->message_type >= (long)role->nprompts) {
                    printf("Unknown message type\n");
                    continue;
                }
                role->prompts[message->message_type](&(actor_info->state), sizeof(message->data), message->data);
            }
        }

        pthread_mutex_lock(&(pool->work_mutex));
        pool->working_cnt--;

        pthread_mutex_lock(&actor_array_mutex);
        if (actor_info->take_from != actor_info->place_at)
            pthread_cond_broadcast(&(pool->work_cond));
        pthread_mutex_unlock(&actor_array_mutex);

        pthread_mutex_unlock(&(pool->work_mutex));
    }

    pool->thread_cnt--;
    pthread_cond_signal(&(pool->dead_cond));
    pthread_mutex_unlock(&(pool->work_mutex));
    return NULL;
}

actor_id_t actor_id_self() {
    actor_id_t *id = pthread_getspecific(actor_key);
    return *id;
}

static void *SIGINT_catcher() {
    sigset_t block_mask;

    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGRTMIN + 1);

    int sig;
    sigwait(&block_mask, &sig);

    if (sig == SIGINT) { // if sig == SIGRTMIN do nothing, just end
        pthread_mutex_lock(&actor_array_mutex);
        interrupted = true;

        for (actor_id_t actor = 0; actor < actor_count; actor++) {
            actor_info_t *actor_info = &actors[actor];
            if (!actor_info->dead) {
                message_t godie_msg;
                godie_msg.message_type = MSG_GODIE;

                actor_info->queue[actor_info->place_at] = godie_msg;
                actor_info->place_at = (actor_info->place_at + 1) % ACTOR_QUEUE_LIMIT;

                pool_add_work(actor);
            }
        }

        pthread_mutex_unlock(&actor_array_mutex);
    }


    return NULL;
}

pthread_t *threads;
int actor_system_create(actor_id_t *actor, role_t *const role) {
    interrupted = false;
    threads = calloc(1 + POOL_SIZE, sizeof(pthread_t *));

    if (pthread_create(&threads[0], NULL, SIGINT_catcher, NULL) != 0)
        exit(1);

    sigset_t set;
    sigfillset(&set);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
        return -1;

    actors_array_size = 1000;
    actors = calloc(actors_array_size, sizeof(actor_info_t));

    if (pthread_mutex_init(&(actor_array_mutex), NULL) != 0)
        return -1;

    if (actor_init(0, role) != 0)
        return -1;

    actor_count = 1;
    dead_actor_count = 0;

    pool = calloc(1, sizeof(thread_pool_t));
    pool->thread_cnt = POOL_SIZE;

    if (pthread_mutex_init(&(pool->work_mutex), NULL) != 0)
        return -1;
    if (pthread_cond_init(&(pool->work_cond), NULL) != 0)
        return -1;
    if (pthread_cond_init(&(pool->dead_cond), NULL) != 0)
        return -1;

    pthread_key_create(&actor_key, NULL);

    for (int i = 0; i < POOL_SIZE; i++) {
        if (pthread_create(&threads[i + 1], NULL, pool_worker, NULL) != 0)
            exit(1);
    }

    *actor = 0;
    message_t hello_msg;
    hello_msg.message_type = MSG_HELLO;
    send_message(*actor, hello_msg);
    return 0;
}

static void actor_system_clear_memory() {
    pool_work_t *work;
    pool_work_t *work2;

    if (pool == NULL)
        return;

    work = pool->work_first;
    while (work != NULL) {
        work2 = work->next;
        pool_work_destroy(work);
        work = work2;
    }

    pthread_mutex_lock(&actor_array_mutex);

    if (!interrupted)
        pthread_kill(threads[0], SIGRTMIN + 1);

    pthread_mutex_unlock(&actor_array_mutex);

    for (int i = 0; i < POOL_SIZE + 1; i++)
        pthread_join(threads[i], NULL);

    pthread_mutex_destroy(&(pool->work_mutex));
    pthread_mutex_destroy(&actor_array_mutex);
    pthread_cond_destroy(&(pool->work_cond));
    pthread_cond_destroy(&(pool->dead_cond));
    pthread_key_delete(actor_key);

    free(threads);
    free(actors);
    free(pool);
}

void actor_system_join(actor_id_t actor) {
    if (pool == NULL)
        return;

    pthread_mutex_lock(&(pool->work_mutex));
    while (true) {
        if (pool->thread_cnt != 0)
            pthread_cond_wait(&(pool->dead_cond), &(pool->work_mutex));
        else
            break;
    }
    pthread_mutex_unlock(&(pool->work_mutex));

    actor_system_clear_memory();
}

int send_message(actor_id_t actor, message_t message) {
    pthread_mutex_lock(&actor_array_mutex);
    if (interrupted) {
        pthread_mutex_unlock(&actor_array_mutex);
        return 0;
    }

    if (actor >= actors_array_size) {
        pthread_mutex_unlock(&actor_array_mutex);
        return -2;
    }

    actor_info_t *actor_info = &actors[actor];

    if (actor_info->dead) {
        pthread_mutex_unlock(&actor_array_mutex);
        return -1;
    }

    actor_info->queue[actor_info->place_at] = message;
    actor_info->place_at = (actor_info->place_at + 1) % ACTOR_QUEUE_LIMIT;

    pthread_mutex_unlock(&actor_array_mutex);

    pool_add_work(actor);
    return 0;
}
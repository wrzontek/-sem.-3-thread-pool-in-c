#include <unistd.h>
#include "cacti.h"

#define value 0
#define time 1

int k, n;
int ***M;

message_t spawn_msg;
message_t godie_msg;

actor_id_t spawned_count = 0;
int64_t done_count = 0;
pthread_cond_t ready;
pthread_cond_t done;
pthread_mutex_t done_mutex;
pthread_mutex_t ready_mutex;

void hello_prompt (void **stateptr, size_t nbytes, void *data) {
    spawned_count++;
    
    actor_id_t id = actor_id_self();
    if (spawned_count < n)
        send_message(id, spawn_msg);
    else
        pthread_cond_broadcast(&ready);
}

void matrix_prompt (void **stateptr, size_t nbytes, void *data) {
    int64_t *d = (int64_t *) data;
    actor_id_t id = actor_id_self();
    int64_t row = d[0];

    int sleep_time = M[row][id][time];
    usleep(sleep_time * 100000);

    d[1] += M[row][id][value]; // id jest tożsame z numerem kolumny

    pthread_mutex_lock(&done_mutex);
    done_count++;
    if (done_count == k * n)
        pthread_cond_broadcast(&done);

    pthread_mutex_unlock(&done_mutex);

    message_t matrix_msg;
    matrix_msg.message_type = 1;
    matrix_msg.data = d;
    matrix_msg.nbytes = sizeof(*d);

    if (id < n - 1)
        send_message(id + 1, matrix_msg);
}

int main(){
    scanf("%d", &k);
    scanf("%d", &n);
    M = (int ***) calloc(k * n * 2, sizeof(int *));
    for (int i = 0; i < k; i++) {
        M[i] = (int **) calloc(n * 2, sizeof(int *));
        for (int j = 0; j < n; j++)
            M[i][j] = (int *) calloc (2, sizeof(int *));
    }

    for (int i = 0; i < k * n; i++) {
        int row = i / n;
        int column = i % n;
        int v, t;
        scanf("%d %d", &v, &t);
        M[row][column][value] = v;
        M[row][column][time] = t;
    }

    actor_id_t first_actor;
    if (pthread_cond_init(&ready, NULL) != 0)
        return -1;
    if (pthread_cond_init(&done, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&done_mutex, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&ready_mutex, NULL) != 0)
        return -1;

    act_t prompts[] = {hello_prompt, matrix_prompt};
    role_t *role = malloc(sizeof(*role));
    role->prompts = prompts;
    role->nprompts = 2;

    spawn_msg.message_type = MSG_SPAWN;
    spawn_msg.data = role;
    spawn_msg.nbytes = sizeof(*role);

    godie_msg.message_type = MSG_GODIE;
    
    if (actor_system_create(&first_actor, role) != 0)
        return -1;

    pthread_mutex_lock(&ready_mutex);
    while (spawned_count != n)
        pthread_cond_wait(&ready, &ready_mutex);


    int64_t data[k][2];
    for (int i = 0; i < k; i++) {
        message_t matrix_msg;
        matrix_msg.message_type = 1;
        data[i][0] = i; // numer wiersza
        data[i][1] = 0; // dotychczasowa suma
        matrix_msg.data = data[i];
        matrix_msg.nbytes = sizeof(*data[i]);

        if (send_message(first_actor, matrix_msg) != 0)
            return -1;
    }


    pthread_mutex_lock(&done_mutex);
    while (done_count != k * n)
        pthread_cond_wait(&done, &done_mutex);

    for (int i = 0; i < n; i++)
        if (send_message(i, godie_msg) != 0)
            return -1;

    actor_system_join(first_actor);

    for (int i = 0; i < k; i++) {
        printf("%ld\n", data[i][1]);
    }

	return 0;
}

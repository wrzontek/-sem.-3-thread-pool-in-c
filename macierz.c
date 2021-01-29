#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "cacti.h"

#define value 0
#define time 1
#define MSG_MATRIX 1

int k, n;
int *column_actor;
int64_t *row_sums;
int ***M;

message_t spawn_msg;
message_t godie_msg;

actor_id_t spawned_count = 0;
pthread_mutex_t mutex;
pthread_cond_t ready;

void hello_prompt (void **stateptr,
                   size_t nbytes __attribute__((unused)),
                   void *data __attribute__((unused))) {
    pthread_mutex_lock(&mutex);

    int64_t *state = calloc(2, sizeof(int64_t)); // numer kolumny, ile wierszy przerobione
    state[0] = spawned_count;
    state[1] = 0;

    *stateptr = state;
    column_actor[spawned_count] = actor_id_self();

    spawned_count++;
    pthread_cond_broadcast(&ready);

    if (spawned_count < n) {
        pthread_mutex_unlock(&mutex);
        send_message(actor_id_self(), spawn_msg);
    } else
        pthread_mutex_unlock(&mutex);
}

void matrix_prompt (void **stateptr,
                    size_t nbytes __attribute__((unused)),
                    void *data) {
    pthread_mutex_lock(&mutex);
    int64_t *d = (int64_t *) data;        // numer wiersza, dotychczasowa suma
    int64_t *s = (int64_t *) (*stateptr); // numer kolumny, ile wierszy przerobione

    int64_t row = d[0];
    int64_t column = s[0];
    pthread_mutex_unlock(&mutex);

    int sleep_time = M[row][column][time];
    usleep(sleep_time);

    pthread_mutex_lock(&mutex);
    d[1] += M[row][column][value];
    s[1]++;

    message_t matrix_msg;
    matrix_msg.message_type = MSG_MATRIX;
    matrix_msg.data = d; //todo mutex?
    matrix_msg.nbytes = sizeof(*d); //todo == nbytes?

    pthread_mutex_unlock(&mutex);

    if (column < n - 1) {
        pthread_mutex_lock(&mutex);
        actor_id_t next_actor = column_actor[column + 1];
        pthread_mutex_unlock(&mutex);

        send_message(next_actor, matrix_msg);
    } else {
        pthread_mutex_lock(&mutex);
        row_sums[row] = d[1];
        pthread_mutex_unlock(&mutex);
    }

    pthread_mutex_lock(&mutex);
    s = (int64_t *) (*stateptr);
    if (s != NULL && s[1] == k) {
        free(s);
        *stateptr = NULL;
        send_message(actor_id_self(), godie_msg);
    }
    pthread_mutex_unlock(&mutex);
}

int main() {
    sigset_t set;
    sigfillset(&set);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
        return -1;

    scanf("%d", &k);
    scanf("%d", &n);
    M = (int ***) calloc(k * n * 2, sizeof(int *));
    for (int i = 0; i < k; i++) {
        M[i] = (int **) calloc(n * 2, sizeof(int *));
        for (int j = 0; j < n; j++)
            M[i][j] = (int *) calloc (2, sizeof(int *));
    }

    column_actor = calloc (n, sizeof(int));
    for (int i = 0; i < n; ++i)
        column_actor[i] = i;

    row_sums = calloc (k, sizeof(int64_t));
    for (int i = 0; i < n; ++i)
        row_sums[i] = 0;

    for (int i = 0; i < k * n; i++) {
        int row = i / n;
        int column = i % n;
        int v, t;
        scanf("%d %d", &v, &t);
        M[row][column][value] = v;
        M[row][column][time] = t;
    }

    actor_id_t first_actor;
    if (pthread_mutex_init(&mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&ready, NULL) != 0)
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

    pthread_mutex_lock(&mutex);
    while (spawned_count < n)
        pthread_cond_wait(&ready, &mutex);
    pthread_mutex_unlock(&mutex);

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

    actor_system_join(first_actor);

    for (int i = 0; i < k; i++)
        printf("%ld\n", row_sums[i]);

    if (pthread_mutex_destroy(&mutex) != 0)
        return  -1;

    if (pthread_cond_destroy(&ready) != 0)
        return  -1;

    for (int i = 0; i < k; i++) {
        for (int j = 0; j < n; j++)
            free(M[i][j]);
        free(M[i]);
    }

    free(M);
    free(role);

	return 0;
}

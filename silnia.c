#include <unistd.h>
#include "cacti.h"

message_t spawn_msg;
message_t godie_msg;

actor_id_t spawned_count = 0;
pthread_cond_t ready;
pthread_mutex_t ready_mutex;

void hello_prompt (void **stateptr, size_t nbytes, void *data) {
    spawned_count++;
    pthread_cond_broadcast(&ready);
}

void factorial_prompt (void **stateptr, size_t nbytes, void *data) {
    int64_t *d = (int64_t *) data;

    d[1]++;
    d[2] = d[2] * d[1];

    actor_id_t id = actor_id_self();

    if (d[1] < d[0]) {
        send_message(id, spawn_msg);

        message_t factorial_msg;
        factorial_msg.message_type = 1;
        factorial_msg.data = d;
        factorial_msg.nbytes = sizeof(*data);

        pthread_mutex_lock(&ready_mutex);
        while (id + 2 > spawned_count)
            pthread_cond_wait(&ready, &ready_mutex);

        pthread_mutex_unlock(&ready_mutex);
        send_message(id + 1, factorial_msg);
    } else
        printf("%ld\n", d[2]);

    send_message(id, godie_msg);
}

int main() {
    sigset_t set;
    sigfillset(&set);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
        return -1;

    int n;
    scanf("%d", &n);

    if (n == 0 || n == 1) {
        printf("1\n");
        return 0;
    } else if (n < 0) {
        printf("n cannot be negative\n");
        return 0;
    }

    if (pthread_cond_init(&ready, NULL) != 0)
        return -1;
    if (pthread_mutex_init(&ready_mutex, NULL) != 0)
        return -1;

    actor_id_t first_actor;

    act_t prompts[] = {hello_prompt, factorial_prompt};
    role_t *role = malloc(sizeof(*role));
    role->prompts = prompts;
    role->nprompts = 2;

    spawn_msg.message_type = MSG_SPAWN;
    spawn_msg.data = role;
    spawn_msg.nbytes = sizeof(*role);

    godie_msg.message_type = MSG_GODIE;

    if (actor_system_create(&first_actor, role) != 0)
        return -1;

    message_t factorial_msg;
    factorial_msg.message_type = 1;
    int64_t data[3] = {n, 1, 1}; // n, k, k!
    factorial_msg.data = data;
    factorial_msg.nbytes = sizeof(*data);
    
    if (send_message(first_actor, factorial_msg) != 0)
        return -1;

    actor_system_join(first_actor);

    /*if (actor_system_create(&first_actor, role) != 0)
        return -1;

    message_t factorial_msg2;
    factorial_msg2.message_type = 1;
    int64_t data2[3] = {n, 1, 1}; // n, k, k!
    factorial_msg2.data = data2;
    factorial_msg2.nbytes = sizeof(*data2);
    spawned_count = 0;

    if (send_message(first_actor, factorial_msg2) != 0)
        return -1;

    actor_system_join(first_actor);*/

    free(role);
    pthread_mutex_destroy(&ready_mutex);
    pthread_cond_destroy(&ready);
	return 0;
}

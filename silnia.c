#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "cacti.h"

#define MSG_FACT 1;
#define MSG_RELAY 2;

message_t spawn_msg;
message_t godie_msg;

void hello_prompt (void **stateptr,
                   size_t nbytes __attribute__((unused)),
                   void *data) {
    //printf("HELLO %ld\n", actor_id_self());

    if (data != NULL) {
        actor_id_t parent = *(actor_id_t *)(data);
        message_t son_msg;
        *stateptr = (void *)(actor_id_self());

        son_msg.data = *stateptr;
        son_msg.message_type = MSG_RELAY;
        son_msg.nbytes = sizeof(*son_msg.data);

        send_message(parent, son_msg);
    }
}


void factorial_prompt (void **stateptr,
                       size_t nbytes __attribute__((unused)),
                       void *data) {
    //printf("  FACTORIAL %ld\n", actor_id_self());
    int64_t *d = (int64_t *) data;

    d[1]++;
    d[2] = d[2] * d[1];

    if (d[1] == d[0]) { // n == k, koniec
        printf("%ld\n", d[2]);
        send_message(actor_id_self(), godie_msg);
    } else {
        *stateptr = d;
        send_message(actor_id_self(), spawn_msg);
    }
}

void relay_prompt (void **stateptr,
                   size_t nbytes __attribute__((unused)),
                   void *data) {
    //printf("    RELAY %ld\n", actor_id_self());
    actor_id_t son = (actor_id_t)(data);

    message_t factorial_msg;
    factorial_msg.message_type = MSG_FACT;
    factorial_msg.data = *stateptr;
    factorial_msg.nbytes = sizeof(*data);

    send_message(son, factorial_msg);
    send_message(actor_id_self(), godie_msg);
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

    actor_id_t first_actor;

    act_t prompts[] = {hello_prompt, factorial_prompt, relay_prompt};
    role_t *role = malloc(sizeof(*role));
    role->prompts = prompts;
    role->nprompts = 3;

    spawn_msg.message_type = MSG_SPAWN;
    spawn_msg.data = role;
    spawn_msg.nbytes = sizeof(*role);

    godie_msg.message_type = MSG_GODIE;

    if (actor_system_create(&first_actor, role) != 0)
        return -1;

    int64_t data[3] = {n, 1, 1}; // n, k, k!
    message_t factorial_msg;
    factorial_msg.message_type = MSG_FACT;
    factorial_msg.data = data;
    factorial_msg.nbytes = sizeof(*data);

    if (send_message(first_actor, factorial_msg) != 0)
        return -1;

    actor_system_join(first_actor);

    free(role);
    return 0;
}

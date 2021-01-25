#include "cacti.h"

message_t spawn_msg;
message_t godie_msg;

actor_id_t spawned_count = 0;
pthread_cond_t ready;

void hello_prompt (void **stateptr, size_t nbytes, void *data) {
    spawned_count++;
    pthread_cond_broadcast(&ready);
    printf("HELLO\n");
}

void factorial_prompt (void **stateptr, size_t nbytes, void *data) {
    printf("   FACTORIAL\n");
    int64_t *d = (int64_t *) data;

    printf("%ld %ld %ld\n", d[0], d[1], d[2]);
    d[1]++;
    d[2] = d[2] * d[1];
    printf("%ld %ld %ld\n" , d[0], d[1], d[2]);

    actor_id_t id = actor_id_self();

    if (d[1] < d[0]) {
        send_message(id, spawn_msg);

        message_t factorial_msg;
        factorial_msg.message_type = 1;
        factorial_msg.data = d;
        factorial_msg.nbytes = sizeof(*data);

        while (id + 1 > spawned_count)
            pthread_cond_wait(&ready, NULL);

        send_message(id + 1, factorial_msg);
    } else
        printf("wynik to %ld\n", d[2]);

    send_message(id, godie_msg);
}

int main() {
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

	return 0;
}

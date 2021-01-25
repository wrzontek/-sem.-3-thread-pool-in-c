#include "cacti.h"

#define value 0
#define time 1

message_t spawn_msg;
message_t godie_msg;

actor_id_t spawned_count = 0;
pthread_cond_t ready;

void hello_prompt (void **stateptr, size_t nbytes, void *data) {
    pthread_cond_broadcast(&ready);
    printf("HELLO\n");
}

void matrix_prompt (void **stateptr, size_t nbytes, void *data) {
    printf("   MACIERZ\n");
    int64_t *d = (int64_t *) data;
    actor_id_t id = actor_id_self();


    send_message(id, godie_msg);
}


int main(){
    int k, n;
    scanf("%d", &k);
    scanf("%d", &n);
    // k wierszy n kolumn
    int M[k][n][2];
/*    for (int i = 0; i < k; i++) {
        for (int j = 0; j < k; j++) {
            M[i][j] = 0;
        }
    }*/

    // id aktora odpowiada numerze kolumny

    for (int i = 0; i < k * n; i++) {
        int wiersz = i / n;
        int kolumna = i % n;
        int v, t;
        scanf("%d %d", &v, &t);
        M[wiersz][kolumna][value] = v;
        M[wiersz][kolumna][time] = t;
    }

    for (int i = 0; i < k; i++) {
        for (int j = 0; j < n; j++) {
            printf("%d ", M[i][j][value]);
        }
        printf("\n");
    }

    actor_id_t first_actor;

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

    actor_system_join(first_actor);
	return 0;
}

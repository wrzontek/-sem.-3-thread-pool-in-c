#include "cacti.h"

#define value 0
#define time 1
int main(){
    int k, n;
    scanf("%d", &k);
    scanf("%d", &n);
    // k wierszy n kolumn
    int M[k][n][2];
    for (int i = 0; i < k; i++) {
        for (int j = 0; j < k; j++) {
            M[i][j] = 0;
        }
    }

    // id aktora odpowiada numerze kolumny

    for (int i = 0; i < k * n; i++) {
        int wiersz = i / n;
        int kolumna = i % n;
        int v, t;
        scanf("%d %d", v, t);
        M[wiersz][kolumna][value] = v;
        M[wiersz][kolumna][time] = t;
    }

	return 0;
}

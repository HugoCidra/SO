/*
Daniel Ferreira Veiga - 2019216891
Hugo Batista Cidra Duarte - 2020219765
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define PIPE_NAME "SENSOR_PIPE"

typedef struct sensor {
    char* id;
    unsigned int intervalo;
    char* chave;
    int min;
    int max;
} sensor;

int fd;
unsigned long int sent = 0;

sensor* init() {
    sensor* s1 = malloc(sizeof(sensor*));

    s1->id = malloc(sizeof(char*)*4);
    s1->intervalo = 0;
    s1->chave = malloc(sizeof(char*)*4);
    s1->min = 0;
    s1->max = 0;

    return s1;
}

void erro(char *msg) {
    printf("Erro: %s\n", msg);
    exit(-1);
}

void sigtstp_handler(int signum) {
    printf("Total de mensagens enviadas para o System Manager: %ld\n", sent);
}

void sigint_handler(int signum) {
    close(fd);
    exit(0);
}

int main(int argc, char** argv) {
    sensor* s1 = init();
    srand(time(NULL));
    char* info = malloc(sizeof(char*)*4*2+2+5);
    char temp[32] = "";

    if(argc != 6) erro("Sensor <id do sensor> <intervalo de envios em s> <chave> <min val> <max val>");

    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    if(strlen(argv[1]) < 3 || strlen(argv[1]) > 32) erro("Tamanho do <ID> incorreto.");
    strcpy(s1->id, argv[1]);

    strcpy(temp, argv[2]);
    for(size_t i = 0; i < strlen(argv[2]); ++i) {
        if(isalpha(temp[i])) erro("<Intervalo de envios> deve ser unsigned int.");
    }
    s1->intervalo = atoi(argv[2]);

    if(strlen(argv[3]) < 3 || strlen(argv[3]) > 32) erro("Tamanho da <chave> incorreto.");
    strcpy(s1->chave, argv[3]);

    strcpy(temp, argv[4]);
    for(size_t i = 0; i < strlen(argv[4]); ++i) {
        if(isalpha(temp[i])) erro("<min> deve ser unsigned int.");
    }

    strcpy(temp, argv[5]);
    for(size_t i = 0; i < strlen(argv[5]); ++i) {
        if(isalpha(temp[i])) erro("<max> deve ser unsigned int.");
    }
    if((atoi(argv[4]) > atoi(argv[5]))) erro("Valor <min> com valor superior que valor <max>.");
    s1->min = atoi(argv[4]);
    s1->max = atoi(argv[5]);

    //mandar id, chave, valor entre min e max, tudo por named pipe (SENSOR_PIPE)

    if((fd = open(PIPE_NAME, O_WRONLY)) < 0) erro("Cannot open pipe for writting: ");

    while(1) {
        int value = (rand() % (s1->max - s1->min + 1)) + s1->min;

        strcpy(info, s1->id);
        strcat(info, "#");

        strcat(info, s1->chave);
        strcat(info, "#");

        char* temp = malloc(sizeof(char*));
        sprintf(temp, "%d", value);
        strcat(info, temp);

        write(fd, info, strlen(info));
        printf("%s\n", info);

        sent++;
        free(temp);

        sleep(s1->intervalo);
    }

    free(s1);
    return 0;
}
/*
Daniel Ferreira Veiga - 2019216891
Hugo Batista Cidra Duarte - 2020219765
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define BUFLEN 1024
#define PIPE_NAME "CONSOLE_PIPE"

typedef struct alert {
    char* id;
    char* chave;
    int min;
    int max;
} alert;

typedef struct messageQ {
    long type;
    char message[BUFLEN];
} messageQ;

int fd, msgID;
messageQ* mq;

alert* init() {
    alert* a1 = malloc(sizeof(alert));

    a1->id = malloc(sizeof(char*)*4);
    a1->chave = malloc(sizeof(char*)*4);
    a1->min = 0;
    a1->max = 0;

    return a1;
}

void erro(char *msg) {
    printf("Erro: %s\n", msg);
    msgctl(msgID, IPC_RMID, NULL);
    close(fd);
    exit(0);
}

void sigint_handler(int signum) {
    close(fd);
    msgctl(msgID, IPC_RMID, NULL);
    exit(0);
}

int main(int argc, char** argv) {
    char buf[BUFLEN];
    char* command = malloc(sizeof(char*)*2);
    key_t key;
    if(argc != 2) erro("User <id da consola>");

    signal(SIGINT, sigint_handler);

    if((fd = open(PIPE_NAME, O_WRONLY)) < 0) erro("Cannot open pipe for writting: ");
    
    key = ftok("queue", 65);
    msgID = msgget(key, 0666|IPC_CREAT);
    mq = malloc(sizeof(messageQ));

    while(1) {
        bzero((void *) buf, strlen(buf));
        int nread = read(0, buf, sizeof(buf));
        buf[nread] = '\0';
        buf[strcspn(buf, "\n")] = 0;
        char* token = strtok(buf, " ");

        //message queue stuff
        if(msgrcv(msgID, mq, sizeof(messageQ), 1, 0) > 0) printf("%s", mq->message);

        if(!strcmp(buf, "exit")) break;
        
        else if(!strcmp(buf, "stats")) {

            //Recebe estatisticas a partir da message queue

            strcpy(command, "STATS");
            write(fd, command, strlen(command));

            printf("Stats and stuff\n");
        }

        else if(!strcmp(buf, "reset")) {
            
            //Envia pelo named pipe (CONSOLE_PIPE) info para que todas as estatisticas sejam limpas
            
            strcpy(command, "RESET");
            write(fd, command, strlen(command));

            printf("All stats reset\n");
        }
        
        else if(!strcmp(buf, "sensors")) {
            
            //Recebe info sobre todos os sensores a partir da message queue, e lista tudo no ecra
            
            strcpy(command, "SENSORS");
            write(fd, command, strlen(command));

            printf("Sensor list:\n");
        }

        else if(!strcmp(token, "add_alert")) {
            
            //Programa envia info do alerta para o sys manager pelo named pipe (CONSOLE_PIPE)

            int count = 0, min = 0;
            strcpy(command, token);
            
            while(token != NULL) {
                token = strtok(NULL," ");

                if(token == NULL) break;

                if(count == 0) {
                    if(strlen(token) < 3 || strlen(token) > 32) {
                        printf("Tamanho incorreto do ID (3 a 32).\n");
                        break;

                    }
                }

                if(count == 1) {
                    if(strlen(token) < 3 || strlen(token) > 32) {
                        printf("Tamanho incorreto de chave (3 a 32).\n");
                        break;
                    }
                }

                if(count == 2) min = atoi(token);
                if(count == 3) {
                    if(min > atoi(token)) {
                        printf("Valor min maior que valor max.\n");
                        break;
                    }
                }

                strcat(command, " ");
                strcat(command, token);

                count++;
            }

            write(fd, command, strlen(command));
            printf("Alerta adicionado com sucesso!\n");
            
        }

        else if(!strcmp(token, "remove_alert")) {

            //Programa envia para sys manager por named pipe (CONSOLE_PIPE)

            strcpy(command, token);
            strtok(NULL, " ");

            if(strlen(token) < 3 || strlen(token) > 32) {
                printf("Tamanho incorreto de ID (3 a 32).\n");
                continue;
            }

            strcat(command, token);

            write(fd, command, strlen(command));

            printf("Alert %s removed.\n", token);
        }

        else if(!strcmp(buf, "list_alerts")) {
            
            //Recebe regras de alertas a partir da message queue, e lista tudo no ecra

            strcpy(command, "LIST_ALERTS");
            write(fd, command, strlen(command));

            printf("Alerts list.\n");
        }

        else printf("Comando nao suportado pelo sistema.\n");
    }
        
    close(fd);
    return 0;
}
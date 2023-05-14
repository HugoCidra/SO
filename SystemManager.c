/*
Daniel Ferreira Veiga - 2019216891
Hugo Batista Cidra Duarte - 2020219765
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include "SystemManager.h"

#define PIPE_NAME_C "CONSOLE_PIPE"
#define PIPE_NAME_S "SENSOR_PIPE"

int max;
sync *syncs;
shared_mem *s_mem;
config *cfg;
sensor* sensores;
worker* workers;
alert* alerts;
queue* internalQueue;

FILE *log_fp;
int shmid,pids[3];

void erro(char* msg) {
    printf("Erro: %s\n", msg);
    exit(0);
}

struct config *readConfig(char *file_name){
	struct config *conf;
	
	conf = (struct config * ) malloc(sizeof(struct config));
	
	int j = 0;
	char buf[512], *token, tokens[3][100];
	
    FILE *fp;

    fp = fopen(file_name, "r");

    if (fp == NULL){
        perror("Error while opening the file\n");
        exit(EXIT_FAILURE);
    }

    printf("Reading from the configurations file\n");

    int v1 = fscanf(fp, "%d\n%d\n%d\n%d\n%d",&(conf->queue_size), &(conf->N_Workers), &(conf->Max_key),&(conf->Max_Sensors),&(conf->Max_Alerts));
	
	if(v1 != 5 || conf->queue_size < 1|| conf->N_Workers < 1 || conf->Max_key < 1 || conf->Max_Sensors < 1 || conf->Max_Alerts < 0){
		printf("Ficheiro de Configurações inválido.\n");
		exit(1);
	}
	
	int v2;

    fclose(fp);
	
	//Print config
	printf("------------\n");
	printf("%d\n%d\n%d\n%d\n%d\n",conf->queue_size, conf->N_Workers, conf->Max_key, conf->Max_Sensors, conf->Max_Alerts);
	
	printf("------------\n");
	
	return conf;
}

void writeLog(char * string){
    char buffer[512]="";

    time_t rawtime;
    struct tm timeinfo;
    time( &rawtime );
    localtime_r( &rawtime, &timeinfo);
	
    sprintf(buffer,"%.2d:%.2d:%.2d %s\n",timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,string);
	
	sem_wait(syncs->log);
    write(1, buffer, strlen(buffer));
    fprintf(log_fp,"%s",buffer);
    fflush(log_fp);
    fflush(stdout);
	sem_post(syncs->log);
}

void adicionaQueue(char* string, int prio) {
    pthread_mutex_lock(syncs->queue_mutex);

    for(int i = 0; i < sizeof(internalQueue)/siezof(internalQueue[0]); i++) {
        if(internalQueue[i].prio == 3) {
            strcpy(internalQueue[i].command, string);
            internalQueue[i].prio = prio;
        }
    }

    pthread_mutex_unlock(syncs->queue_mutex);
}

void *SensorReader(){
	int fd;
	sensores = initS();
	char info[512] = "";
	if((fd = open(PIPE_NAME_S, O_RDONLY)) < 0) erro("Cannot open pipe for reading (Sensor): ");

	
	while(1) {
		bzero((void*) info, strlen(info));
		int nread = read(fd, info, sizeof(info));
		info[nread] = '\0';
		info[strcspn(info, "\n")] = 0;

		adicionaQueue(info, 2);
	}

	close(fd);
	free(sensores);
}

void *ConsoleReader(){
	int fd;
	char* id = malloc(sizeof(char*));
	
	while(1) {
		close(fd);
		if((fd = open(PIPE_NAME_C, O_RDONLY)) < 0) erro("Cannot open pipe for reading (Console): ");

		char command[512] = "";
		bzero((void *) command, strlen(command));
		int nread = read(fd, command, sizeof(command));
		command[nread] = '\0';
		command[strcspn(command, "\n")] = 0;

		adicionaQueue(command, 1);
	}
}

void *Dispatcher() {
    int aux;
	
    if((aux = sizeof(internalQueue)/sizeof(internalQueue[0])) > 0) {
		
        for(int i = 0; i < aux; i++) {
			pthread_mutex_lock(syncs->queue_mutex);

            if(internalQueue[i].prio == 1) {
                for(int j = 0; j < cfg->N_Workers; j++) {

                    if(!workers[j].active) {
                        write(workers[j].pipe[1], internalQueue[i].command, strlen(internalQueue[i].command));

                        strcpy(internalQueue[i].command, "");
                        internalQueue[i].prio = 3;

                    }
                }
            }

            else if(internalQueue[i].prio == 2) {
                for(int j = 0; j < cfg->N_Workers; j++) {
                    if (!workers[j].active){
						write(workers[j].pipe[1], internalQueue[i].command, strlen(internalQueue[i].command));
                        
                        strcpy(internalQueue[i].command, "");
                        internalQueue[i].prio = 3;

					}
                }
            }

			pthread_mutex_unlock(syncs->queue_mutex);
        }
    }

    //Meter writeLog()
}

void *AlertsWatcher(){
	//messageQ msg;

	printf("Alerts Watcher criado!!!\n");
}

void Worker(int* pipe,int id){ //SINCRONIZAÇAO
	workers->active = 1;

	close(pipe[1]);

	char reader[MAX];
	read(pipe[0],reader,MAX);
	char* token = strtok(reader, "#");
	if(!strcmp(token, "REMOVE_ALERT")) {
		
		token = strtok(NULL, "#");

		//Procurar e depois "eliminar"
		strcpy(alerts->id, "");
		alerts->min= 0;
		alerts->max= 0;
	}

	if(!strcmp(token, "LIST_ALERTS")) {
		printf("ID	Key	MIN	MAX");
		printf("%s	%s	%d	%d",alerts->id, alerts->chave, alerts->min, alerts->max);
	}
	if(!strcmp(token, "STATS")){
		sprintf(messageQ.msg,"%s %d %d %d %d %d", sensores->chave, sensores->recentValue,sensores->min,sensores->max,sensores->avg,sensores->count);
	}

	if(!strcmp(token, "RESET")) {
		alerts->min=0;
		alerts->max=0;
	}

	if(!strcmp(token, "SENSORS")) {
		printf("ID\n");
		for (int i = 0; i < sizeof(alert); i++){
			printf("%d\n",sensores->id);	
		}
			
		sprintf(messageQ.msg,sensores->id);
	}
	if(!strcmp(token,"ADD_ALERT")){
		token = strtok(NULL, "#");
		strcpy(alerts->id,token);
		token = strtok(NULL, "#");
		strcpy(alerts->chave,token);
		token = strtok(NULL, "#");
		alerts->min=atoi(token);
		token = strtok(NULL, "#");
		alerts->max=atoi(token);

		//Wtite log
	}

}

void init() {
	internalQueue = malloc(sizeof(queue)*cfg->queue_size);
    for(int i = 0; i < cfg->queue_size; ++i) {
        strcpy(internalQueue[i].command,"");
        internalQueue[i].prio = 3;
    }

	workers = malloc(sizeof(worker)*cfg->N_Workers);
    for(int i = 0; i < cfg->N_Workers; ++i) {
        workers[i].active = 0;
    }

    sensores = malloc(sizeof(sensor)*cfg->Max_Sensors);
    for(int i = 0; i < cfg->Max_Sensors; ++i) {
        sensores[i].id = malloc(sizeof(char*));
        sensores[i].chave = malloc(sizeof(char*));
        sensores[i].count = 0;
        sensores[i].sum = 0;
    }

    alerts = malloc(sizeof(alert)*cfg->Max_Alerts);
    for(int i = 0; i < cfg->Max_Alerts; ++i) {
        alerts[i].id = malloc(sizeof(char*));
        alerts[i].chave = malloc(sizeof(char*));
    }
}

int main(int argc, char* argv[]) {
    pthread_t *thrds;
	log_fp = fopen("logs.txt", "a");

	//Inicializacao de semaforos
	syncs = (sync*) malloc(sizeof(sync));
    sem_unlink("LOG");
	sem_unlink("ALERTS_W");
    syncs->log = sem_open("LOG", O_CREAT|O_EXCL, 0777, 1);
	syncs->alert_watcher_sem = sem_open("ALERTS_W", O_CREAT|O_EXCL, 0777, 1);
	syncs->queue_mutex = PTHREAD_MUTEX_INITIALIZER;

    writeLog("Program Started");

	//Create shared memory
	shmid = shmget(IPC_PRIVATE, sizeof(struct shared_mem), IPC_CREAT|0700);
    //if (shmid < 1) exit(1);
    s_mem = (struct shared_mem*) shmat(shmid, NULL, 0);
	//if (s_mem < (struct shared_mem *) 1) exit(1);
	
	writeLog("Shared Memory created");


	//Read the configuration file
    char *file_name = argv[1];

	cfg = readConfig(file_name);
    init();

	//Create and open named pipe if it does not exist already
	if(access(PIPE_NAME_C, F_OK) == -1) {
		if(mkfifo(PIPE_NAME_C, 0666) == -1) erro("Erro a criar CONSOLE_PIPE: ");
	}
	if(access(PIPE_NAME_S, F_OK) == -1) {
		if(mkfifo(PIPE_NAME_S, 0666) == -1) erro("Erro a criar SENSOR_PIPE: ");
	}

    if(pthread_create(&thrds[0], NULL, SensorReader, NULL) != 0){
		printf("Error creating thread scheduler.");
	}
	if(pthread_create(&thrds[1], NULL, ConsoleReader, NULL) != 0){
		printf("Error creating thread scheduler.");
	}
	if(pthread_create(&thrds[2], NULL, Dispatcher	, NULL) != 0){
		printf("Error creating thread scheduler.");
	}
	

	pthread_join(thrds[0], NULL);
	writeLog("SensorReader created");
	pthread_join(thrds[1], NULL);
	writeLog("ConsoleReader created");
	pthread_join(thrds[2], NULL);
	writeLog("Dispatcher created");

	
	

	if((pids[0] = fork()) == 0){
		
        AlertsWatcher();
		writeLog("AlertsWatcher created");
    }else if(pids[0] == -1){
		printf("Error creating AlertsWatcher process.\n");
		exit(1);
	}else{
		for( int i = 0 ;i< cfg->N_Workers;i++){

			if ((pids[1+i] = fork()) == 0){
				workers[i].id = i+1;
				Worker(workers[i].pipe,i+1);
				//write log
			}else if(pids[1+i] == -1){
				printf("Error creating Worker process.\n");
				exit(1);

			}else{
				writeLog("HOME_IOT SIMULATOR WAITING FOR LAST TASKS TO FINISH");
				wait(NULL);
				writeLog("HOME_IOT SIMULATOR CLOSING");
				sem_unlink("LOG");
				fclose(log_fp);
				
			}
		}
	}

	return 0;
}
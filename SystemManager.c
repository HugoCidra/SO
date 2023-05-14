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


pthread_t *thrds;
sync *syncs;
config *cfg;//
sensor* sensores;//
worker* workers;//
alert* alerts;//
queue* internalQueue;//

FILE *log_fp;
int shmid_s, shmid_a, shmid_w, pids[3], max, sensor_fd, console_fd;

void erro(char* msg) {
    printf("Erro: %s\n", msg);
	writeLog("Programa a terminar.");

	//close names pipes fd and wait workers
	close(sensor_fd);
	unlink(PIPE_NAME_S);
	close(console_fd);
	unlink(PIPE_NAME_C);
	wait(NULL);
	
	//unlink sems and destroy mutexes
	sem_close(syncs->alert_watcher_sem);
	sem_unlink("ALERTS_W");
	sem_close(syncs->log);
	sem_unlink("LOG");
	sem_close(syncs->shm_sem);
	sem_unlink("SHM");
	pthread_mutex_destroy(syncs->queue_mutex);

	//exit threads
	for(int i = 0; i < sizeof(thrds)/sizeof(thrds[0]); ++i) {
		pthread_cancel(thrds[i]);
	}

	//detatch shm
	shmdt(sensores);
	shmctl(shmid_s, IPC_RMID, NULL);
	shmdt(alerts);
	shmctl(shmid_a, IPC_RMID, NULL);
	shmdt(workers);
	shmctl(shmid_w, IPC_RMID, NULL);

	//free resources
	free(syncs);
	free(cfg);
	free(sensores);
	free(workers);
	free(alerts);
	free(internalQueue);

	close(log_fp);
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

	writeLog("Task adicionada a queue.");

    pthread_mutex_unlock(syncs->queue_mutex);
}

void *SensorReader(){
	sensores = initS();
	char info[512] = "";
	if((sensor_fd = open(PIPE_NAME_S, O_RDONLY)) < 0) erro("Cannot open pipe for reading (Sensor): ");

	
	while(1) {
		bzero((void*) info, strlen(info));
		int nread = read(sensor_fd, info, sizeof(info));
		info[nread] = '\0';
		info[strcspn(info, "\n")] = 0;

		adicionaQueue(info, 2);
	}

	close(sensor_fd);
	free(sensores);
}

void *ConsoleReader(){
	char* id = malloc(sizeof(char*));
	
	while(1) {
		close(console_fd);
		if((console_fd = open(PIPE_NAME_C, O_RDONLY)) < 0) erro("Cannot open pipe for reading (Console): ");

		char command[512] = "";
		bzero((void *) command, strlen(command));
		int nread = read(console_fd, command, sizeof(command));
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
						close(workers[j].pipe[0]);
                        write(workers[j].pipe[1], internalQueue[i].command, strlen(internalQueue[i].command));
						
						char temp[512] = "";
						sprintf(temp, "Task with priority level %d has been sent to worker %d", internalQueue[i].prio, workers[j].id);
						writeLog(temp);
                        
						strcpy(internalQueue[i].command, "");
                        internalQueue[i].prio = 3;

						
                    }
                }
            }

            else if(internalQueue[i].prio == 2) {
                for(int j = 0; j < cfg->N_Workers; j++) {
                    if (!workers[j].active){
						close(workers[j].pipe[0]);
						write(workers[j].pipe[1], internalQueue[i].command, strlen(internalQueue[i].command));
                        
						char temp[512] = "";
						sprintf(temp, "Task with priority level %d has been sent to worker %d", internalQueue[i].prio, workers[j].id);
						writeLog(temp);

                        strcpy(internalQueue[i].command, "");
                        internalQueue[i].prio = 3;

					}
                }
            }

			pthread_mutex_unlock(syncs->queue_mutex);
        }
    }
}

void *AlertsWatcher(){
	writeLog("Alerts Watcher iniciado.");
	while(1) {
		sem_wait(syncs->shm_sem);

		for(int i = 0; i < sizeof(sensores)/sizeof(sensores[0]); ++i) {
			if(!strcmp(sensores[i].id, "")) continue;

			for(int j = 0; j < sizeof(alerts)/sizeof(alerts[0]); ++j) {
				if((sensores[i].recentValue < alerts[j].min) || (sensores[i].recentValue > alerts[j].max)) {
					writeLog("Valor lido por sensor fora dos limites de alerta.");

					//mandar para a msg queue
					break;
				}
			}
			
		}

		sem_post(syncs->shm_sem);
	}
	
}

void Worker(int* pipe,int id){
	close(pipe[1]);

	char reader[MAX];
	read(pipe[0],reader,MAX);
	char* token = strtok(reader, "#");

	sem_wait(syncs->shm_sem);
	workers[id].active = 1;
	sem_post(syncs->shm_sem);

	sem_wait(syncs->shm_sem);

	if(!strcmp(token, "REMOVE_ALERT")) {
		
		token = strtok(NULL, "#");

		//Procurar e depois "eliminar"
        for (int i = 0; i < sizeof(alerts)/sizeof(alerts[0]); i++){
            if(!strcmp(alerts[i].id,token)) {
                strcpy(alerts[i].id, "");
				strcpy(alerts[i].chave, "");
                alerts[i].min = 0;
                alerts[i].max = 0;
            }
        }

		char temp[512] = "";
		sprintf(temp, "Worker %d removeu aletra.", id);
		writeLog(temp);

		//mandar "OK" pela msg queue
    }

	if(!strcmp(token, "LIST_ALERTS")) {
		char temp[1024] = "";
		strcat(temp, "ID	KEY		MIN		MAX\n");
		for(int i = 0; i < sizeof(alerts)/sizeof(alerts[0]); ++i) {
			if(!strcmp(alerts[i].id, "")) continue; 
			
			strcat(temp, alerts[i].id);
			strcat(temp, " ");
			strcat(temp, alerts[i].chave);
			strcat(temp, " ");
			strcat(temp, itoa(alerts[i].min));
			strcat(temp, " ");
			strcat(temp, itoa(alerts[i].max));
			strcat(temp, "\n");
		}

		char temp[512] = "";
		sprintf(temp, "Worker %d enviou lista de alertas.", id);
		writeLog(temp);
		//mandar para a msg queue
	}
	if(!strcmp(token, "STATS")){
		char temp[1024] = "";
		strcat(temp, "Key	Last	Min		Max		Avg		Count\n");
		for(int i = 0; i < sizeof(sensores)/sizeof(sensores[0]); i++) {
			if(!strcmp(sensores[i].id, "")) continue;

			strcat(temp, sensores[i].chave);
			strcat(temp, " ");
			strcat(temp, itoa(sensores[i].recentValue));
			strcat(temp, " ");
			strcat(temp, itoa(sensores[i].min));
			strcat(temp, " ");
			strcat(temp, itoa(sensores[i].max));
			strcat(temp, " ");
			strcat(temp, itoa(sensores[i].avg));
			strcat(temp, " ");
			strcat(temp, itoa(sensores[i].count));
			strcat(temp, "\n");
		}

		char temp[512] = "";
		sprintf(temp, "Worker %d enviou estatisticas de sensores.", id);
		writeLog(temp);

		//mandar para a msg queue
	}

	if(!strcmp(token, "RESET")) {
        for (int i = 0; i < sizeof(sensores)/sizeof(sensores[0]); i++){
            sensores[i].min=0;
            sensores[i].max=0;
			sensores[i].avg=0;
			sensores[i].recentValue=0;
			sensores[i].count=0;
			sensores[i].sum=0;
        }

		char temp[512] = "";
		sprintf(temp, "Worker %d limpou estatisticas de sensores.", id);
		writeLog(temp);

		//mandar para a msg queue
	}

	if(!strcmp(token, "SENSORS")) {
		char temp[1024] = "";
		strcat(temp, "ID\n");
		for (int i = 0; i < sizeof(sensores)/sizeof(sensores[0]); i++){
			if(!strcmp(sensores[i].id, "")) continue;
			
			strcat(temp, sensores[i].id);
			strcat(temp, "\n");
		}
		
		char temp[512] = "";
		sprintf(temp, "Worker %d enviou lista de sensores.", id);
		writeLog(temp);

		//mandar para a msg queue
	}
	if(!strcmp(token,"ADD_ALERT")){
		int aux = 0, size = sizeof(alerts)/(sizeof(alerts[0])), fill = 0;
		
		for(int i = 0; i < size; ++i) {
			fill++;
		}
		if(fill == size) {
			//mandar para a msg queue
			sem_post(syncs->shm_sem);
			return;
		}
		
		for(int i = 0; i < size; ++i) {
			
			if(!strcmp(alerts[i].id, token)) {
				aux++;
				break;
			}
		}
		if(aux > 0) {
			//mandar para a msg queue
			sem_post(syncs->shm_sem);
			return;
		}

		for(int i = 0; i < size; ++i) {
			
			if(!strcmp(alerts[i].id, "")) {
				token = strtok(NULL, "#");
				strcpy(alerts[i].id, token);
				token = strtok(NULL, "#");
				strcpy(alerts[i].chave,token);
				token = strtok(NULL, "#");
				alerts[i].min=atoi(token);
				token = strtok(NULL, "#");
				alerts[i].max=atoi(token);

				char temp[512] = "";
				sprintf(temp, "Worker %d adicionou alerta novo.", id);
				writeLog(temp);

				break;
			}
		}

		//mandar para a msg queue
		
	}

	for(int i = 0; i < sizeof(sensores)/sizeof(sensores[0]); ++i) {
		if(!strcmp(token, sensores[i].id)) {
			token = strtok(NULL, "#");
			if(!strcmp(token, sensores[i].chave)) {
				token = strtok(NULL, "#");
				int val = atoi(token);
				
				sensores[i].recentValue = val;
				if(val < sensores[i].min) sensores[i].min = val;
				if(val > sensores[i].max) sensores[i].max = val;
				sensores[i].count++;
				sensores[i].sum = sensores[i].sum + val;
				sensores[i].avg = sensores[i].sum/sensores[i].count;

				char temp[512] = "";
				sprintf(temp, "Worker %d atualizou sensor %s.", id, sensores[i].id);
				writeLog(temp);
				break;
			}
		}
	}

	workers[id].active = 0;
	sem_post(syncs->shm_sem);
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

void sigint(int signum){
	printf("\n");
	writeLog("Signal SIGINT recebido.");
	writeLog("Programa a terminar.");

    signal(SIGINT, SIG_IGN);

	//close names pipes fd and wait workers
	close(sensor_fd);
	unlink(PIPE_NAME_S);
	close(console_fd);
	unlink(PIPE_NAME_C);
	wait(NULL);
	
	//unlink sems and destroy mutexes
	sem_close(syncs->alert_watcher_sem);
	sem_unlink("ALERTS_W");
	sem_close(syncs->log);
	sem_unlink("LOG");
	sem_close(syncs->shm_sem);
	sem_unlink("SHM");
	pthread_mutex_destroy(syncs->queue_mutex);

	//exit threads
	for(int i = 0; i < sizeof(thrds)/sizeof(thrds[0]); ++i) {
		pthread_cancel(thrds[i]);
	}

	//detatch shm
	shmdt(sensores);
	shmctl(shmid_s, IPC_RMID, NULL);
	shmdt(alerts);
	shmctl(shmid_a, IPC_RMID, NULL);
	shmdt(workers);
	shmctl(shmid_w, IPC_RMID, NULL);

	//free resources
	free(syncs);
	free(cfg);
	free(sensores);
	free(workers);
	free(alerts);
	free(internalQueue);

	close(log_fp);
    exit(0);
}


int main(int argc, char* argv[]) {
	log_fp = fopen("logs.txt", "a");

	syncs = (sync*) malloc(sizeof(sync));
    sem_unlink("LOG");
	sem_unlink("ALERTS_W");
	sem_unlink("SHM");
    syncs->log = sem_open("LOG", O_CREAT|O_EXCL, 0777, 1);
	syncs->alert_watcher_sem = sem_open("ALERTS_W", O_CREAT|O_EXCL, 0777, 1);
	syncs->shm_sem = sem_open("SHM", O_CREAT|O_EXCL, 0777, 1);
	syncs->queue_mutex = PTHREAD_MUTEX_INITIALIZER;

    writeLog("Program Started");

	//Read the configuration file
    char *file_name = argv[1];
	cfg = readConfig(file_name);

	//Iniclializar arrays
    init();

	//Create shared memory and map it
	if((shmid_s = shmget(IPC_PRIVATE, sizeof(sensor)*cfg->Max_Sensors, IPC_CREAT|0777)) == -1)
		erro("Erro a criar shared memmory.");
	sensores = (sensor*)shmat(shmid_s, NULL, 0);

	if((shmid_a = shmget(IPC_PRIVATE, sizeof(alert)*cfg->Max_Alerts, IPC_CREAT|0777)) == -1)
		erro("Erro a criar shared memmory.");
	alerts = (alert*)shmat(shmid_a, NULL, 0);

	if((shmid_w = shmget(IPC_PRIVATE, sizeof(worker)*cfg->N_Workers, IPC_CREAT|0777)) == -1)
		erro("Erro a criar shared memmory.");
	workers = (worker*)shmat(shmid_w, NULL, 0);
	
	writeLog("Shared Memory created");

	//Create and open named pipes if they don't yet exist
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
	
	//Joins
	pthread_join(thrds[0], NULL);
	writeLog("SensorReader created");
	pthread_join(thrds[1], NULL);
	writeLog("ConsoleReader created");
	pthread_join(thrds[2], NULL);
	writeLog("Dispatcher created");

	//Inicializar processor worker e Alters Watcher
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

				char temp[512] = "";
				sprintf(temp, "Worker %d criado.", i+1);
				writeLog(temp);
			}
			else if(pids[1+i] == -1){
				printf("Error creating Worker process.\n");
				exit(1);

			}
			else{
				writeLog("Waiting for tasks to finish...");
				wait(NULL);
				writeLog("Programa a terminar.");
				sem_unlink("LOG");
				fclose(log_fp);
			}
		}
	}

	return 0;
}
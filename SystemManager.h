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

#define MAX 512

typedef struct config{
	int queue_size;
	int N_Workers;
	int Max_key;
	int Max_Sensors;
	int Max_Alerts;
} config;

typedef struct sync{
	sem_t* log;
	sem_t* alert_watcher_sem;
	sem_t* shm_sem;
	pthread_mutex_t* queue_mutex;
} sync;

typedef struct queue{
	char command[MAX];	//vou mudar
	int prio;			//2 = sensor|| 1= console
} queue;

typedef struct workers{
	int id;
	int active;			// 0 = livre | 1 = ocupado
	int pipe[2];
}worker;

typedef struct alert {
	char* id;
	char* chave;
	int min;
	int max;
} alert;

typedef struct sensor {
	char* id;
	char* chave;
	int recentValue;
	int min;
	int max;
	int avg;
	unsigned long int count;
	long int sum;
} sensor;

typedef struct messageQ {
  char msg[MAX];
} messageQ;
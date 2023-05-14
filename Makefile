all: Sensor UserConsole SystemManager

Sensor: Sensor.c
	gcc -Wall -Wextra Sensor.c -o Sensor

UserConsole: UserConsole.c
	gcc -Wall -Wextra UserConsole.c -o UserConsole

SystemManager: SystemManager.c
	gcc -pthread -lrt  SystemManager.c SystemManager.h -o SystemManager
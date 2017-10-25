TARGET  := monitor
CC      := gcc
CFLAGS  := -g -std=c99 -O0
WARN    := -Wall -Wextra -Wno-format -pedantic
OBJECTS := eventqueue.o b64.o utils.o thread.o fileops.o kafkaops.o monitor.o
LIBS	:= -lcrypto -lrdkafka -lpthread
#DEFS	:= -DDEBUG -DTHREAD_DEBUG
DEFS	:=

all:	monitor

monitor: ${OBJECTS}
	${CC} ${OBJECTS} -o monitor ${LIBS}

monitor.o: monitor.c
	${CC} ${CFLAGS} ${DEFS} ${WARN} -c monitor.c

eventqueue.o: eventqueue.c eventqueue.h
	${CC} ${CFLAGS} ${DEFS} ${WARN} -c eventqueue.c

fileops.o: fileops.c fileops.h
	${CC} ${CFLAGS} ${DEFS} ${WARN} -c fileops.c

kafkaops.o: kafkaops.c kafkaops.h
	${CC} ${CFLAGS} ${DEFS} ${WARN} -c kafkaops.c

b64.o: b64.c b64.h
	${CC} ${CFLAGS} ${DEFS} ${WARN} -c b64.c

utils.o: utils.c utils.h
	${CC} ${CFLAGS} ${DEFS} ${WARN} -c utils.c

thread.o: thread.c thread.h
	${CC} ${CFLAGS} ${DEFS} ${WARN} -c thread.c

clean:
	-rm *.o monitor

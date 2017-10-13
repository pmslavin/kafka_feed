TARGET  := monitor
CC      := gcc
CFLAGS  := -g -std=c99 -O0
WARN    := -Wall -Wextra -Wno-format -pedantic
OBJECTS := eventqueue.o b64.o fileops.o kafkaops.o monitor.o
LIBS	:= -lcrypto -lssl -lrdkafka

all:	monitor

monitor: ${OBJECTS}
	${CC} ${OBJECTS} -o monitor ${LIBS}

monitor.o: monitor.c
	${CC} ${CFLAGS} ${WARN} -c monitor.c

eventqueue.o: eventqueue.c eventqueue.h
	${CC} ${CFLAGS} ${WARN} -c eventqueue.c

fileops.o: fileops.c fileops.h
	${CC} ${CFLAGS} ${WARN} -c fileops.c

kafkaops.o: kafkaops.c kafkaops.h
	${CC} ${CFLAGS} ${WARN} -c kafkaops.c

b64.o: b64.c b64.h
	${CC} ${CFLAGS} ${WARN} -c b64.c

clean:
	-rm *.o monitor

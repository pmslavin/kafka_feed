TARGET  := monitor
CC      := gcc
CFLAGS  := -g -std=c99 -O0
WARN    := -Wall -Wextra -Wno-format -pedantic
OBJECTS := eventqueue.o fileops.o monitor.o
LIBS	:= -lcrypto -lssl

all:	monitor

monitor: ${OBJECTS}
	${CC} ${OBJECTS} -o monitor ${LIBS}

monitor.o: monitor.c
	${CC} ${CFLAGS} ${WARN} -c monitor.c

eventqueue.o: eventqueue.c eventqueue.h
	${CC} ${CFLAGS} ${WARN} -c eventqueue.c

fileops.o: fileops.c fileops.h
	${CC} ${CFLAGS} ${WARN} -c fileops.c

clean:
	-rm *.o monitor

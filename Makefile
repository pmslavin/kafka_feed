TARGET  := monitor
CC      := gcc
CFLAGS  := -g -std=c99 -O0
WARN    := -Wall -Wextra -Wno-format -pedantic
OBJECTS := monitor.o

all:	monitor

monitor: ${OBJECTS}
	${CC} ${OBJECTS} -o monitor ${LIBS}

monitor.o: monitor.c
	${CC} ${CFLAGS} ${WARN} -c monitor.c


clean:
	-rm *.o monitor

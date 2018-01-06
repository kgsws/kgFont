program=kfnedit
OBJ = main.o text.o
SRC = main.c text.c
LIBS = -lSDL -lGL -lGLU

OPT=-O2
#OPT=-g
CC=gcc
CFLAGS=${OPT}

build: ${program}

clean:
	rm -f *.o ${program}

${program}: ${OBJ}
	${CC} ${OBJ} ${LIBS} -o ${program} ${OPT}


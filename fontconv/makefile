program=font2kfn
OBJ = main.o
LIBS = -lm -lfreetype

OPT=-O2
#OPT=-g
CC=gcc
CFLAGS=${OPT} -I/usr/include/freetype2/

build: ${program}

clean:
	rm -f *.o ${program}
${program}: ${OBJ}
	${CC} ${OBJ} ${LIBS} -o ${program} ${OPT}


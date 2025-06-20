CC      = gcc
CFLAGS  =  -O3 -Wall -Wextra -Wpedantic -Wshadow -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wundef 
CFLAGS  += -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIC -fPIE -pie 
CFLAGS  += -fno-omit-frame-pointer 
PROGRAM=maketopologicf90
SRC=maketopologicf90.c

all: $(PROGRAM)

$(PROGRAM): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(PROGRAM)  
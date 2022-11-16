#############################################################################################
# Makefile
#############################################################################################
# G++ is part of GCC (GNU compiler collection) and is a compiler best suited for C++
CC=g++

# Compiler Flags: https://linux.die.net/man/1/g++
#############################################################################################
# -g: produces debugging information (for gdb)
# -l: searches the lib during linking
# -Wall: enables all the warnings
# -Wextra: further warnings
# -O: Optimizer turned on
# -std: c++ 17 is the latest stable version c++2a is the upcoming version
# -I: Add the directory dir to the list of directories to be searched for header files.
# -c: says not to run the linker
# -pthread: Add support for multithreading using the POSIX threads library. This option sets 
#           flags for both the preprocessor and linker. It does not affect the thread safety 
#           of object code produced by the compiler or that of libraries supplied with it. 
#           These are HP-UX specific flags.
#############################################################################################
CFLAGS=-Wall -Wextra -o -std=c++17 -pthread
LIBS=-lldap -llber

rebuild: clean all
all: ./bin/twmailer-server ./bin/twmailer-client

clean:
	clear
	rm -f bin/* obj/*

./obj/twmailerserver.o: TWMailerServer.cpp
	${CC} ${CFLAGS} -o obj/twmailerserver.o TWMailerServer.cpp -c

./bin/twmailer-server: ./obj/twmailerserver.o
	${CC} ${CFLAGS} -o bin/twmailer-server obj/twmailerserver.o ${LIBS}

./bin/twmailer-client: TWMailerClient.cpp
	${CC} ${CFLAGS} -o bin/twmailer-client TWMailerClient.cpp
# make rule primaria con dummy target 'all' --> non crea alcun file all ma fa un complete build
# 												che dipende dai target client e server scritti sotto
all: server td kd cli

# make rule server
server: server.o
			gcc -Wall server.o -o server

# make rule td
td: td.o
			gcc -Wall td.o -o td

# make rule kd
kd: kd.o
			gcc -Wall kd.o -o kd

# make rule cli
cli: cli.o
			gcc -Wall cli.o -o cli

# pulizia file compilazione (eseguito con ‘make clean’ da terminale)
clean:
			rm *o server td kd cli

OBJ =  main.o util.o 
all: SyncDaemon

SyncDaemon: $(OBJ)
	gcc $(OBJ) -o SyncDaemon
$(obj): util.h
.PHONY: clean
clean:
	rm -f *.o SyncDaemon
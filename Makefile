OBJ =  main.o util.o 
all: SyncDaemon

SyncDaemon: $(OBJ)
	gcc $(OBJ) -o SyncDaemon
$(OBJ): util.h
.PHONY: clean
clean:
	rm -f *.o SyncDaemon
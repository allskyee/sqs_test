CC=gcc
INC=-I/usr/include/libxml2/
LIBS=-lcurl -lxml2 

all : 
	$(CC) -o test main.c basic_io.c xml.c $(LIBS) $(INC)

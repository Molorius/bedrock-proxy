
CC=gcc
CFLAGS=-g -Wall -O3
LFLAGS=
LIBS=
SRCS=main.c proxy.c
TARGET=bedrock-proxy

OBJS=$(SRCS:.c=.o)

.PHONY: depend clean

all: $(TARGET)
	@echo Finished build of $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(OBJS) $(LFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	$(RM) *.o *~ $(TARGET)

depend: $(SRCS)
	makedepend $(INCLUDES) $^

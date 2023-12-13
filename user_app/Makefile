
CC := gcc
CFLAGS := -Wall -Wextra
LDFLAGS := -ldevmapper

TARGET = entanglement_app
SRCS = main.c
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
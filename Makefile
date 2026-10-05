CC = gcc
SRCS = $(wildcard **/*.c)
OBJS = $(SRCS:.c=.o)
MAIN = pl0
CFLAGS = -Wall
LDLIBS = -lm

.PHONY: depend clean

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN) $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(MAIN) $(MAIN).exe

run: $(MAIN)
	./$(MAIN) $(filter-out $@, $(MAKECMDGOALS))

%:
	@:

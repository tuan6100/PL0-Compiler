CC = gcc
CFLAGS = -Wall

SRCS = $(shell find src -name "*.c")
OBJS = $(SRCS:.c=.o)

MAIN = pl0

.PHONY: clean run

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(MAIN) $(MAIN).exe

run: $(MAIN)
	./$(MAIN) $(filter-out $@,$(MAKECMDGOALS))

%:
	@:
SOURCES = $(wildcard *.c)

OBJECTS = $(SOURCES:.c=.o)

DEPENDS = $(OBJECTS:.o=.d)

LDFLAGS = -fno-omit-frame-pointer -fstack-protector -fsanitize=address

LDLIBS =

PROG = riff

CFLAGS += -std=gnu11
CFLAGS += -Wall -Wextra -Wpointer-arith -Wconversion -Wshadow
CFLAGS += -Wnull-dereference -Wdouble-promotion
CFLAGS += -Wreturn-type -Wcast-align -Wcast-qual -Wuninitialized -Winit-self
CFLAGS += -Wformat=2 -Wformat-security -Wmissing-include-dirs
CFLAGS += -Wstrict-prototypes
CFLAGS += -ggdb -O0
CFLAGS += -Wpedantic -Wduplicated-cond -Wlogical-op

.PHONEY: all
all: $(PROG)

$(PROG): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

-include $(DEPENDS)
%.o: %.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

.PHONEY: clean
clean:
	$(RM) $(OBJECTS)
	$(RM) $(PROG)
	$(RM) $(DEPENDS)

SRC  = $(wildcard *.c)
OBJS = $(SRC:.c=.o)
DEPS = $(SRC:.c=.d)

CFLAGS := -O2 -fomit-frame-pointer -std=c99 -ggdb \
	-pedantic -Wall -Wextra -MMD -pipe
LDFLAGS := -lgawen -lpcap

TARGET=velad

ifdef VERBOSE
	Q :=
else
	Q := @
endif

.PHONY: all clean

%.o: %.c
	@echo "===> CC $<"
	$(Q)$(CC) -c $(CFLAGS) -o $@ $<

$(TARGET): $(OBJS)
	@echo "===> LD $@"
	$(Q)$(CC) $(OBJS) $(LDFLAGS) -o $@

clean:
	@echo "===> CLEAN"
	$(Q)rm -f *.o
	$(Q)rm -f *.d
	$(Q)rm -f $(TARGET)

install:
	@echo "===> Installing $(TARGET)"
	$(Q)install -s $(TARGET) /usr/local/sbin

-include $(DEPS)

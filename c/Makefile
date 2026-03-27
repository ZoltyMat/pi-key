CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -pthread -D_GNU_SOURCE
LDFLAGS = -lbluetooth -lcurl -lyaml -pthread -lm

SRCDIR  = src
OBJDIR  = obj
TARGET  = pikey

SRCS    = $(SRCDIR)/main.c \
          $(SRCDIR)/config.c \
          $(SRCDIR)/keymap.c \
          $(SRCDIR)/transport.c \
          $(SRCDIR)/jiggler.c \
          $(SRCDIR)/typer.c \
          $(SRCDIR)/llm_client.c \
          $(SRCDIR)/cjson/cJSON.c

OBJS    = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/pikey

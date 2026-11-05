CC ?= clang
CFLAGS ?= -O3 -Wall -Wextra -pedantic -std=c11 -pthread
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

SRC = src/main.c src/scanner.c src/utils.c src/progress.c src/cmd_ls.c
OBJ = $(SRC:.c=.o)
TARGET = bin/sizeof

.PHONY: all clean install uninstall test

all: $(TARGET) sizeof

$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

sizeof: $(TARGET)
	@cp $(TARGET) sizeof

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	@mkdir -p $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/sizeof
	@echo "Installed sizeof to $(BINDIR)/sizeof"

uninstall:
	rm -f $(BINDIR)/sizeof
	@echo "Uninstalled sizeof from $(BINDIR)/sizeof"

clean:
	rm -f src/*.o bin/sizeof sizeof
	rm -rf test_dir

test: $(TARGET)
	@bash tests/test_sizeof.sh

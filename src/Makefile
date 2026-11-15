# created by xdidend00

#defines for compilation
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic

PROG_NAME = ifj26_compiler

#defines for clang-format
CLANG_FORMAT = clang-format
FORMAT_STYLE = Microsoft
SOURCE_FILES_FOLDER = #nothing
HEADER_FILES_FOLDER = #nothing

FILES_TO_FORMAT := $(wildcard $(SOURCE_FILES_FOLDER)*.c $(HEADER_FILES_FOLDER)*.h)

.PHONY: fmt
fmt:
	@$(CLANG_FORMAT) -i --style=$(FORMAT_STYLE) $(FILES_TO_FORMAT)

.PHONY: fmt-check
fmt-check:
	@$(CLANG_FORMAT) --dry-run --Werror --style=$(FORMAT_STYLE) $(FILES_TO_FORMAT)

PROG_OBJECTS = main.o ast.o attribute_table.o bst.o error.o lexer.o parser.o stack.o symtable.o token.o utils.o #TODO: append other parts when done
.PHONY: build

build: $(PROG_OBJECTS)
	$(CC) $(FLAGS) $(PROG_OBJECTS) -o $(PROG_NAME)
	rm *.o

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

ast.o: ast.c
	$(CC) $(CFLAGS) -c $< -o $@

attribute_table.o: attribute_table.c
	$(CC) $(CFLAGS) -c $< -o $@

bst.o: bst.c
	$(CC) $(CFLAGS) -c $< -o $@

error.o: error.c
	$(CC) $(CFLAGS) -c $< -o $@

lexer.o: lexer.c
	$(CC) $(CFLAGS) -c $< -o $@

parser.o: parser.c
	$(CC) $(CFLAGS) -c $< -o $@

stack.o: stack.c
	$(CC) $(CFLAGS) -c $< -o $@

symtable.o: symtable.c
	$(CC) $(CFLAGS) -c $< -o $@

token.o: token.c
	$(CC) $(CFLAGS) -c $< -o $@

utils.o: utils.c
	$(CC) $(CFLAGS) -c $< -o $@
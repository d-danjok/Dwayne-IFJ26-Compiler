# created by xdidend00

#defines for compilation
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic

PROG_NAME = ifj26

#defines for clang-format
CLANG_FORMAT = clang-format
FORMAT_STYLE = Microsoft
SOURCE_FILES_FOLDER = #nothing
HEADER_FILES_FOLDER = #nothing

FILES_TO_FORMAT := $(wildcard $(SOURCE_FILES_FOLDER)*.c $(HEADER_FILES_FOLDER)*.h)

fmt:
	@$(CLANG_FORMAT) -i --style=$(FORMAT_STYLE) $(FILES_TO_FORMAT)

fmt-check:
	@$(CLANG_FORMAT) --dry-run --Werror --style=$(FORMAT_STYLE) $(FILES_TO_FORMAT)

PROG_COMPS = main.c ast.c attribute_table.c bst.c error.c lexer.c parser.c stack.c symtable.c utils.c semantic_analyser.c #TODO: append other parts when done

all: build

build: $(PROG_COMPS)
	$(CC) $(FLAGS) $(PROG_COMPS) -o $(PROG_NAME)

.PHONY: fmt fmt-check build
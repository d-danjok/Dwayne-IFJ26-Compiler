# created by xdidend00

#defines for compilation
CC = gcc

#TODO: implement actual build

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
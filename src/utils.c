// @file 	utils.c
//
// @brief 	implementation of utility functions
// @author 	Daniil Didenko (xdidend00)

#include "utils.h"

void *utils_safe_malloc(error *err, size_t size)
{
    *err = NONE;
    if (size == 0)
    {
        *err = INTERNAL_INVALID_MEMORY_ALLOCATION_SIZE;
        return NULL;
    }

    void *ptr = malloc(size);
    if (ptr == NULL)
    {
        *err = INTERNAL_MEMORY_ALLOCATION;
        return NULL;
    }

    memset(ptr, 0, size);
    return ptr;
}

void utils_safe_free(void **ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    if (*ptr == NULL)
    {
        return;
    }
    free(*ptr);
    *ptr = NULL;
}

char utils_get_char(error *err, FILE *stream)
{
    *err = NONE;
    int c = getc(stream);
    if (c == EOF)
    {
        *err = INTERNAL_END_OF_FILE;
        if (ferror(stream) != 0)
        {
            *err = INTERNAL_FILE_READ_ERROR;
        }
        return '\0';
    }
    return (char)c;
}

char utils_peek_char(error *err, FILE *stream, int offset)
{
    *err = NONE;

    if (offset < 0)
    {
        *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        return '\0';
    }

    long position = ftell(stream);
    if (position == -1L)
    {
        *err = INTERNAL_FILE_READ_ERROR;
        return '\0';
    }

    int c = getc(stream);
    for (int i = 0; i < offset && c != EOF; i++)
    {
        c = getc(stream);
    }

    if (c == EOF)
    {
        *err = ferror(stream) ? INTERNAL_FILE_READ_ERROR : INTERNAL_END_OF_FILE;
    }

    if (fseek(stream, position, SEEK_SET) != 0)
    {
        *err = INTERNAL_FILE_READ_ERROR;
        return '\0';
    }
    if (c == EOF)
    {
        return '\0';
    }
    return (char)c;
}

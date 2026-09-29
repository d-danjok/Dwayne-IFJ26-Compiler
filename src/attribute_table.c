// @file attribute_table.c
//
// @author 	Daniil Didenko 		(xdidend00)
//			Oleksandr Fedorenko (xfedoro00)
//
// @note    we worked using IDE's Code together feature,
// 			so on repository only one author is seen

#include "attribute_table.h"
#include <math.h>

#define INITIAL_SLICE_CAPACITY 100
#define FLOAT_EQUALITY_TOLERANCE 0.0001

// helper slices
typedef struct
{
    int *content;
    int len;
    int capacity;
} int_slice;

typedef struct
{
    float *content;
    int len;
    int capacity;
} float_slice;

typedef struct
{
    char **content;
    int len;
    int capacity;
} string_slice;

// constructors
int_slice *new_int_slice(error *err)
{
    int_slice *self = (int_slice *)utils_safe_malloc(err, sizeof(int_slice));
    if (*err != NONE)
    {
        return NULL;
    }
    self->len = 0;
    self->capacity = INITIAL_SLICE_CAPACITY;

    self->content = (int *)utils_safe_malloc(err, sizeof(int) * INITIAL_SLICE_CAPACITY);
    if (*err != NONE)
    {
        UTILS_SAFE_FREE(&self);
        return NULL;
    }

    return self;
}

float_slice *new_float_slice(error *err)
{
    float_slice *self = (float_slice *)utils_safe_malloc(err, sizeof(float_slice));
    if (*err != NONE)
    {
        return NULL;
    }
    self->len = 0;
    self->capacity = INITIAL_SLICE_CAPACITY;

    self->content = (float *)utils_safe_malloc(err, sizeof(float) * INITIAL_SLICE_CAPACITY);
    if (*err != NONE)
    {
        UTILS_SAFE_FREE(&self);
        return NULL;
    }

    return self;
}

string_slice *new_string_slice(error *err)
{
    string_slice *self = (string_slice *)utils_safe_malloc(err, sizeof(string_slice));
    if (*err != NONE)
    {
        return NULL;
    }
    self->len = 0;
    self->capacity = INITIAL_SLICE_CAPACITY;

    self->content = (char **)utils_safe_malloc(err, sizeof(char *) * INITIAL_SLICE_CAPACITY);
    if (*err != NONE)
    {
        UTILS_SAFE_FREE(&self);
        return NULL;
    }

    return self;
}

// insert (returns index where val was placed)
int int_slice_insert(int_slice *self, error *err, int val)
{
    *err = NONE;
    // check if is already in list
    for (int i = 0; i < self->len; i++)
    {
        if ((self->content)[i] == val)
        {
            return i;
        }
    }
    // done in this way in order to not do conversion
    if (self->capacity < 2 * (self->len + 1))
    {
        int *tmt_content = (int *)realloc(self->content, sizeof(int) * (self->capacity) * 2);
        if (tmt_content == NULL)
        {
            *err = INTERNAL_MEMORY_ALLOCATION;
            return -1;
        }
        self->content = tmt_content;
        self->capacity *= 2;
    }
    int insertion_index = self->len;
    (self->len)++;
    (self->content)[insertion_index] = val;
    return insertion_index;
}

int float_slice_insert(float_slice *self, error *err, float val)
{
    *err = NONE;
    // check if is already in list
    for (int i = 0; i < self->len; i++)
    {
        if (fabs((self->content)[i] - val) < FLOAT_EQUALITY_TOLERANCE)
        {
            return i;
        }
    }
    // done in this way in order to not do conversion
    if (self->capacity < 2 * (self->len + 1))
    {
        float *tmp_content = (float *)realloc(self->content, sizeof(float) * (self->capacity) * 2);
        if (tmp_content == NULL)
        {
            *err = INTERNAL_MEMORY_ALLOCATION;
            return -1;
        }
        self->content = tmp_content;
        self->capacity = self->capacity * 2;
    }

    int insertion_index = self->len;
    self->content[insertion_index] = val;
    self->len++;
    return insertion_index;
}

int string_slice_insert(string_slice *self, error *err, char *val)
{
    *err = NONE;
    // check if is already in list
    for (int i = 0; i < self->len; i++)
    {
        if (strcmp((self->content)[i], val) == 0)
        {
            return i;
        }
    }
    // done in this way in order to not do conversion
    if (self->capacity < 2 * (self->len + 1))
    {
        char **tmp_content = (char **)realloc(self->content, sizeof(char *) * (self->capacity) * 2);
        if (tmp_content == NULL)
        {
            *err = INTERNAL_MEMORY_ALLOCATION;
            return -1;
        }
        self->content = tmp_content;
        self->capacity = self->capacity * 2;
    }

    int insertion_index = self->len;
    self->content[insertion_index] = val;
    self->len++;
    return insertion_index;
}

void int_slice_destruct(int_slice *self)
{
    if (self == NULL)
    {
        return;
    }
    UTILS_SAFE_FREE(&(self->content));
    UTILS_SAFE_FREE(&self);
}

void float_slice_destruct(float_slice *self)
{
    if (self == NULL)
    {
        return;
    }

    UTILS_SAFE_FREE(&(self->content));
    UTILS_SAFE_FREE(&self);
}

void string_slice_destruct(string_slice *self)
{
    if (self == NULL)
    {
        return;
    }
    for (int i = 0; i < self->len; i++)
    {
        UTILS_SAFE_FREE(&(self->content[i]));
    }
    UTILS_SAFE_FREE(&(self->content));
    UTILS_SAFE_FREE(&self);
}

// attribute table itself

// Structure with all attribute tables
struct attribute_table_s
{
    int_slice *int_values;
    float_slice *float_values;
    string_slice *string_values;
    string_slice *identifiers;
};

attribute_table *new_attribute_table(error *err)
{
    attribute_table *table = (attribute_table *)utils_safe_malloc(err, sizeof(attribute_table));
    if (*err != NONE)
    {
        return NULL;
    }

    table->int_values = new_int_slice(err);
    if (*err != NONE)
    {
        free(table);
        return NULL;
    }

    table->float_values = new_float_slice(err);
    if (*err != NONE)
    {
        int_slice_destruct(table->int_values);
        free(table);
        return NULL;
    }

    table->string_values = new_string_slice(err);
    if (*err != NONE)
    {
        int_slice_destruct(table->int_values);
        float_slice_destruct(table->float_values);
        free(table);
        return NULL;
    }

    table->identifiers = new_string_slice(err);
    if (*err != NONE)
    {
        int_slice_destruct(table->int_values);
        float_slice_destruct(table->float_values);
        string_slice_destruct(table->string_values);
        free(table);
        return NULL;
    }

    return table;
}

void destruct_attribute_table(attribute_table **self)
{
    if (self == NULL || *self == NULL)
    {
        return;
    }

    int_slice_destruct((*self)->int_values);
    float_slice_destruct((*self)->float_values);
    string_slice_destruct((*self)->string_values);
    string_slice_destruct((*self)->identifiers);

    UTILS_SAFE_FREE(self);
}

int attribute_table_insert_integer(attribute_table *self, error *err, int val)
{
    return int_slice_insert(self->int_values, err, val);
}

int attribute_table_insert_float(attribute_table *self, error *err, float val)
{
    return float_slice_insert(self->float_values, err, val);
}

int attribute_table_insert_string(attribute_table *self, error *err, char *val)
{
    return string_slice_insert(self->string_values, err, val);
}

int attribute_table_insert_identifier(attribute_table *self, error *err, char *identifier)
{
    return string_slice_insert(self->identifiers, err, identifier);
}

int attribute_table_get_integer(attribute_table *self, error *err, int index)
{
    *err = NONE;
    if (index < 0 || index >= (self->int_values)->len)
    {
        *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        return 0;
    }
    return ((self->int_values)->content)[index];
}

float attribute_table_get_float(attribute_table *self, error *err, int index)
{
    *err = NONE;
    if (index < 0 || index >= (self->float_values)->len)
    {
        *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        return 0;
    }
    return ((self->float_values)->content)[index];
}

char *attribute_table_get_string(attribute_table *self, error *err, int index)
{
    *err = NONE;
    if (index < 0 || index >= (self->string_values)->len)
    {
        *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        return NULL;
    }
    return ((self->string_values)->content)[index];
}

char *attribute_table_get_identifier(attribute_table *self, error *err, int index)
{
    *err = NONE;
    if (index < 0 || index >= (self->identifiers)->len)
    {
        *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        return NULL;
    }
    return ((self->identifiers)->content)[index];
}

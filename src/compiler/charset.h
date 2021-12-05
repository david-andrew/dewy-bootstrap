#ifndef CHARSET_H
#define CHARSET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "object.h"

typedef struct
{
    uint32_t start;
    uint32_t stop;
} urange;

typedef struct
{
    urange* ranges;
    size_t size;
    size_t capacity;
} charset;

charset* new_charset();
charset charset_empty_struct();
charset* charset_get_new_anyset();
void charset_get_static_anyset(charset* anyset, urange* range);
charset* charset_get_endmarker();
obj* new_charset_obj(charset* s);
void charset_add_char(charset* s, uint32_t c);
void charset_add_range(charset* s, urange r);
void charset_add_range_unchecked(charset* s, urange r);
size_t charset_size(charset* s);
size_t charset_capacity(charset* s);
uint64_t charset_length(charset* s);
void charset_resize(charset* s, size_t new_size);
charset* charset_clone(charset* s);
int urange_compare(const void* a, const void* b);
void charset_rectify(charset* s);
void charset_sort(charset* s);
void charset_reduce(charset* s);
charset* charset_compliment(charset* s);
charset* charset_diff(charset* a, charset* b);
charset* charset_intersect(charset* a, charset* b);
charset* charset_union(charset* a, charset* b);
void charset_union_into(charset* a, charset* b, bool free_b);
bool charset_equals(charset* a, charset* b);
bool charset_is_anyset(charset* s);
bool charset_is_single_char(charset* s);
int charset_get_c_index(charset* s, uint32_t c);
bool charset_contains_c(charset* s, uint32_t c);
bool charset_contains_r(charset* s, urange r);
bool charset_contains_charset(charset* superset, charset* subset);
bool urange_contains_c(urange r, uint32_t c);
bool is_charset_escape(uint32_t c);
void charset_char_str(uint32_t c, bool single_char);
int charset_char_strlen(uint32_t c, bool single_char);
void charset_str(charset* s);
void charset_str_inner(charset* s);
int charset_strlen(charset* s);
void charset_repr(charset* s);
void charset_free(charset* s);
// int64_t charset_compare(charset* left, charset* right);
uint64_t charset_hash_lambda(void* seq, size_t i);
uint64_t charset_hash(charset* s);

#endif
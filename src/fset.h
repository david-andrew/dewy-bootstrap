#ifndef FSET_H
#define FSET_H

#include "charset.h"
#include "object.h"
#include "set.h"

typedef struct
{
    charset* terminals;
    bool special; // instead of representing ϵ/$ as an obj, keep a separate boolean
                  // indicates first sets that contain ϵ, and follow sets that contain $
} fset;

fset* new_fset();
obj* new_fset_obj(fset* s);
uint64_t fset_size(fset* s);
void fset_add(fset* s, charset* cs);
void fset_set_special(fset* s, bool special);
// fset* fset_union(fset* left, fset* right);
void fset_union_into(fset* left, fset* right, bool do_nullable, bool free_right);
void fset_free(fset* s);
fset* fset_copy(fset* s);
void fset_str(fset* s);
void fset_first_str(fset* s);
void fset_follow_str(fset* s);
// bool fset_contains_charset(fset* s, charset* cs);
bool fset_contains_c(fset* s, uint32_t c);

#endif
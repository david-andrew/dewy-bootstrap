#ifndef SET_C
#define SET_C

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "utilities.c"
#include "obj.c"
#include "dict.c"

typedef struct set_struct
{
    dict* d; //a set is just a wrapper around a dict
} set;

set* new_set();
size_t set_size(set* S);
size_t set_capacity(set* S);
bool set_add(set* S, obj* item);
// bool set_remove(set* s, obj* item);
bool set_contains(set* S, obj* item);
set* set_copy(set* S);
set* set_union(set* A, set* B);
set* set_intersect(set* A, set* B);
void set_reset(set* S);
void set_free(set* S);
void set_repr(set* S);
void set_str(set* S);


set* new_set()
{
    set* s = malloc(sizeof(set));
    s->d = new_dict(); //create the wrapped dictionary
    return s;
}

size_t set_size(set* S) 
{ 
    return dict_size(S->d); 
}

size_t set_capacity(set* S) 
{ 
    return dict_capacity(S->d); 
}

bool set_add(set* S, obj* item)
{
    return dict_set(S->d, item, item); //to store an item, use the item as the key
}

bool set_contains(set* S, obj* item)
{
    return dict_contains(S->d, item);
}

// set* set_copy(set* s)
// {
//     set* copy = malloc(sizeof(set));
//     s->d = dict_copy(s->d); //TODO->implement this dict function
//     return s;
// }

set* set_union(set* A, set* B)
{
    set* S = new_set();

    //This part could be replaced with `S = set_copy(A);`
    for (int i = 0; i < set_capacity(A); i++ )
    {
        dict_entry e = A->d->table[i];
        if (e.hash != 0 || e.key != NULL)
        {
            set_add(S, obj_copy(e.key)); //add a copy of the element to the union set
        }
    }

    for (int i = 0; i < set_capacity(B); i++)
    {
        dict_entry e = B->d->table[i];
        if (e.hash != 0 || e.key != NULL)
        {
            if (!set_contains(S, e.key)) //only add an element if it wasn't in the first list (so that we don't try to add duplicates)
            {
                set_add(S, obj_copy(e.key));
            }
        }
    }

    return S;
}

set* set_intersect(set* A, set* B)
{
    set* S = new_set();
    for (int i = 0; i < set_capacity(A); i++)
    {
        dict_entry e = A->d->table[i];
        if (e.hash != 0 || e.key != NULL)
        {
            if (set_contains(B, e.key)) //only if both A and B have the item
            {
                set_add(S, obj_copy(e.key));
            }
        }
    }
    return S;
}

void set_reset(set* s)
{
    dict_reset(s->d);
}

void set_free(set* s)
{
    dict_free(s->d);
}

void set_repr(set* s)
{
    //TODO
}

void set_str(set* s)
{
    //TODO
}


#endif
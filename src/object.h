#ifndef OBJECT_H
#define OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//// Object structs and typedefs ////

/**
 * Enum for each of the different types object types that exist
 */
typedef enum
{
    Boolean_t,
    Character_t,
    CharSet_t,
    Integer_t,
    UInteger_t,
    Pointer_t,
    String_t,
    UnicodeString_t,
    MetaToken_t,
    MetaAST_t,
    Slot_t,
    Slice_t,
    FSet_t,
    CRFClusterNode_t,
    CRFLabelNode_t,
    CRFActionHead_t,
    Descriptor_t,
    BSRHead_t,
    Vector_t,
    Dictionary_t,
    Set_t,
    ObjectTypesCount // count how many types of object there are
} obj_type;

/**
 * Struct/type declaration for generic objects
 */
typedef struct
{
    obj_type type; // integer specifying what type of object.
    void* data;    // data allocated for this object
} obj;

// forward declare so we can use dict here
typedef struct dict_struct dict;

//// Object Init Functions ////

// light weight primitive pointer allocations.
// can use instead of obj* if needed
bool* new_bool(bool b);
uint32_t* new_char(uint32_t c);
int64_t* new_int(int64_t i);
uint64_t* new_uint(uint64_t u);

// full obj* wrapped objects
obj* new_bool_obj(bool b);
obj* new_char_obj(uint32_t c);
obj* new_int_obj(int64_t i);
obj* new_uint_obj(uint64_t u);
obj* new_string_obj(char* s);      // new string object from an allocated string
obj* new_string_obj_copy(char* s); // new string object from a non-allocated string
obj* new_ptr_obj(void* ptr);

//// Utility Functions ////

obj obj_struct(obj_type type, void* data);
obj* new_obj(obj_type type, void* data);
obj* obj_copy(obj* o);
obj* obj_copy_with_refs(obj* o, dict* refs);
void obj_str(obj* o);
int obj_strlen(obj* o);
uint64_t obj_hash(obj* o);
int64_t obj_compare(obj* left, obj* right);
bool obj_equals(obj* left, obj* right);
void obj_free(obj* o);
void* obj_free_keep_inner(obj* o, obj_type type);

#endif
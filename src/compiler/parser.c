#ifndef PARSER_C
#define PARSER_C

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "charset.h"
#include "metaparser.h"
#include "parser.h"
#include "set.h"
#include "ustring.h"

/**
 * Global data structures used by the parser
 * Slots are a head ::= rule, with a dot starting the rule, or following a non-terminals)
 */
vect* parser_symbol_firsts;         // vect containing the first set of each symbol.
vect* parser_symbol_follows;        // vect containing the follow set of each symbol.
dict* parser_substring_firsts_dict; // dict<slice, fset> which memoizes the first set of a substring
vect* parser_labels;

/**
 * Allocate global data structures used by the parser
 */
void allocate_parser()
{
    parser_symbol_firsts = new_vect();
    parser_symbol_follows = new_vect();
    parser_substring_firsts_dict = new_dict();
    parser_labels = new_vect();
}

/**
 * Initialize any global data structures used by the parser.
 *
 * Note: this function should only be run after the complete_metaparser()
 * has been called on a successful metaparse of a grammar.
 */
void initialize_parser(/*uint64_t source_length*/)
{
    parser_compute_symbol_firsts();
    parser_compute_symbol_follows();
}

/**
 * Free any global data structures used by the parser.
 */
void release_parser()
{
    vect_free(parser_symbol_firsts);
    vect_free(parser_symbol_follows);
    dict_free(parser_substring_firsts_dict);
    vect_free(parser_labels);
    // other frees here
}

/**
 * Create a new parser context
 */
parser_context* new_parser_context(uint32_t* src, uint64_t len)
{
    parser_context* con = malloc(sizeof(parser_context));
    *con = (parser_context){
        .I = src,
        .m = len,
        .cI = 0,
        .cU = 0,
        .CRF = new_crf(),
        .P = new_set(),
        .Y = new_set(),
        .R = new_set(),
        .U = new_set(),
    };
    return con;
}

/**
 * Free a parser context
 */
void parser_context_free(parser_context* con)
{
    crf_free(con->CRF);
    set_free(con->P);
    set_free(con->Y);
    set_free(con->R);
    set_free(con->U);
    free(con);
}

/**
 * Parse a given source string.
 */
void parser_parse(parser_context* con)
{
    uint64_t start_symbol_idx = metaparser_get_start_symbol_idx();
    crf_cluster_node* u0 = crf_new_cluster_node(start_symbol_idx, 0);
    uint64_t node_idx = crf_add_cluster_node(con->CRF, u0);
    parser_nt_add(start_symbol_idx, 0, con);
}

/**
 * Generate the list of labels (slots) used by the CNP algorithm for the current grammar
 */
void parser_generate_labels()
{
    // iterate over the list of productions in the metaparser
    dict* productions = metaparser_get_productions();
    for (size_t i = 0; i < dict_size(productions); i++)
    {
        obj head_idx_obj;
        obj bodies_set_obj;
        dict_get_at_index(productions, i, &head_idx_obj, &bodies_set_obj);
        uint64_t head_idx = *(uint64_t*)head_idx_obj.data;
        set* bodies = (set*)bodies_set_obj.data;

        for (size_t body_idx = 0; body_idx < set_size(bodies); body_idx++)
        {
            vect* body = metaparser_get_production_body(head_idx, body_idx);

            slot s = slot_struct(head_idx, body_idx, 0);

            // add the initial item for the production to the list of labels
            vect_append(parser_labels, new_slot_obj(slot_copy(&s)));

            // iterate over the slot until the dot is at the end of the production
            for (s.dot = 1; s.dot <= vect_size(body); s.dot++)
            {
                uint64_t* symbol_idx = vect_get(body, s.dot - 1)->data;
                // if the symbol before the dot is a non-terminal, add a new slot
                if (!metaparser_is_symbol_terminal(*symbol_idx))
                {
                    vect_append(parser_labels, new_slot_obj(slot_copy(&s)));
                }
            }
        }
    }
}

/**
 * return the list of labels generated by the parser for the current grammar
 */
vect* parser_get_labels() { return parser_labels; }

/**
 * perform the CNP parsing actions for the given label
 */
void parser_handle_label(slot* label, parser_context* con)
{
    // keep track of the current dot in the item without modifying the original
    uint64_t dot = label->dot;

    vect* body = metaparser_get_production_body(label->head_idx, label->production_idx);
    if (label->dot == 0 && vect_size(body) == 0)
    {
        // Y.add((SubTerm(label.head, Sentence([])), cI, cI, cI))
    }
    else
    {
        while (dot < vect_size(body))
        {
            if (!metaparser_is_symbol_terminal(*(uint64_t*)vect_get(body, dot)->data)) { break; }
            if (dot != 0)
            {
                slice s = slice_struct(body, dot, vect_size(body), NULL);
                if (!parser_test_select(con->I[con->cI], label->head_idx, &s)) { return; }
            }
            dot++;

            parser_bsr_add(slot_copy(label), con->cU, con->cI, con->cI + 1);
            con->cI++;
        }

        if (dot < vect_size(body))
        {
            if (dot != 0)
            {
                slice s = slice_struct(body, dot, vect_size(body), NULL);
                if (!parser_test_select(con->I[con->cI], label->head_idx, &s)) { return; }
            }
            dot++;
            parser_call(slot_copy(label), con->cU, con->cI);
        }
    }

    if (label->dot == vect_size(body) ||
        (dot == vect_size(body) && metaparser_is_symbol_terminal(*(uint64_t*)vect_get(body, dot - 1)->data)))
    {
        // get the followset of the label head
        fset* follow = parser_follow_of_symbol(label->head_idx);
        if (fset_contains_c(follow, con->I[con->cI]))
        {
            parser_return(label->head_idx, con->cU, con->cI);
            return;
        }
    }
}

/**
 * print the CNP actions performed for the given label
 */
void parser_print_label(slot* label)
{
    slot_str(label);
    printf("\n");

    // keep track of the current dot in the item without modifying the original
    uint64_t dot = label->dot;

    vect* body = metaparser_get_production_body(label->head_idx, label->production_idx);
    if (label->dot == 0 && vect_size(body) == 0)
    {
        printf("    Y.add((SubTerm(label.head, Sentence([])), cI, cI, cI))\n");
    }
    else
    {
        while (dot < vect_size(body))
        {
            if (!metaparser_is_symbol_terminal(*(uint64_t*)vect_get(body, dot)->data)) { break; }
            if (dot != 0)
            {
                slice s = slice_struct(body, dot, vect_size(body), NULL);
                printf("    if (!parser_test_select(I[cI], ");
                obj_str(metaparser_get_symbol(label->head_idx));
                printf(", ");
                parser_print_body_slice(&s);
                printf("))\n        goto L0\n");
            }
            dot++;
            printf("    parser_bsr_add(");
            slot_str(&(slot){label->head_idx, label->production_idx, dot});
            printf(", cU, cI, cI + 1);\n    cI += 1\n");
        }

        if (dot < vect_size(body))
        {
            if (dot != 0)
            {
                slice s = slice_struct(body, dot, vect_size(body), NULL);
                printf("    if (!parser_test_select(I[cI], ");
                obj_str(metaparser_get_symbol(label->head_idx));
                printf(", ");
                parser_print_body_slice(&s);
                printf("))\n        goto L0\n");
            }
            dot++;
            printf("    parser_call(");
            slot_str(&(slot){label->head_idx, label->production_idx, dot});
            printf(", cU, cI);\n");
        }
    }

    if (label->dot == vect_size(body) ||
        (dot == vect_size(body) && metaparser_is_symbol_terminal(*(uint64_t*)vect_get(body, dot - 1)->data)))
    {
        printf("    if (I[cI] ∈ follow(");
        obj_str(metaparser_get_symbol(label->head_idx));
        printf("))\n        rtn(");
        obj_str(metaparser_get_symbol(label->head_idx));
        printf(", cU, cI);\n");
    }
    printf("    goto L0\n");
}

/**
 * TODO->what is this function for?
 */
void parser_nt_add(uint64_t head_idx, uint64_t j, parser_context* con)
{
    set* bodies = metaparser_get_production_bodies(head_idx);
    for (size_t body_idx = 0; body_idx < set_size(bodies); body_idx++)
    {
        uint64_t* production_idx = set_get_at_index(bodies, body_idx)->data;
        vect* body = metaparser_get_production_body(head_idx, body_idx);
        slice s = slice_struct(body, 0, vect_size(body), NULL);
        if (parser_test_select(con->I[j], head_idx, &s))
        {
            parser_dsc_add(&(slot){head_idx, *production_idx, 0}, j, j);
        }
    }
}

/**
 * TODO->what is this function for?
 */
bool parser_test_select(uint32_t c, uint64_t head_idx, slice* string)
{
    fset* first = parser_memo_first_of_string(string);
    if (fset_contains_c(first, c)) { return true; }
    else if (first->special)
    {
        fset* follow = parser_follow_of_symbol(head_idx);
        if (fset_contains_c(follow, c)) { return true; }
    }

    return false;
}

/**
 * TODO->what is this function for?
 * creates a copy of the slot if it is to be inserted. Original is not modified.
 */
void parser_dsc_add(slot* slot, uint64_t k, uint64_t j)
{
    // TODO->convert the slot to a slot_idx
    // create a static 3 tuple containing (slot, k, j)
    // if tuple not in U, add copy(tuple) to U and R
}

/**
 * TODO->what is this function for?
 */
void parser_return(uint64_t head_idx, uint64_t k, uint64_t j)
{
    // create static 3 tuple (head_idx, k, j)
    // check if tuple not in P
    // if not, add tuple to P
    //   and for each child v of (head_idx, k) in the CRF
    //     let (L, i) be the label of v
    //     dsc_add(L, i, j)
    //     bsr_add(L, i, k, j)
}

/**
 * TODO->what is this function for?
 */
void parser_call(slot* slot, uint64_t i, uint64_t j)
{
    // suppose that L is Y ::= αX · β
    // if there is no CRF node labelled (L, i) create one
    // let u be the CRF node labelled (L, i)
    // if there is no CRF node labelled (X, j) {
    //   create a CRF node v labelled (X, j)
    //   create an edge from v to u
    //   ntAdd(X, j) }
    // else { let v be the CRF node labelled (X, j)
    //   if there is not an edge from v to u {
    //     create an edge from v to u
    //     for all ((X, j, h) ∈ P) {
    //       dscAdd(L, i, h); bsrAdd(L, i, j, h) } } } }
}

/**
 * TODO->what is this function for?
 */
void parser_bsr_add(slot* slot, uint64_t i, uint64_t k, uint64_t j)
{
    vect* body = metaparser_get_production_body(slot->head_idx, slot->production_idx);
    if (vect_size(body) == slot->dot)
    {
        // insert (head_idx, production_idx, i, k, j) into Y
    }
    else if (slot->dot > 1)
    {
        // slice s = slice_struct(body, 0, slot->dot, NULL);
        // insert (s, i, k, j) into Y
    }
}

/**
 * Helper function to count the total number of elements in all first/follow sets
 *
 * fsets is either the array "metaparser_symbol_firsts" or "metaparser_symbol_follows"
 */
size_t parser_count_fsets_size(vect* fsets)
{
    size_t count = 0;
    for (size_t i = 0; i < vect_size(fsets); i++)
    {
        fset* s = vect_get(fsets, i)->data;
        count += set_size(s->terminals) + s->special;
    }
    return count;
}

/**
 * Compute all first sets for each symbol in the grammar
 */
void parser_compute_symbol_firsts()
{
    set* symbols = metaparser_get_symbols();

    // create empty fsets for each symbol in the grammar
    for (size_t i = 0; i < set_size(symbols); i++) { vect_append(parser_symbol_firsts, new_fset_obj(NULL)); }

    // compute firsts for all terminal symbols, since the fset is just the symbol itself.
    for (size_t symbol_idx = 0; symbol_idx < set_size(symbols); symbol_idx++)
    {
        if (!metaparser_is_symbol_terminal(symbol_idx)) { continue; }
        fset* symbol_fset = vect_get(parser_symbol_firsts, symbol_idx)->data;
        fset_add(symbol_fset, new_uint_obj(symbol_idx));
        symbol_fset->special = false;
    }

    // compute first for all non-terminal symbols. update each set until no new changes occur
    size_t count;
    do {
        // keep track of if any sets got larger (i.e. by adding new terminals to any fsets)
        count = parser_count_fsets_size(parser_symbol_firsts);

        // for each non-terminal symbol
        for (size_t symbol_idx = 0; symbol_idx < set_size(symbols); symbol_idx++)
        {
            if (metaparser_is_symbol_terminal(symbol_idx)) { continue; }

            fset* symbol_fset = vect_get(parser_symbol_firsts, symbol_idx)->data;
            set* bodies = metaparser_get_production_bodies(symbol_idx);
            for (size_t production_idx = 0; production_idx < set_size(bodies); production_idx++)
            {
                vect* body = metaparser_get_production_body(symbol_idx, production_idx);

                // for each element in body, get its fset, and merge into this one. stop if non-nullable
                for (size_t i = 0; i < vect_size(body); i++)
                {
                    uint64_t* body_symbol_idx = vect_get(body, i)->data;
                    fset* body_symbol_fset = vect_get(parser_symbol_firsts, *body_symbol_idx)->data;
                    fset_union_into(symbol_fset, fset_copy(body_symbol_fset), true);
                    if (!body_symbol_fset->special) { break; }
                }

                // epsilon strings add epsilon to fset
                if (vect_size(body) == 0) { symbol_fset->special = true; }
            }
        }
    } while (count < parser_count_fsets_size(parser_symbol_firsts));
}

/**
 * Compute all follow sets for each symbol in the grammar
 */
void parser_compute_symbol_follows()
{
    // steps for computing follow sets:
    // 1. place $ in FOLLOW(S) where S is the start symbol and $ is the input right endmarker
    // 2. If there is a production A -> αBβ, then everything in FIRST(β) except ϵ is in FOLLOW(B)
    // 3. if there is a production A -> αB, or a production A -> αBβ where FIRST(β) contains ϵ, then everything in
    //    FOLLOW(A) is in FOLLOW(B)

    set* symbols = metaparser_get_symbols();

    // first initialize fsets for each symbol in the grammar
    for (size_t i = 0; i < set_size(symbols); i++) { vect_append(parser_symbol_follows, new_fset_obj(NULL)); }

    // 1. add $ to the follow set of the start symbol
    uint64_t start_symbol_idx = metaparser_get_start_symbol_idx();
    ((fset*)vect_get(parser_symbol_follows, start_symbol_idx)->data)->special = true;

    // 2/3. add first of following substrings following terminals, and follow sets of rule heads
    dict* productions = metaparser_get_productions();
    size_t count;
    do {
        // keep track of if any sets got larger (i.e. by adding new terminals to any fsets)
        count = parser_count_fsets_size(parser_symbol_follows);

        for (size_t i = 0; i < dict_size(productions); i++)
        {
            obj head_idx_obj;
            obj bodies_set_obj;
            dict_get_at_index(productions, i, &head_idx_obj, &bodies_set_obj);
            uint64_t head_idx = *(uint64_t*)head_idx_obj.data;
            set* bodies = (set*)bodies_set_obj.data;

            // for each production body
            for (size_t body_idx = 0; body_idx < set_size(bodies); body_idx++)
            {
                vect* body = metaparser_get_production_body(head_idx, body_idx);

                // for each element in body, get its fset, and merge into this one. stop if non-nullable
                for (size_t i = 0; i < vect_size(body); i++)
                {
                    uint64_t* symbol_idx = vect_get(body, i)->data;

                    // create a substring beta of the body from i + 1 to the end, and compute its first set
                    slice beta = slice_struct(body, i + 1, vect_size(body), NULL);
                    fset* beta_first = parser_first_of_string(&beta);
                    bool nullable = beta_first->special; // save nullable status

                    // get union first of beta into the follow set of the symbol (ignoring epsilon)
                    fset* symbol_follow = vect_get(parser_symbol_follows, *symbol_idx)->data;
                    fset_union_into(symbol_follow, beta_first, false); // beta_first gets freed here

                    // if beta is nullable, add everything in follow set of head to follow set of the current terminal
                    if (nullable)
                    {
                        fset* head_follow = vect_get(parser_symbol_follows, head_idx)->data;
                        fset_union_into(symbol_follow, fset_copy(head_follow), true);
                    }
                }
            }
        }
    } while (count < parser_count_fsets_size(parser_symbol_follows));
}

/**
 * return the list of first sets for each symbol in the grammar.
 */
vect* parser_get_symbol_firsts() { return parser_symbol_firsts; }

/**
 * return the list of follow sets for each symbol in the grammar.
 */
vect* parser_get_symbol_follows() { return parser_symbol_follows; }

/**
 * return the first set for the given symbol
 */
fset* parser_first_of_symbol(uint64_t symbol_idx) { return vect_get(parser_symbol_firsts, symbol_idx)->data; }

/**
 * Compute the first set for the given string of symbols.
 *
 * symbol_first: vect<fset>
 * returned set needs to be freed when done.
 */
fset* parser_first_of_string(slice* string)
{
    fset* result = new_fset();
    vect* symbol_firsts = parser_get_symbol_firsts();

    if (slice_size(string) == 0)
    {
        // empty string is nullable
        result->special = true;
    }
    else
    {
        // handle each symbol in the string, until a non-nullable symbol is reached
        for (size_t i = 0; i < slice_size(string); i++)
        {
            uint64_t* symbol_idx = slice_get(string, i)->data;
            fset* first_i = vect_get(symbol_firsts, *symbol_idx)->data;
            bool nullable = first_i->special;
            fset_union_into(result, fset_copy(first_i),
                            false); // merge first of symbol into result. Don't merge nullable
            if (i == slice_size(string) - 1 && nullable) { result->special = true; }

            // only continue to next symbol if this symbol was nullable
            if (!nullable) { break; }
        }
    }

    return result;
}

/**
 * Memoized call to first of string. Returned fset is owned by the memoizer dict, and should not be freed.
 */
fset* parser_memo_first_of_string(slice* string)
{
    // check if the slice is in the dictionary already
    obj* result;
    if ((result = dict_get(parser_substring_firsts_dict, &(obj){.type = Slice_t, .data = string})) != NULL)
    {
        return result->data;
    }

    // otherwise, compute the first set and add it to the dictionary
    fset* result_fset = parser_first_of_string(string);
    dict_set(parser_substring_firsts_dict, new_slice_obj(slice_copy(string)), new_fset_obj(result_fset));

    return result_fset;
}

/**
 * return the follow set for the given symbol
 */
fset* parser_follow_of_symbol(uint64_t symbol_idx) { return vect_get(parser_symbol_follows, symbol_idx)->data; }

/**
 * print out the string of symbols in the given production body slice.
 */
void parser_print_body_slice(slice* body)
{
    if (slice_size(body) == 0) { printf("ϵ"); }
    for (size_t i = 0; i < slice_size(body); i++)
    {
        obj_str(metaparser_get_symbol(*(uint64_t*)(slice_get(body, i)->data)));
        if (i != slice_size(body) - 1) { printf(" "); }
    }
}

/**
 * print out the string of symbols for the given production body.
 */
void parser_print_body(vect* body)
{
    slice body_slice = slice_struct(body, 0, vect_size(body), NULL);
    parser_print_body_slice(&body_slice);
}

#endif
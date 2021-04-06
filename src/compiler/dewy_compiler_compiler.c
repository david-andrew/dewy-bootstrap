/**
 * How to use:
 * 
 * ./dewy [-s] [-a] [-p] [-g] [-t] [-c] [--verbose] /grammar/file/path /input/file/path
 * 
 * -s scanner
 * -a ast
 * -p parser
 * -g grammar itemsets (and first sets if --verbose)
 * -t grammar table
 * -c srnglr compiler
 * 
 * --verbose prints out repr instead of str
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "utilities.h"
#include "object.h"
#include "metatoken.h"
#include "metascanner.h"
#include "metaparser.h"
#include "metaitem.h"
#include "srnglr.h"
#include "dewy_compiler_compiler.h"

#define match_argv(var, val)                \
{                                           \
    var = false;                            \
    for (size_t i = 0; i < argc - 2; i++)   \
        if (strcmp(argv[i], #val) == 0)     \
            var = true;                     \
}


int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Error: you must specify a grammar file and a source file\n");
        printf("Usage: ./dewy [-s] [-a] [-p] [-g] [-t] [-c] [--verbose] /grammar/file/path /input/file/path");
        return 1;
    }

    //load the grammar source file into a string
    char* grammar_file_path = argv[argc-2];
    char* grammar_source;
    size_t grammar_size = read_file(grammar_file_path, &grammar_source);

    //load the input source file into a unicode string
    char* input_file_path = argv[argc-1];
    uint32_t* input_source;
    size_t input_size = read_unicode_file(input_file_path, &input_source);

    //determine what parts of the compile process to print out
    bool scanner, ast, parser, grammar, table, compile, forest, verbose;
    match_argv(scanner, -s)
    match_argv(ast, -a)
    match_argv(parser, -p)
    match_argv(grammar, -g)
    match_argv(table, -t)
    match_argv(compile, -c)
    match_argv(forest, -f)
    match_argv(verbose, --verbose)

    //if no sections specified, run all of them
    if (!(scanner || ast || parser || grammar || table || compile || forest))
    {
        scanner = ast = parser = grammar = table = compile = forest = true;
    }

    //set up structures for the sequence of scanning/parsing
    initialize_metascanner();
    initialize_metaparser();
    initialize_srnglr(input_size);

    run_compiler_compiler(grammar_source, verbose, scanner, ast, parser, grammar, table);
    run_compiler(input_source, compile, forest);

    free(grammar_source);
    free(input_source);

    release_metascanner();
    release_metaparser();
    release_srnglr();

    return 0;
}


/**
 * Run all steps in the compiler, and print out the intermediate results if the 
 * corresponding bool is true. If verbose is true, print out more structure info.
 */
void run_compiler_compiler(char* source, bool verbose, bool scanner, bool ast, bool parser, bool grammar, bool table)
{
    vect* tokens = new_vect();
    obj* t = NULL;

    //SCANNER STEP: collect all tokens from raw text
    while (*source != 0 && (t = scan(&source)) != NULL)
    {
        vect_push(tokens, t);
    }
    if (scanner) 
    {
        printf("METASCANNER OUTPUT:\n");
        print_scanner(tokens, verbose);
        printf("\n\n");
    }

    //AST & PARSER STEP: build ASTs from tokens, and then convert to CFG sentences
    if (ast) { printf("METAAST OUTPUT:\n"); }
    while (metatoken_get_next_real_token(tokens, 0) >= 0)
    {
        if (!metaparser_is_valid_rule(tokens)) { break; }

        obj* head = metaparser_get_rule_head(tokens);
        uint64_t head_idx = metaparser_add_symbol(head);
        vect* body_tokens = metaparser_get_rule_body(tokens);
        metaast* body_ast = metaast_parse_expr(body_tokens);
        if (ast) { print_ast(head_idx, body_ast, verbose); }

        //apply ast reductions if possible
        if (body_ast != NULL)
        {
            //count if any reductions were performed
            int reductions = 0;
            while ((metaast_fold_constant(&body_ast)) && ++reductions);

            if (ast && reductions > 0)
            {
                printf("Reduced AST: ");
                print_ast(head_idx, body_ast, verbose);
            }
        
            //attempt to convert metaast into sentential form
            metaparser_insert_rule_ast(head_idx, body_ast); 
        }

        //free up ast objects
        body_ast == NULL ? vect_free(body_tokens) : metaast_free(body_ast);
    }
    if (ast) { printf("\n\n"); }

    if (parser)
    {
        printf("METAPARSER OUTPUT:\n");
        print_parser(verbose);
        printf("\n\n");
    }

    //GRAMMAR ITEMSET STEP: generate the itemsets for the grammar
    srnglr_generate_grammar_itemsets();
    if (grammar)
    {
        printf("GRAMMAR OUTPUT:\n");
        print_grammar();
        printf("\n\n");
    }

    //SRNGLR TABLE: print out the generated srnglr table for the grammar
    if (table)
    {
        printf("SRNGLR TABLE:\n");
        print_table();
        printf("\n\n");
    }

    //print out any unparsed input and tokens
    if (*source != 0) 
    { 
        printf("UNSCANNED SOURCE:\n```\n%s\n```\n\n", source);
    }
    if (metatoken_get_next_real_token(tokens, 0) >= 0)
    {
        printf("UNPARSED TOKENS:\n");
        print_scanner(tokens, verbose);
        printf("\n\n");
    }

    vect_free(tokens);
}


/**
 * Parse the input file according to the input grammar.
 */
void run_compiler(uint32_t* source, bool compile, bool forest)
{

}


/**
 * Print the output of the scanner step
 */
void print_scanner(vect* tokens, bool verbose)
{
    for (size_t i = 0; i < vect_size(tokens); i++)
    {
        metatoken* t = vect_get(tokens, i)->data;
        verbose ? metatoken_repr(t) : metatoken_str(t);
        if (verbose && i < vect_size(tokens) - 1) { printf(" "); } //space after each verbose token
        if (t->type == comment && t->content[1] == '/') { printf("\n"); } //print a newline after singleline comments. TODO->maybe have single line comments include the newline?
    }
}


/**
 * Print the output of a single ast from the ast parse step.
 */
void print_ast(uint64_t head_idx, metaast* body_ast, bool verbose)
{
    obj* head = metaparser_get_symbol(head_idx);
    obj_str(head);
    if (body_ast != NULL)
    {
        printf(" = ");
        if (verbose) { metaast_repr(body_ast); } 
        else { metaast_str(body_ast); printf("\n"); }
    }
    else { printf(" = NULL\n"); }
}


/**
 * Print the output of the CFG covnersion step
 */
void print_parser(bool verbose)
{
    verbose ? metaparser_productions_repr() : metaparser_productions_str();
}


/**
 * Print out the itemsets generated by the grammar.
 */
void print_grammar()
{

    printf("first sets:\n"); 
    srnglr_print_firsts();
    
    printf("itemsets:\n");
    srnglr_print_itemsets();
}


/**
 * Print out the srnglr table generated by the grammar.
 */
void print_table()
{
    srnglr_print_table();
}
#ifndef DEWY_COMPILER_COMPILER_H
#define DEWY_COMPILER_COMPILER_H

#include <stdint.h>

#include "metaast.h"
#include "set.h"
#include "vector.h"

bool run_compiler_compiler(char* source, bool verbose, bool scanner, bool mast, bool grammar);
bool run_compiler(uint32_t* source, size_t length, bool fsets, bool labels, bool input, bool crf, bool descriptors,
                  bool bsr, bool result, bool ast, bool verbose);
void print_scanner(vect* tokens, bool verbose);
void print_ast(uint64_t head_idx, metaast* body_ast, bool verbose);
void print_parser(bool verbose);
void print_grammar_first_sets();
void print_grammar_follow_sets();

#endif
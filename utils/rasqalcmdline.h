/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rasqalcmdline.h - Rasqal command line utility functions
 *
 * Copyright (C) 2013, David Beckett http://www.dajobe.org/
 *
 * This package is Free Software and part of Redland http://librdf.org/
 *
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 *
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 *
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 *
 */

#ifndef RASQAL_CMDLINE_H
#define RASQAL_CMDLINE_H

typedef struct compare_query_results_t compare_query_results;

/* compare_results.c */
compare_query_results* new_compare_query_results(rasqal_world* world, rasqal_query_results* qr1, const char* qr1_label, rasqal_query_results* qr2, const char* qr2_label);
void free_compare_query_results(compare_query_results* cqr);
void compare_query_results_set_log_handler(compare_query_results* cqr, void* log_user_data,raptor_log_handler log_handler);
int compare_query_results_compare(compare_query_results* cqr);

/* read_files.c */
unsigned char* rasqal_cmdline_read_file_fh(const char* program, FILE* fh, const char* filename, const char* label, size_t* len_p);

unsigned char* rasqal_cmdline_read_file_string(const char* program, const char* filename,  const char* label, size_t* len_p);

unsigned char* rasqal_cmdline_read_uri_file_stdin_contents(const char* program, raptor_world* world, raptor_uri* uri, const char* filename, size_t* len_p);

/* results.c */
rasqal_query_results* rasqal_cmdline_read_results(rasqal_world* world, raptor_world* raptor_world_ptr, rasqal_query_results_type results_type, raptor_iostream* result_iostr, const char* result_filename, const char* result_format_name);

void rasqal_cmdline_print_bindings_results_simple(const char* program, rasqal_query_results *results, FILE* output, int quiet, int count);

#endif

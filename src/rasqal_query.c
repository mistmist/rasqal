/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rasqal_query.c - Rasqal RDF Query
 *
 * $Id$
 *
 * Copyright (C) 2003 David Beckett - http://purl.org/net/dajobe/
 * Institute for Learning and Research Technology - http://www.ilrt.org/
 * University of Bristol - http://www.bristol.ac.uk/
 * 
 * This package is Free Software or Open Source available under the
 * following licenses (these are alternatives):
 *   1. GNU Lesser General Public License (LGPL)
 *   2. GNU General Public License (GPL)
 *   3. Mozilla Public License (MPL)
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * full license terms.
 * 
 * 
 */

#ifdef HAVE_CONFIG_H
#include <rasqal_config.h>
#endif

#ifdef WIN32
#include <win32_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rasqal.h>
#include <rasqal_internal.h>



/* Constructor */
rasqal_query*
rasqal_new_query(const char *name, const unsigned char *uri) {
  rasqal_query* q=(rasqal_query*)malloc(sizeof(rasqal_query));

  /* name "rdql" uri "http://jena.hpl.hp.com/2003/07/query/RDQL" */
  return q;
}

/* Destructor */
void
rasqal_free_query(rasqal_query* query) {
  if(query->selects)
    rasqal_free_sequence(query->selects);
  if(query->sources)
    rasqal_free_sequence(query->sources);
  if(query->triples)
    rasqal_free_sequence(query->triples);
  if(query->prefixes)
    rasqal_free_sequence(query->prefixes);
}


/* Methods */
void
rasqal_query_add_source(rasqal_query* query, const unsigned char* uri) {
  rasqal_sequence_shift(query->sources, (void*)uri);
}

rasqal_sequence*
rasqal_query_get_source_sequence(rasqal_query* query) {
  return query->sources;
}

const unsigned char *
rasqal_query_get_source(rasqal_query* query, int idx) {
  return rasqal_sequence_get_at(query->sources, idx);
}


int
rasqal_parse_query(rasqal_query *query, const char *query_string) {
  return rdql_parse(query, query_string);
}



static void
rasqal_print_string(char *data, FILE *fh) 
{
  fputs(data, fh);
}


/* Utility methods */
void
rasqal_query_print(rasqal_query* query, FILE *fh) {
  fprintf(fh, "selects: ");
  rasqal_sequence_print(query->selects, fh, 
                        (rasqal_print_handler)rasqal_print_variable);
  fprintf(fh, "\nsources: ");
  rasqal_sequence_print(query->sources, fh, 
                        (rasqal_print_handler)rasqal_print_string);
  fprintf(fh, "\ntriples: ");
  rasqal_sequence_print(query->triples, fh, 
                        (rasqal_print_handler)rasqal_print_triple);
  fprintf(fh, "\nconstraints: ");
  rasqal_sequence_print(query->constraints, fh, 
                        (rasqal_print_handler)rasqal_print_expression);
  fprintf(fh, "\nprefixes: ");
  rasqal_sequence_print(query->prefixes, fh, 
                        (rasqal_print_handler)rasqal_print_prefix);
  fputc('\n', fh);
}


#ifdef STANDALONE
#include <stdio.h>
#include <locale.h>


extern char *filename;
extern int lineno;
char *program;

int
rdql_parser_error(const char *msg) 
{
  fprintf(stderr, "%s: Query parsing error: %s\n", program, msg);
  return (0);
}


int
main(int argc, char *argv[]) 
{
  rasqal_query *rq;
  char *ql_name;
  char *ql_uri;
  int rc=0;

  program=argv[0];
  
  if(argc != 4) {
    fprintf(stderr, "USAGE: %s QUERY-LANGUAGE|- QUERY-URI|- QUERY-STRING\n", 
            program);
    exit(1);
  }

  ql_name=!strcmp(argv[1], "-") ? NULL : argv[1];
  ql_uri=!strcmp(argv[1], "-") ? NULL : argv[2];

  rq=rasqal_new_query(ql_name, ql_uri);
  if(rasqal_parse_query(rq, argv[3])) {
    fprintf(stderr, "%s: Parsing query '%s' failed\n", program, argv[3]);
    rc=1;
  }

  fprintf(stdout, "Query:\n");
  rasqal_query_print(rq, stdout);

  rasqal_free_query(rq);

  return (rc);
}
#endif

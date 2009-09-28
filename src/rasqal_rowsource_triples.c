/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rasqal_rowsource_triples.c - Rasqal triple pattern rowsource class
 *
 * Copyright (C) 2008, David Beckett http://www.dajobe.org/
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


#ifdef HAVE_CONFIG_H
#include <rasqal_config.h>
#endif

#ifdef WIN32
#include <win32_rasqal_config.h>
#endif

#include <stdio.h>
#include <string.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <raptor.h>

#include "rasqal.h"
#include "rasqal_internal.h"


#ifndef STANDALONE

typedef struct 
{
  /* source of triple pattern matches */
  rasqal_triples_source* triples_source;

  /* sequence of triple SHARED with query */
  raptor_sequence* triples;

  /* current column being iterated */
  int column;

  /* first triple pattern in sequence to use */
  int start_column;

  /* last triple pattern in sequence to use */
  int end_column;

  /* number of triple patterns in the sequence
     ( = end_column - start_column + 1) */
  int triples_count;
  
  /* An array of items, one per triple pattern in the sequence */
  rasqal_triple_meta* triple_meta;

  /* offset into results for current row */
  int offset;
  
  /* number of variables used in variables table  */
  int size;
  
  /* bound-in array index[index into vars table] = column */
  int *bound_in;

  /* number of variables in bound_in[] array - may be more than 'size' */
  int bound_in_size;
  
  /* preserve bindings when all rows are finished - for optional mostly */
  int preserve_on_all_finished;

  /* GRAPH origin to use */
  rasqal_literal *origin;
} rasqal_triples_rowsource_context;


static int
rasqal_triples_rowsource_init(rasqal_rowsource* rowsource, void *user_data)
{
  rasqal_triples_rowsource_context *con;
  int column;
  int *bound_in = NULL;
  int rc = 0;
  int size;
  int i;
  
  con = (rasqal_triples_rowsource_context*)user_data;

  bound_in = con->bound_in;
  size = con->bound_in_size;
  
  /* Construct the ordered projection of the variables set by these triples */
  con->size = 0;
  for(i = 0; i < size; i++) {
    column = bound_in[i];
    if(column >= con->start_column && column <= con->end_column) {
      rasqal_variable *v;
      v = rasqal_variables_table_get(rowsource->vars_table, i);
      raptor_sequence_push(rowsource->variables_sequence, v);
      con->size++;
    }
  }

  con->column = con->start_column;

  for(column = con->start_column; column <= con->end_column; column++) {
    rasqal_triple_meta *m;
    rasqal_triple *t;
    rasqal_variable* v;

    m = &con->triple_meta[column - con->start_column];

    m->parts = (rasqal_triple_parts)0;

    t = (rasqal_triple*)raptor_sequence_get_at(con->triples, column);
    
    if((v = rasqal_literal_as_variable(t->subject)) &&
       bound_in[v->offset] == column)
      m->parts = (rasqal_triple_parts)(m->parts | RASQAL_TRIPLE_SUBJECT);
    
    if((v = rasqal_literal_as_variable(t->predicate)) &&
       bound_in[v->offset] == column)
      m->parts = (rasqal_triple_parts)(m->parts | RASQAL_TRIPLE_PREDICATE);
    
    if((v = rasqal_literal_as_variable(t->object)) &&
       bound_in[v->offset] == column)
      m->parts = (rasqal_triple_parts)(m->parts | RASQAL_TRIPLE_OBJECT);

    RASQAL_DEBUG4("triple pattern column %d has parts %s (%d)\n", column,
                  rasqal_engine_get_parts_string(m->parts), m->parts);

  }
  
  return rc;
}


static int
rasqal_triples_rowsource_ensure_variables(rasqal_rowsource* rowsource,
                                          void *user_data)
{
  rasqal_triples_rowsource_context* con;
  con = (rasqal_triples_rowsource_context*)user_data; 

  rowsource->size = con->size;
  
  return 0;
}


static int
rasqal_triples_rowsource_finish(rasqal_rowsource* rowsource, void *user_data)
{
  rasqal_triples_rowsource_context *con;
  int i;
  
  con = (rasqal_triples_rowsource_context*)user_data;

  for(i = con->start_column; i <= con->end_column; i++) {
    rasqal_triple_meta *m;
    m = &con->triple_meta[i - con->start_column];
    rasqal_reset_triple_meta(m);
  }
  
  if(con->triple_meta)
    RASQAL_FREE(rasqal_triple_meta, con->triple_meta);

  if(con->bound_in)
    RASQAL_FREE(rasqal_bound_in, con->bound_in);

  if(con->origin)
    rasqal_free_literal(con->origin);

  RASQAL_FREE(rasqal_triples_rowsource_context, con);

  return 0;
}


static rasqal_engine_error
rasqal_triples_rowsource_get_next_row(rasqal_rowsource* rowsource, 
                                      rasqal_triples_rowsource_context *con)
{
  rasqal_query *query = rowsource->query;
  rasqal_engine_error error = RASQAL_ENGINE_OK;
  
  while(con->column >= con->start_column) {
    rasqal_triple_meta *m;
    rasqal_triple *t;

    m = &con->triple_meta[con->column - con->start_column];
    t = (rasqal_triple*)raptor_sequence_get_at(con->triples, con->column);

    error = RASQAL_ENGINE_OK;

    if(!m->triples_match) {
      /* Column has no triples match so create a new query */
      m->triples_match = rasqal_new_triples_match(query,
                                                  con->triples_source,
                                                  m, t);
      if(!m->triples_match) {
        /* triples matching setup failed - matching state is unknown */
        RASQAL_DEBUG2("Failed to make a triple match for column %d\n",
                      con->column);
        error = RASQAL_ENGINE_FAILED;
        break;
      }
      RASQAL_DEBUG2("made new triples match for column %d\n", con->column);
    }


    if(rasqal_triples_match_is_end(m->triples_match)) {
      RASQAL_DEBUG2("end of pattern triples match for column %d\n",
                    con->column);

      if(con->preserve_on_all_finished && 
         con->column == con->end_column) {
        int is_finished = 1;
        int i;
          
        RASQAL_DEBUG1("CHECKING ALL TRIPLE PATTERNS FINISHED\n");

        for(i = con->start_column; i <= con->end_column; i++) {
          rasqal_triple_meta *m2;
          m2 = &con->triple_meta[con->column - con->start_column];
          if(!rasqal_triples_match_is_end(m2->triples_match)) {
            is_finished = 0;
            break;
          }
        }
        if(is_finished) {
          RASQAL_DEBUG1("end of all pattern triples matches\n");
          error = RASQAL_ENGINE_FINISHED;
          break;
        }
      }

      /* reset this column and move to next match in previous column */
      rasqal_reset_triple_meta(m);
      con->column--;
      if(con->column < con->start_column) {
        error = RASQAL_ENGINE_FINISHED;
        break;
      }
      continue;
    }

    if(m->parts) {
      rasqal_triple_parts parts;
      parts = rasqal_triples_match_bind_match(m->triples_match, m->bindings,
                                              m->parts);
      RASQAL_DEBUG4("bind_match for column %d returned parts %s (%d)\n",
                    con->column, rasqal_engine_get_parts_string(parts), parts);
      if(!parts) {
        error = RASQAL_ENGINE_FINISHED;
        break;
      }
    } else {
      RASQAL_DEBUG2("Nothing to bind_match for column %d\n", con->column);
    }

    rasqal_triples_match_next_match(m->triples_match);
    
    if(con->column == con->end_column)
      /* finished matching all columns - return result */
      break;

    /* continue matching in next column */
    con->column++;
  }

  return error;
}


static rasqal_row*
rasqal_triples_rowsource_read_row(rasqal_rowsource* rowsource, void *user_data)
{
  rasqal_triples_rowsource_context *con;
  int i;
  rasqal_row* row = NULL;
  rasqal_engine_error error = RASQAL_ENGINE_OK;
  
  con = (rasqal_triples_rowsource_context*)user_data;

  error = rasqal_triples_rowsource_get_next_row(rowsource, con);
  RASQAL_DEBUG2("rasqal_triples_rowsource_get_next_row() returned error %s\n",
                rasqal_engine_error_as_string(error));

  if(error != RASQAL_ENGINE_OK)
    goto done;

#ifdef RASQAL_DEBUG
  if(1) {
    int values_returned = 0;
    /* Count actual bound values */
    for(i = 0; i < con->size; i++) {
      rasqal_variable* v;
      v = rasqal_rowsource_get_variable_by_offset(rowsource, i);
      if(v->value)
        values_returned++;
    }
    RASQAL_DEBUG2("Solution binds %d values\n", values_returned);
  }
#endif

  row = rasqal_new_row(rowsource);
  if(!row) {
    error = RASQAL_ENGINE_FAILED;
    goto done;
  }

  for(i = 0; i < row->size; i++) {
    rasqal_variable* v;
    v = rasqal_rowsource_get_variable_by_offset(rowsource, i);
    if(row->values[i])
      rasqal_free_literal(row->values[i]);
    row->values[i] = rasqal_new_literal_from_literal(v->value);
  }

  row->offset = con->offset++;

  done:
  if(error != RASQAL_ENGINE_OK) {
    if(row) {
      rasqal_free_row(row);
      row = NULL;
    }
  }

  return row;
}


static raptor_sequence*
rasqal_triples_rowsource_read_all_rows(rasqal_rowsource* rowsource,
                                       void *user_data)
{
  rasqal_triples_rowsource_context *con;
  raptor_sequence *seq = NULL;
  
  con = (rasqal_triples_rowsource_context*)user_data;

  return seq;
}


static int
rasqal_triples_rowsource_reset(rasqal_rowsource* rowsource, void *user_data)
{
  rasqal_triples_rowsource_context *con;
  int column;
  
  con = (rasqal_triples_rowsource_context*)user_data;

  con->column = con->start_column;
  for(column = con->start_column; column <= con->end_column; column++) {
    rasqal_triple_meta *m;
    
    m = &con->triple_meta[column - con->start_column];
    rasqal_reset_triple_meta(m);
  }

  return 0;
}


static int
rasqal_triples_rowsource_set_preserve(rasqal_rowsource* rowsource,
                                      void *user_data, int preserve)
{
  rasqal_triples_rowsource_context *con;

  con = (rasqal_triples_rowsource_context*)user_data;
  con->preserve_on_all_finished = preserve;

  return 0;
}


static int
rasqal_triples_rowsource_set_origin(rasqal_rowsource *rowsource,
                                    void *user_data,
                                    rasqal_literal *origin)
{
  rasqal_triples_rowsource_context *con;
  int column;

  con = (rasqal_triples_rowsource_context*)user_data;
  if(con->origin)
    rasqal_free_literal(con->origin);
  con->origin = rasqal_new_literal_from_literal(origin);

  for(column = con->start_column; column <= con->end_column; column++) {
    rasqal_triple *t;
    t = (rasqal_triple*)raptor_sequence_get_at(con->triples, column);
    if(t->origin)
      rasqal_free_literal(t->origin);
    t->origin = rasqal_new_literal_from_literal(con->origin);
  }
  
  return 0;
}


static const rasqal_rowsource_handler rasqal_triples_rowsource_handler = {
  /* .version = */ 1,
  "triple pattern",
  /* .init = */ rasqal_triples_rowsource_init,
  /* .finish = */ rasqal_triples_rowsource_finish,
  /* .ensure_variables = */ rasqal_triples_rowsource_ensure_variables,
  /* .read_row = */ rasqal_triples_rowsource_read_row,
  /* .read_all_rows = */ rasqal_triples_rowsource_read_all_rows,
  /* .reset = */ rasqal_triples_rowsource_reset,
  /* .set_preserve = */ rasqal_triples_rowsource_set_preserve,
  /* .get_inner_rowsource = */ NULL,
  /* .set_origin = */ rasqal_triples_rowsource_set_origin
};


/*
 * rasqal_new_triples_rowsource:
 * @world: world object
 * @query: query object
 * @triples_source: shared triples source
 * @triples: shared triples sequence
 * @start_column: start column in triples sequence
 * @end_column: end column in triples sequence
 * @bound_in: array marking the triples that bind a variable
 *
 * INTERNAL - create a new triples rowsource
 *
 * NOTE @bound_in becomes owned by the new rowsource
 *
 * Return value: new triples rowsource or NULL on failure
 */
rasqal_rowsource*
rasqal_new_triples_rowsource(rasqal_world *world,
                             rasqal_query *query,
                             rasqal_triples_source* triples_source,
                             raptor_sequence* triples,
                             int start_column, int end_column,
                             int *bound_in, int bound_in_size)
{
  rasqal_triples_rowsource_context *con;
  int flags = 0;

  if(!world || !query || !triples_source || !triples) {
    RASQAL_FREE(rasqal_bound_in, bound_in);
    return NULL;
  }
  
  con = (rasqal_triples_rowsource_context*)RASQAL_CALLOC(rasqal_triples_rowsource_context, 1, sizeof(rasqal_triples_rowsource_context));
  if(!con) {
    RASQAL_FREE(rasqal_bound_in, bound_in);
    return NULL;
  }

  con->triples_source = triples_source;
  con->triples = triples;
  con->start_column = start_column;
  con->end_column = end_column;
  con->column = -1;
  con->bound_in = bound_in;
  con->bound_in_size = bound_in_size;

  con->triples_count = con->end_column - con->start_column + 1;

  con->triple_meta = (rasqal_triple_meta*)RASQAL_CALLOC(rasqal_triple_meta,
                                                        con->triples_count,
                                                        sizeof(rasqal_triple_meta));
  if(!con->triple_meta) {
    rasqal_triples_rowsource_finish(NULL, con);
    return NULL;
  }

  return rasqal_new_rowsource_from_handler(world, query,
                                           con,
                                           &rasqal_triples_rowsource_handler,
                                           query->vars_table,
                                           flags);
}


#endif



#ifdef STANDALONE

/* one more prototype */
int main(int argc, char *argv[]);

#define QUERY_LANGUAGE "sparql"
#define QUERY_FORMAT "\
SELECT ?s ?p ?o \
FROM <%s> \
WHERE { ?s ?p ?o }\
"

int
main(int argc, char *argv[]) 
{
  const char *program = rasqal_basename(argv[0]);
  rasqal_rowsource *rowsource = NULL;
  rasqal_world *world;
  rasqal_query *query;
  const char *query_language_name = QUERY_LANGUAGE;
  const char *query_format = QUERY_FORMAT;
  unsigned char *query_string;
  int failures = 0;
  int start_column;
  int end_column;
  int rc;
  raptor_sequence* triples;
  rasqal_triples_source* triples_source;
  raptor_uri *base_uri = NULL;
  unsigned char *data_string = NULL;
  unsigned char *uri_string = NULL;
  /* <http://example.org#subject> <http://example.org#predicate> "object" . */
#define SUBJECT_URI_STRING (const unsigned char*)"http://example.org#subject"
#define PREDICATE_URI_STRING (const unsigned char*)"http://example.org#predicate"
#define OBJECT_STRING "object"
  raptor_uri* s_uri = NULL;
  raptor_uri* p_uri = NULL;
  int size;
  int *bound_in;
  const char *data_file;
  
  if((data_file = getenv("NT_DATA_FILE"))) {
    /* got data from environment */
  } else {
    if(argc != 2) {
      fprintf(stderr, "USAGE: %s data-filename\n", program);
      return(1);
    }
    data_file = argv[1];
  }
    
  world = rasqal_new_world();
  if(!world || rasqal_world_open(world)) {
    fprintf(stderr, "%s: rasqal_world init failed\n", program);
    return(1);
  }
  
  query = rasqal_new_query(world, "sparql", NULL);
  
  data_string = raptor_uri_filename_to_uri_string(data_file);
  query_string = (unsigned char*)RASQAL_MALLOC(cstring, strlen((const char*)data_string)+strlen(query_format)+1);
  sprintf((char*)query_string, query_format, data_string);
  raptor_free_memory(data_string);
  
  uri_string = raptor_uri_filename_to_uri_string("");
#ifdef RAPTOR_V2_AVAILABLE
  base_uri = raptor_new_uri_v2(world->raptor_world_ptr, uri_string);  
#else
  base_uri = raptor_new_uri(uri_string);  
#endif
  raptor_free_memory(uri_string);

  query = rasqal_new_query(world, query_language_name, NULL);
  if(!query) {
    fprintf(stderr, "%s: creating query in language %s FAILED\n", program,
            query_language_name);
    failures++;
    goto tidy;
  }

  printf("%s: preparing %s query\n", program, query_language_name);
  rc = rasqal_query_prepare(query, query_string, base_uri);
  if(rc) {
    fprintf(stderr, "%s: failed to prepare query '%s'\n", program,
            query_string);
    failures++;
    goto tidy;
  }
  
  RASQAL_FREE(cstring, query_string);
  query_string = NULL;

  triples = rasqal_query_get_triple_sequence(query);
  start_column = 0;
  end_column = 0;

  triples_source = rasqal_new_triples_source(query);
  
  size = rasqal_variables_table_get_named_variables_count(query->vars_table);
  bound_in = rasqal_query_triples_build_bound_in(query, size,
                                                 start_column,
                                                 end_column);
  if(!bound_in) {
    fprintf(stderr, "%s: failed to create bound_in\n", program);
    failures++;
    goto tidy;
  }

  rowsource = rasqal_new_triples_rowsource(world, query, triples_source,
                                           triples, start_column, end_column,
                                           bound_in, size);
  if(!rowsource) {
    fprintf(stderr, "%s: failed to create triples rowsource\n", program);
    failures++;
    goto tidy;
  }

  while(1) {
    rasqal_row* row;
    rasqal_literal *s;
    rasqal_literal *p;
    rasqal_literal *o;

    row = rasqal_rowsource_read_row(rowsource);
    if(!row)
      break;
    
  #ifdef RASQAL_DEBUG  
    RASQAL_DEBUG1("Result Row:\n  ");
    rasqal_row_print(row, stderr);
    fputc('\n', stderr);
  #endif

#ifdef RAPTOR_V2_AVAILABLE
    s_uri = raptor_new_uri_v2(world->raptor_world_ptr, SUBJECT_URI_STRING);
    p_uri = raptor_new_uri_v2(world->raptor_world_ptr, PREDICATE_URI_STRING);
#else
    s_uri = raptor_new_uri(SUBJECT_URI_STRING);
    p_uri = raptor_new_uri(PREDICATE_URI_STRING);
#endif
    
    s = row->values[0];
    if(!s ||
       (s && s->type != RASQAL_LITERAL_URI) ||
#ifdef RAPTOR_V2_AVAILABLE
       !raptor_uri_equals_v2(world->raptor_world_ptr, s->value.uri, s_uri)
#else
       !raptor_uri_equals(s->value.uri, s_uri)
#endif
       ) {
      fprintf(stderr, "%s: 's' is bound to %s not URI %s\n", program,
              rasqal_literal_as_string(s),
#ifdef RAPTOR_V2_AVAILABLE
              raptor_uri_as_string_v2(world->raptor_world_ptr, s_uri)
#else
              raptor_uri_as_string(s_uri)
#endif
              );
      failures++;
    }
    p = row->values[1];
    if(!p ||
       (p && p->type != RASQAL_LITERAL_URI) ||
#ifdef RAPTOR_V2_AVAILABLE
       !raptor_uri_equals_v2(world->raptor_world_ptr, p->value.uri, p_uri)
#else       
       !raptor_uri_equals(p->value.uri, p_uri)
#endif
       ) {
      fprintf(stderr, "%s: 'p' is bound to %s not URI %s\n", program,
              rasqal_literal_as_string(p),
#ifdef RAPTOR_V2_AVAILABLE
              raptor_uri_as_string_v2(world->raptor_world_ptr, p_uri)
#else
              raptor_uri_as_string(p_uri)
#endif
              );
      failures++;
    }
    o = row->values[2];
    if(!o ||
       (o && o->type != RASQAL_LITERAL_STRING) ||
       strcmp((const char*)o->string, OBJECT_STRING)) {
      fprintf(stderr, "%s: 'o' is bound to %s not string '%s'\n", program,
              rasqal_literal_as_string(o), OBJECT_STRING);
      failures++;
    }

    rasqal_free_row(row);
    if(failures)
      break;
  }

  tidy:
#ifdef RAPTOR_V2_AVAILABLE
  raptor_free_uri_v2(world->raptor_world_ptr, base_uri);
  if(s_uri)
    raptor_free_uri_v2(world->raptor_world_ptr, s_uri);
  if(p_uri)
    raptor_free_uri_v2(world->raptor_world_ptr, p_uri);
#else
  raptor_free_uri(base_uri);
  if(s_uri)
    raptor_free_uri(s_uri);
  if(p_uri)
    raptor_free_uri(p_uri);
#endif

  if(triples_source)
    rasqal_free_triples_source(triples_source);
  if(rowsource)
    rasqal_free_rowsource(rowsource);
  if(query)
    rasqal_free_query(query);
  if(world)
    rasqal_free_world(world);

  return failures;
}

#endif

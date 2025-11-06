#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdbool.h>
#include <cjson/cJSON.h>

typedef cJSON json ;

void json_write_to_file (const json * json, const char * filepath) ;

// creation/deletion 

json * json_create_from_mem (const char * mem) ; 
json * json_create_from_file (const char * filepath) ; 
void   json_free (json * json) ; 
char * json_print (const json * json) ;

// object creation/manipulation

json * json_create_object () ;
json * json_add_string_to_object (json * object, const char * name, const char * value) ;
json * json_add_number_to_object (json * object, const char * name, const int * number) ;
bool   json_add_item_to_object (json * object, const char * name, json * item) ;

// array operations

json * json_create_array () ;
json * json_get_array_item (const json * jarray, const size_t index) ; 
int    json_get_array_size (const json * jarray) ; 

// type validation 

bool json_string_is_valid (const json * jstring) ; 
bool json_array_is_valid (const json * jarray) ;

// parsing

#define LAST_ELEMENT_INDEX -1

json * json_get_via_path (json * root, const char * path) ; 

#endif
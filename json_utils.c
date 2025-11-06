#include <cjson/cJSON.h>
#include <stdlib.h>
#include <string.h>

#include "include/json_utils.h"

#include "include/utils.h"
#include "include/raylib.h"

void json_write_to_file (const json * json, const char * filepath)
{
    if ( !json || !valid_string(filepath)) 
        return ;

    char * buffer = json_print(json) ;

    if ( !buffer) {
        fprintf(stderr, "json_write_to_file: json object is empty\n") ;
        return ; 
    }

    write_string_to_file(filepath, buffer) ;

    free(buffer); buffer = NULL ;
}

json * json_create_from_mem (const char * mem)
{
    return cJSON_Parse(mem) ;
}

json * json_create_from_file (const char * filepath)
{
    if ( !valid_string(filepath))
        return NULL ;

    char * fcontent = get_file_content(filepath) ;
    
    if ( !fcontent) 
        return NULL ;

    cJSON * json = json_create_from_mem(fcontent) ;

    free(fcontent) ; fcontent = NULL ;

    return json;
}

void json_free (json * json)
{
    if (json) {
        cJSON_Delete(json) ; 
        json = NULL ;
    }
}

char * json_print (const json * json)
{
    return json ? 
           cJSON_Print(json) : 
           NULL ;
}

json * json_create_object ()
{
    return cJSON_CreateObject() ;
}

json * json_add_string_to_object (json * object, const char * name, const char * string)
{
    return valid_string(name) && valid_string(string) ? 
           cJSON_AddStringToObject(object, name, string) : 
           NULL ;
}

json * json_add_number_to_object (json * object, const char * name, const int * number)
{
    return number ? 
           cJSON_AddNumberToObject(object, name, (*number)) :
           NULL ;
}

bool json_add_item_to_object (json * object, const char * name, json * item)
{
    return (item && valid_string(name)) ? 
           cJSON_AddItemToObject(object, name, item) :
           false ;
}

json * json_create_array ()
{
    return cJSON_CreateArray() ;
}

int json_get_array_size (const json * jarray)
{
    return json_array_is_valid(jarray) ? 
           cJSON_GetArraySize(jarray) : 
           -1 ;
}

json * json_get_array_item (const json * jarray, const size_t index) 
{
    return json_array_is_valid(jarray) ? 
           cJSON_GetArrayItem(jarray, index) :
           NULL ;
}

bool json_string_is_valid (const json * jstring)
{
     return jstring &&
           cJSON_IsString(jstring) && 
           valid_string(jstring->valuestring) ;
}

bool json_array_is_valid (const json * jarray)
{   
    return jarray && 
           cJSON_IsArray(jarray) ;
}

json * json_get_via_path (json * root, const char * path)
{
    if ( !root || !valid_string(path)) 
        return root ;

    int n = 0 ;
    const char ** elements = TextSplit(path, '.', &n) ;
    
    if ( !elements) 
        return NULL;

    cJSON * ret = root ;

    for(int i = 0; (i < n); i++) {
        if ( !valid_string(elements[i])) 
            continue ;

        const char * opening_brace = strchr(elements[i], '[') ;
        
        if (opening_brace) {
            const size_t name_len = (size_t) (opening_brace - elements[i]) ; 

            char array_name[name_len + 1] ; 
            
            memcpy(array_name, elements[i], name_len) ;
            array_name[name_len] = '\0' ;

            ret = cJSON_GetObjectItem(ret, array_name) ;
            
            if ( !cJSON_IsArray(ret)) 
                return NULL;

            const char* closing_brace = strchr(opening_brace, ']') ;
            
            if ( !closing_brace) 
                return NULL ;

            const size_t index_len = (size_t) (closing_brace - (opening_brace + 1)) ; 

            char index_buffer[index_len + 1] ;
            
            memcpy(index_buffer, (opening_brace + 1), index_len) ; 
            index_buffer[index_len] = '\0' ;  

            const size_t array_size = cJSON_GetArraySize(ret) ;
            
            const int index_buffer_val = atoi(index_buffer) ;
            
            const int index = (index_buffer_val == LAST_ELEMENT_INDEX) ? (array_size - 1) : index_buffer_val ;

            if (index >= array_size) 
                return NULL ;

            ret = cJSON_GetArrayItem(ret, index) ;
        } 

        else 
            ret = cJSON_GetObjectItem(ret, elements[i]) ;
    }

    return ret ;
}
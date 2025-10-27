#include <ctype.h>

#include "include/parse.h"
#include "include/data.h"
#include "include/query.h"

#include "../include/utils.h"
#include "../include/raylib.h"

static cJSON * parse_json_file (const char * filename)
{
    if ( !valid_string(filename)) 
        return NULL ;

    char * buffer = get_file_content(filename);
    
    if ( !buffer) 
        return NULL ;

    cJSON * json = cJSON_Parse(buffer) ;

    free(buffer) ; buffer = NULL ;

    return json;
}

static bool json_string_is_valid (const cJSON * json_string)
{
    return json_string &&
           cJSON_IsString(json_string) && 
           valid_string(json_string->valuestring) ;
}

static cJSON * get_cjson_ptr (cJSON * root, const char * path)
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

static bool json_string_equals (cJSON * json, const char * path, const char * expected)
{
    if ( !json || !valid_string(path) || !expected)
        return false ;

    const cJSON * jstring = get_cjson_ptr(json, path) ;

    return (json_string_is_valid(jstring)) && (strcmp(jstring->valuestring, expected) == 0) ;
}

typedef char * (*EnumToTextRoutine) (const int enumeration) ;

static int extract_enum_from_string (cJSON * root, const char * path, const size_t ne_memb, EnumToTextRoutine routine)
{
    if ( !root || !path || !routine)
        return -1 ;

    const cJSON * jstring = cJSON_GetObjectItem(root, path) ;
    
    if ( !json_string_is_valid(jstring)) 
        return false ;

    int enumeration = 0 ;
    for ( ; (enumeration < ne_memb) && (strcmp(routine(enumeration), jstring->valuestring) != 0); enumeration++)
        ;
    
    return enumeration ;
}

#define EXTRACT_ROUTINE_BUFFER 256

bool extract_string (cJSON * json, const char * path, void * dest, const size_t dest_size)
{
    if ( !json || !valid_string(path) || !dest) 
        return false ;

    const cJSON * jstring = get_cjson_ptr(json, path) ;
    
    if ( !json_string_is_valid(jstring)) 
        return false ;

    const int written = snprintf(((char*) dest), dest_size, "%s", jstring->valuestring) ;

    return (0 < written) && (written < dest_size) ;
}

bool extract_int_from_string (cJSON * json, const char * path, void * dest, const size_t dest_size)
{
    if ( !json || !valid_string(path) || !dest) 
        return false ;
    
    char buffer[EXTRACT_ROUTINE_BUFFER] = {0} ;

    if ( !extract_string(json, path, buffer, sizeof(buffer))) {
        fprintf(stderr, "extract_int_from_string: failed string extraction\n") ;
        return false ;
    } 

    const int numeric_chars = filter_non_numeric_chars(buffer, strlen(buffer)) ;

    if (numeric_chars <= 0) {
        fprintf(stderr, "extract_int_from_string: invalid string parsed: %s\n", buffer) ;
        return false ;
    }

    (*((int*) dest)) = strtol(buffer, NULL, 10) ;

    return true ;
}

bool extract_allocated_string (cJSON * root, const char * path, void * char_ptr, const size_t sizeof_dest)
{
    if ( !root || !path || !char_ptr)
        return false ;

    const cJSON * jstring = get_cjson_ptr(root, path) ;

    return (json_string_is_valid(jstring)) && ( (*((char**) char_ptr)) = strdup(jstring->valuestring)) ;
}

bool extract_video_duration (cJSON * json, const char * path, void * dest, const size_t dest_size)
{
    if ( !json || !valid_string(path) || !dest) 
        return false ;

    char duration_buffer[EXTRACT_ROUTINE_BUFFER] = {0} ;

    if ( !extract_string(json, path, duration_buffer, sizeof(duration_buffer))) {
        fprintf(stderr, "extract_video_duration: failed to extract raw video duration with the path %s\n", path) ;
        return false ;
    }

    const int duration_s = hms_to_seconds(duration_buffer) ;

    if (duration_s < 0) {
        fprintf(stderr, "extract_video_duration: invalid duration extracted from the buffer \"%s\"\n", duration_buffer) ;
        return false ;
    }

    (*((int*) dest)) = duration_s ;

    return true ;
}

static int get_youtube_multiplier (const char multiplier_char)
{
    switch (multiplier_char) {
        case 'K': return 1e3 ;
        case 'M': return 1e6 ;
        case 'B': return 1e9 ;
        default:
            return 1 ;
    }
}

// token is formatted as "x.xxA ...)", where 'A' is either 'K', 'M', 'B', or ' '

bool extract_youtube_formatted_number (cJSON* json, const char * path, void * dest, const size_t dest_size)
{
    if ( !json || !valid_string(path) || !dest) 
        return false ;

    char sub_count_buffer[EXTRACT_ROUTINE_BUFFER] = {0} ; 

    if ( !extract_string(json, path, sub_count_buffer, sizeof(sub_count_buffer))) {
        fprintf(stderr, "extract_youtube_formatted_number: failed extraction with path \"%s\"\n", path) ;
        return false ;
    }
    
    char * c_ptr = sub_count_buffer ;
    
    for (; !isalpha((*c_ptr)); c_ptr++)
        ;
    
    const char first_char = toupper((*c_ptr)) ; 
    
    (*c_ptr) = '\0' ;
    
    const int multiplier = get_youtube_multiplier(first_char) ;

    (*((int*) dest)) = strtof(sub_count_buffer, NULL) * multiplier ;

    return true ;
}

static bool get_video_thumbnail_path (const char * video_id, char * dest, const size_t dest_size)
{
    if ( !valid_string(video_id))
        return false ;

    const size_t written = snprintf(dest, dest_size, "/vi/%s/" MEDIUM_THUMBNAIL_VIDEO_RESOLUTION ".jpg", video_id) ;
    
    return (0 < written) && (written < dest_size) ;
}

bool extract_video_thumbnail (cJSON * json, const char * path, void * dest, const size_t dest_size)
{
    if ( !json || !valid_string(path) || !dest)
        return false ;

    char id_buffer[EXTRACT_ROUTINE_BUFFER] = {0} ;
    
    if ( !extract_string(json, path, id_buffer, sizeof(id_buffer))) {
        fprintf(stderr, "extract_video_thumbnail_path: failed to extract raw id with the path \"%s\"\n", path) ;
        return false ;
    }

    return get_video_thumbnail_path(id_buffer, dest, dest_size) ;
}

bool extract_channel_thumbnail (cJSON * json, const char * path, void * dest, const size_t dest_size)
{
    if ( !json || !valid_string(path) || !dest)
        return false ;

    char thumbnail_buffer[EXTRACT_ROUTINE_BUFFER] = {0} ;

    if ( !extract_string(json, path, thumbnail_buffer, sizeof(thumbnail_buffer))) {
        fprintf(stderr, "extract_channel_thumbnail_path: failed extraction with path \"%s\"\n", path) ;
        return false ;
    }

    //  the path either starts with '/ytc', or just '/'
    const char * potential_path1 = strstr(thumbnail_buffer, "/ytc") ;
    const char * potential_path2 = strrchr(thumbnail_buffer, '/') ;

    const int written = snprintf(((char*) dest), dest_size, "%s", potential_path1 ? potential_path1 : potential_path2) ;
            
    return (0 < written) && (written < dest_size) ;
}

bool parse_token_is_ready (const ParseToken * token)
{
    return token->routine && 
           enum_is_valid(token->field, FIELD_COUNT) ;
}

static const ParseToken VIDEO_TOKENS [] = {
    TOKEN_ID,
    TOKEN_TITLE,
    TOKEN_DURATION,
    TOKEN_AUTHOR_ID,
    TOKEN_VIEW_COUNT,
    TOKEN_UPLOAD_DATE,
    TOKEN_VIDEO_THUMBNAIL,
    TOKEN_SENTINAL,
} ;

static const ParseToken LIVE_VIDEO_TOKENS [] = {
    TOKEN_ID,
    TOKEN_TITLE,
    TOKEN_AUTHOR_ID,
    TOKEN_VIEW_COUNT_LIVE,
    TOKEN_VIDEO_THUMBNAIL,
    TOKEN_SENTINAL,
} ;

static const ParseToken PLAYLIST_VIDEO_TOKENS [] = {
    TOKEN_ID,
    TOKEN_TITLE,
    TOKEN_DURATION,
    TOKEN_AUTHOR_ID,
    TOKEN_VIEW_COUNT_FORMATTED,
    TOKEN_UPLOAD_DATE,
    TOKEN_VIDEO_THUMBNAIL,
    TOKEN_SENTINAL,
} ;

static const ParseToken HIGHLIGHTED_VIDEO_TOKENS [] = {
    TOKEN_ID,
    TOKEN_TITLE,
    { FIELD_DURATION, extract_int_from_string }, // TODO : move to .h... need to figure out fallback concept for parsing tokens
    TOKEN_AUTHOR_ID,
    TOKEN_VIEW_COUNT_FORMATTED,
    TOKEN_UPLOAD_DATE,
    TOKEN_VIDEO_THUMBNAIL,
    TOKEN_DESCRIPTION,
    TOKEN_SENTINAL,
} ;

static const ParseToken CHANNEL_TOKENS [] = {
    TOKEN_ID,
    TOKEN_TITLE,
    TOKEN_SUBSCRIBER_COUNT,
    TOKEN_CHANNEL_THUMBNAIL,
    TOKEN_SENTINAL,
} ;

static const ParseToken PLAYLIST_TOKENS [] = {
    TOKEN_ID,
    TOKEN_TITLE,
    TOKEN_PLAYLIST_LENGTH,
    TOKEN_VIDEO_THUMBNAIL,
    TOKEN_SENTINAL,
} ;

const ParseToken * find_parse_tokens (const SearchResultType type)
{
    switch (type) {
        case SEARCH_RESULT_TYPE_VIDEO:             return VIDEO_TOKENS ;
        case SEARCH_RESULT_TYPE_LIVE_VIDEO:        return LIVE_VIDEO_TOKENS ;
        case SEARCH_RESULT_TYPE_PLAYLIST_VIDEO:    return PLAYLIST_VIDEO_TOKENS ;
        case SEARCH_RESULT_TYPE_CHANNEL:           return CHANNEL_TOKENS ;
        case SEARCH_RESULT_TYPE_PLAYLIST:          return PLAYLIST_TOKENS ;
        case SEARCH_RESULT_TYPE_HIGHLIGHTED_VIDEO: return HIGHLIGHTED_VIDEO_TOKENS ;
        default:
            return NULL ;
    }
}

bool parse_profile_init (ParseProfile * profile, cJSON * raw_profile)
{
    if ( !profile || !raw_profile)
        return false ;

    if ( !extract_allocated_string(raw_profile, PARSE_PROFILE_RESULT_OBJECT, &profile->results_path, sizeof(char*))) {
        fprintf(stderr, "parse_profile_init: failed to extract " PARSE_PROFILE_RESULT_OBJECT "\n") ;
        return false ;
    }

    if ( !extract_allocated_string(raw_profile, PARSE_PROFILE_CONTINUATION_TOKEN_OBJECT, &profile->continuation_token_path, sizeof(char*))) {
        fprintf(stderr, "parse_profile_init: failed to extract " PARSE_PROFILE_CONTINUATION_TOKEN_OBJECT "\n") ;
        return false ;
    }

    profile->query_action = extract_enum_from_string(raw_profile, PARSE_PROFILE_QUERY_ACTION, QUERY_ACTION_COUNT, (void*) query_action_to_text) ;
    
    if ( !enum_is_valid(profile->query_action, QUERY_ACTION_COUNT)) {
        fprintf(stderr, "parse_profile_init: failed to retrieve QueryType\n") ;
        return false ;
    }

    profile->query_type = extract_enum_from_string(raw_profile, PARSE_TOKEN_QUERY_TYPE, QUERY_TYPE_COUNT, (void*) query_type_to_text) ;

    if ( !enum_is_valid(profile->query_type, QUERY_TYPE_COUNT)) {
        fprintf(stderr, "parse_profile_init: failed to retrieve QueryAttribute\n") ;
        return false ;
    }

    return true ;
}

void parse_profile_free (ParseProfile * profile)
{
    if (profile) {
        if (profile->results_path) {
            free(profile->results_path) ; profile->results_path = NULL ;
        }

        if (profile->continuation_token_path) {
            free(profile->continuation_token_path) ; profile->continuation_token_path = NULL ;
        }
    }
}

void parse_profile_print (const ParseProfile * profile)
{
    if (profile) {
        printf("ACTION: \"%s\"\nTYPE: \"%s\"\nRESULT: \"%s\"\nCONTINUATION_TOKEN: \"%s\"\n\n",
            query_action_to_text(profile->query_action),
            query_type_to_text(profile->query_type),
            profile->results_path,
            profile->continuation_token_path) ;
    }
}

bool profile_list_init(ProfileList * profile_list, cJSON * config)
{
    if ( !profile_list || !config)
        return false ;

    const cJSON * parseProfiles = get_cjson_ptr(config, PARSE_PROFILES_OBJECT) ;

    if ( !cJSON_IsArray(parseProfiles)) {
        fprintf(stderr, "profile_list_init: failed to retrieve %s\n", PARSE_PROFILES_OBJECT) ;
        return false ;
    }

    profile_list->count = cJSON_GetArraySize(parseProfiles) ;

    profile_list->profiles = calloc(profile_list->count, sizeof(ParseProfile)) ;

    if ( !profile_list->profiles) {
        fprintf(stderr, "profile_list_init: failed to allocate %zu ParseProfile(s)\n", profile_list->count) ;
        return false ;
    }

    for (size_t i = 0; i < profile_list->count; i++) {
        
        cJSON * raw_profile = cJSON_GetArrayItem(parseProfiles, i) ;
        
        ParseProfile * profile = profile_list->profiles + i ;

        if ( !parse_profile_init(profile, raw_profile)) {
            fprintf(stderr, "profile_list_init: failed to configure profile at index %zu\n", i) ;
            profile_list_free(profile_list) ;
            return false ;
        }
    }

    return true ;
}

void profile_list_free (ProfileList * profile_list)
{
    if ( !profile_list)
        return ;

    size_t i = 0 ;

    for (ParseProfile * profile = profile_list->profiles; profile && i < profile_list->count; profile++, i++)
        parse_profile_free(profile) ;

    free(profile_list->profiles) ; profile_list->profiles = NULL ;
}

bool path_template_init (PathTemplate * template, cJSON * raw_template)
{
    if ( !template || !raw_template)
        return false ;

    template->response_path = NULL ;
    
    if ( !extract_allocated_string(raw_template, PATH_TEMPLATES_JSON_PATH_OBJECT, &template->response_path, sizeof(char*))) {
        fprintf(stderr, "path_template_init: failed to retrieve %s\n", PATH_TEMPLATES_JSON_PATH_OBJECT) ;
        return false ;
    }

    template->response_type = extract_enum_from_string(raw_template, PATH_TEMPLATES_NAME_OBJECT, RESPONSE_COUNT, (void*) response_type_to_text) ;

    if ( !enum_is_valid(template->response_type, RESPONSE_COUNT)) {
        fprintf(stderr, "path_template_init: failed to retrieve YoutubeJsonResponseType\n") ;
        return false ;
    }   

    for (YoutubeResultField field = (YoutubeResultField) 0; (field < FIELD_COUNT); field++) {
        const char * path = field_to_text(field) ;
        extract_allocated_string(raw_template, path, &template->field_paths[field], sizeof(char*)) ;
    }

    return true ;
}

void path_template_free (PathTemplate * template)
{
    if ( !template)
        return ;

    if (template->response_path) {
        free(template->response_path) ; template->response_path = NULL ;
    }

    for (size_t i = 0; (i < FIELD_COUNT); i++) {
        if (template->field_paths[i]) {
            free(template->field_paths[i]) ; template->field_paths[i] = NULL ;
        }
    }
}

bool path_template_is_ready (const PathTemplate * template)
{
    return template && 
           template->response_path && 
           enum_is_valid(template->response_type, RESPONSE_COUNT) ;
}

bool path_template_list_init (PathTemplateList * template_list, cJSON * config)
{
    if ( !template_list || !config)
        return false ;

    const cJSON * pathTemplates = get_cjson_ptr(config, PATH_TEMPLATES_OBJECT) ;

    if ( !cJSON_IsArray(pathTemplates)) {
        fprintf(stderr, "path_template_list_init: failed to retrieve %s\n", PATH_TEMPLATES_OBJECT) ;
        return false ;
    }

    template_list->count = cJSON_GetArraySize(pathTemplates) ;

    template_list->templates = calloc(template_list->count, sizeof(PathTemplate)) ;

    if ( !template_list->templates) {
        fprintf(stderr, "path_template_list_init: failed to allocate %zu PathTemplate object(s)\n", template_list->count) ;
        return false ;
    }

    for ( size_t i = 0 ; (i < template_list->count); i++) {
        
        cJSON * raw_template = cJSON_GetArrayItem(pathTemplates, i) ;

        PathTemplate * template = template_list->templates + i ;

        if ( !path_template_init(template, raw_template)) {
            fprintf(stderr, "path_template_list_init: failed to create PathTemplate object at index %zu\n", i) ;
            return false ;
        }
    }

    return true ;
}

void path_template_list_free (PathTemplateList * template_list)
{
    if ( !template_list)
        return ;
    
    size_t i = 0 ;

    for (PathTemplate * template = template_list->templates; (i < template_list->count); template++, i++) 
            path_template_free(template) ;

    free(template_list->templates) ; template_list->templates = NULL ;
}

bool youtube_parse_context_init (YoutubeParseContext * context, const char * parse_config_path) 
{
    if ( !context || !valid_string(parse_config_path))
        return false ;

    cJSON * config_json = parse_json_file(parse_config_path) ;

    if ( !config_json) {
        fprintf(stderr, "youtube_parse_context_init: failed to parse %s\n", parse_config_path) ;
        return false ;
    }

    if ( !profile_list_init(&context->profile_list, config_json)) {
        fprintf(stderr, "youtube_parse_context_init: failed to init ProfileList object\n") ;
        cJSON_Delete(config_json) ;
        return false ;
    }

    if ( !path_template_list_init(&context->template_list, config_json)) {
        fprintf(stderr, "youtube_parse_context_init: failed to init PathTemplateList object\n") ;
        cJSON_Delete(config_json) ;
        return false ;
    }

    cJSON_Delete(config_json) ;

    return true ;
}

void youtube_parse_context_free (YoutubeParseContext * context)
{
    if ( !context)
        return ;

    profile_list_free(&context->profile_list) ;
    path_template_list_free(&context->template_list) ;
}

typedef struct {
    void * address ;
    size_t size ;
} ExtractDest ;

static bool extract_dest_is_ready (const ExtractDest * info)
{
    return info && info->address ;
}

static ExtractDest get_extract_dest (const YoutubeSearchResult * result, const YoutubeResultField field)
{
    static const size_t FIELD_OFFSETS[] = {
        [FIELD_ID]               = offsetof(YoutubeSearchResult, id),
        [FIELD_TITLE]            = offsetof(YoutubeSearchResult, title),
        [FIELD_DURATION]         = offsetof(YoutubeSearchResult, duration),
        [FIELD_AUTHOR_ID]        = offsetof(YoutubeSearchResult, author_id),
        [FIELD_VIEW_COUNT]       = offsetof(YoutubeSearchResult, view_count),
        [FIELD_LIVE_VIEW_COUNT]  = offsetof(YoutubeSearchResult, view_count),
        [FIELD_PLAYLIST_LENGTH]  = offsetof(YoutubeSearchResult, playlist_length),
        [FIELD_UPLOAD_DATE]      = offsetof(YoutubeSearchResult, upload_date),
        [FIELD_THUMBNAIL_PATH]   = offsetof(YoutubeSearchResult, thumbnail_path),
        [FIELD_SUBSCRIBER_COUNT] = offsetof(YoutubeSearchResult, subscriber_count),
        [FIELD_DESCRIPTION]      = offsetof(YoutubeSearchResult, description),
    } ; 

    static const size_t FIELD_SIZES[] = {
        [FIELD_ID]               = sizeof(result->id),
        [FIELD_TITLE]            = sizeof(result->title),
        [FIELD_DURATION]         = sizeof(result->duration),
        [FIELD_AUTHOR_ID]        = sizeof(result->author_id),
        [FIELD_VIEW_COUNT]       = sizeof(result->view_count),
        [FIELD_LIVE_VIEW_COUNT]  = sizeof(result->view_count),
        [FIELD_PLAYLIST_LENGTH]  = sizeof(result->playlist_length),
        [FIELD_UPLOAD_DATE]      = sizeof(result->upload_date),
        [FIELD_THUMBNAIL_PATH]   = sizeof(result->thumbnail_path),
        [FIELD_SUBSCRIBER_COUNT] = sizeof(result->subscriber_count),
        [FIELD_DESCRIPTION]      = sizeof(result->description),

    } ;

    ExtractDest dest = {0} ;

    if (enum_is_valid(field, FIELD_COUNT)) {
        dest.size = FIELD_SIZES[field] ;
        dest.address = (((char*) result) + FIELD_OFFSETS[field]) ;
    }

    return dest ;    
}

static SearchResultType find_search_result_type (cJSON * response, const YoutubeJSONResponse type)
{
    if ( !response)
        return SEARCH_RESULT_TYPE_UNDF ;

    switch (type) {
        case RESPONSE_RICH_ITEM_RENDERER:
        case RESPONSE_VIDEO_RENDERER: 
            return json_string_equals(response, LIVE_VIDEO_IDENTIFIER_PATH, LIVE_VIDEO_EXPECTED_VALUE) ? SEARCH_RESULT_TYPE_LIVE_VIDEO : SEARCH_RESULT_TYPE_VIDEO ;
        case RESPONSE_CHANNEL_RENDERER:
        case RESPONSE_HIGHLIGHTED_CHANNEL_RENDERER: 
            return SEARCH_RESULT_TYPE_CHANNEL ;
        case RESPONSE_LOCKUP_VIEW_MODEL: 
            return json_string_equals(response, PLAYLIST_IDENTIFIER_PATH, PLAYLIST_EXPECTED_VALUE) ? SEARCH_RESULT_TYPE_PLAYLIST : SEARCH_RESULT_TYPE_VIDEO ;
        case RESPONSE_PLAYLIST_VIDEO_RENDERER: 
            return SEARCH_RESULT_TYPE_PLAYLIST_VIDEO ;
        case RESPONSE_HIGHLIGHTED_VIDEO_RENDERER:
            return SEARCH_RESULT_TYPE_HIGHLIGHTED_VIDEO ;
        default:
            return SEARCH_RESULT_TYPE_UNDF ;
    }
}

bool parse_youtube_search_result (YoutubeSearchResult * dest, cJSON * json, PathTemplate * template)
{
    if ( !dest || !json || !path_template_is_ready(template))
        return false ;

    cJSON * result_json = get_cjson_ptr(json, template->response_path) ;

    if ( !result_json) {
        fprintf(stderr, "parse_youtube_search_result: failed to retrieve search result JSON object with path %s\n", template->response_path) ;
        return false ;
    }

    dest->type = find_search_result_type(result_json, template->response_type) ;

    if (dest->type == SEARCH_RESULT_TYPE_UNDF) {
        fprintf(stderr, "parse_youtube_search_result: failed to find search result type of json response item\n") ;
        return false ;
    }

    const ParseToken * parse_tokens = find_parse_tokens(dest->type) ;

    if ( !parse_tokens) {
        fprintf(stderr, "parse_youtube_search_result: the SearchResultType %d returned invalid parse token lis\n", dest->type) ;
        return NULL ;
    }

    for (const ParseToken * token = parse_tokens; parse_token_is_ready(token); token++) {

        const char * path = template->field_paths[token->field] ;
        
        if ( !path)
            continue ;

        const ExtractDest field_dest = get_extract_dest(dest, token->field) ;

        if ( !extract_dest_is_ready(&field_dest))
            continue ;

        if ( !token->routine(result_json, path, field_dest.address, field_dest.size)) {
            fprintf(stderr, "EXTRACTION ERROR: (JSON Object: %s, Field: %s, Result Type: %s, Path: %s)\n",
                response_type_to_text(template->response_type),
                field_to_text(token->field),
                result_type_to_text(dest->type),
                path) ;
        }
    }

    return true ;
}

PathTemplate * find_path_template (PathTemplateList * list, cJSON * item)
{
    if ( !list || !item)
        return NULL ;

     for (size_t i = 0; (i < list->count); i++) {
        if (get_cjson_ptr(item, list->templates[i].response_path))
            return &list->templates[i] ;
    }

    return NULL ;
}

static ParseProfile * find_parse_profile (ProfileList * list, const QueryAction action, const QueryType type)
{
    if ( !list)
        return NULL ;

    ParseProfile * profile = list->profiles ;

    for (size_t i = 0; (profile) && (i < list->count); profile++, i++) {
        if ( (profile->query_action == action) && (profile->query_type == type))
            break ;
    }

    return profile ;
}

int get_youtube_search_results (cJSON * response, YoutubeParseContext * context, LinkedList * results_dest, char ** continuation_token, const QueryType type, const QueryAction action)
{
    if ( !response || !context || !results_dest || !continuation_token) 
        return -1 ;

    ParseProfile * profile = find_parse_profile(&context->profile_list, action, type) ;

    if ( !profile) {
        fprintf(stderr, "get_youtube_search_results: no ParseProfile object found (QueryType: %s, QueryAction: %s)\n", 
            query_type_to_text(type),
            query_action_to_text(action)) ;
        
        return -1 ;
    }

    parse_profile_print(profile) ;

    if (*continuation_token) {
        free((*continuation_token)) ; (*continuation_token) = NULL ;
    } 

    if ( !extract_allocated_string(response, profile->continuation_token_path, continuation_token, sizeof(char*))) 
        fprintf(stderr, "get_youtube_search_results: no continuation token with path \"%s\"\n", profile->continuation_token_path) ;
    
    else
        printf("EXTRACTED CONTINUATION TOKEN: \"%s\"\n\n", (*continuation_token)) ;

    cJSON * search_results = get_cjson_ptr(response, profile->results_path) ;

    if ( !cJSON_IsArray(search_results)) {
        fprintf(stderr, "get_youtube_search_results: %s returned an invalid results array object\n", profile->results_path) ;
        return -1 ;
    }

    pthread_mutex_lock(&results_dest->mutex) ;

    const size_t prev_size = results_dest->count ; 

    pthread_mutex_unlock(&results_dest->mutex) ;

    int results_added = 0 ;
    
    const size_t nelements = cJSON_GetArraySize(search_results) ;

    for (size_t i = 0; i < nelements; i++) {
        cJSON * item = cJSON_GetArrayItem(search_results, i) ;

        PathTemplate * template = find_path_template(&context->template_list, item) ;
    
        if ( !template) {
            cJSON * unknown_object = item->child ;
            char * name = unknown_object ? unknown_object->string : "" ;
            fprintf(stderr, "get_youtube_search_results: unknown json result object: \"%s\"\n", name) ;
            continue ;
        }

        YoutubeSearchResult * result = youtube_search_result_init() ;

        if ( !result) {
            fprintf(stderr, "get_youtube_search_results: failed to create YoutubeSearchResult object\n") ;
            return results_added ;
        }

        if ( !parse_youtube_search_result(result, item, template)) {
            fprintf(stderr, "get_youtube_search_results: failed to parse YoutubeSearchResult object\n") ;
            youtube_search_result_free(result) ;
            continue ;
        }

        Node * node = node_init(result, sizeof(YoutubeSearchResult), (void*) youtube_search_result_free, (void*) youtube_search_result_print) ;
    
        if ( !node) {
            fprintf(stderr, "get_youtube_search_result: failed to create Node object\n") ;
            youtube_search_result_free(result) ;
            return results_added ;
        }

        pthread_mutex_lock(&results_dest->mutex) ;
    
        linked_list_append(results_dest, node) ;
    
        pthread_mutex_unlock(&results_dest->mutex) ;
        
        results_added++ ;
    }

    if (type == QUERY_TYPE_REPLACE) {
        pthread_mutex_lock(&results_dest->mutex) ;
        for (size_t i = 0; (i < prev_size) && (results_dest->head); i++) 
            node_free(linked_list_dequeue(results_dest)) ;
        pthread_mutex_unlock(&results_dest->mutex) ;
    }

    return results_added ;
} 
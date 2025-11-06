#ifndef PARSE_H
#define PARSE_H

#include "data.h"
#include "query.h"
#include "../../include/json_utils.h"
#include "../../include/linked_list.h"

#define HIGH_THUMBNAIL_VIDEO_RESOLUTION     "hqdefault"
#define MEDIUM_THUMBNAIL_VIDEO_RESOLUTION   "mqdefault"
#define STANDARD_THUMBNAIL_VIDEO_RESOLUTION "sddefault"

typedef bool (*ExtractRoutine) (json * json, const char * path, void * dest, const size_t sizeof_dest) ;

bool extract_int (json * root, const char * path, void * dest, const size_t dest_size) ;
bool extract_string  (json * json, const char * path, void * dest, const size_t dest_size) ;
bool extract_int_from_string (json * json, const char * path, void * dest, const size_t dest_size) ;
bool extract_allocated_string (json * json, const char * path, void * dest, const size_t dest_size) ;
bool extract_video_duration (json * json, const char * path, void * dest, const size_t dest_size) ;
bool extract_youtube_formatted_number (json * json, const char * path, void * dest, const size_t dest_size) ;
bool extract_video_thumbnail (json * json, const char * path, void * dest, const size_t dest_size) ;
bool extract_channel_thumbnail (json * json, const char * path, void * dest, const size_t dest_size) ;

typedef struct {
    YoutubeResultField field;  
    ExtractRoutine routine;    
} ParseToken;

bool parse_token_is_ready (const ParseToken * token) ;
const ParseToken * find_parse_tokens (SearchResultType type) ;
void parse_token_execute (const ParseToken * token, const char * path, YoutubeSearchResult * result, json * json, const YoutubeJSONResponse response_type) ;

#define TOKEN_ID                   { FIELD_ID, extract_string }
#define TOKEN_TITLE                { FIELD_TITLE, extract_string }
#define TOKEN_AUTHOR_ID            { FIELD_AUTHOR_ID, extract_string }
#define TOKEN_DURATION             { FIELD_DURATION, extract_video_duration }
#define TOKEN_UPLOAD_DATE          { FIELD_UPLOAD_DATE, extract_string }
#define TOKEN_VIEW_COUNT           { FIELD_VIEW_COUNT, extract_int_from_string }
#define TOKEN_VIEW_COUNT_LIVE      { FIELD_LIVE_VIEW_COUNT, extract_int_from_string }
#define TOKEN_VIEW_COUNT_FORMATTED { FIELD_VIEW_COUNT, extract_youtube_formatted_number }
#define TOKEN_SUBSCRIBER_COUNT     { FIELD_SUBSCRIBER_COUNT, extract_youtube_formatted_number }
#define TOKEN_PLAYLIST_LENGTH      { FIELD_PLAYLIST_LENGTH, extract_int_from_string }
#define TOKEN_VIDEO_THUMBNAIL      { FIELD_THUMBNAIL_PATH, extract_video_thumbnail }
#define TOKEN_CHANNEL_THUMBNAIL    { FIELD_THUMBNAIL_PATH, extract_channel_thumbnail }
#define TOKEN_DESCRIPTION          { FIELD_DESCRIPTION, extract_allocated_string }
#define TOKEN_SENTINAL             { -1, NULL }

// paths to 'parseProfiles' objects in paths.json 

#define PARSE_PROFILES_OBJECT                   "parseProfiles"
#define PARSE_PROFILE_QUERY_ACTION              "queryAction"
#define PARSE_TOKEN_QUERY_TYPE                  "queryType"
#define PARSE_PROFILE_RESULT_OBJECT             "results"
#define PARSE_PROFILE_CONTINUATION_TOKEN_OBJECT "continuationToken"

typedef struct {
    QueryType query_type;
    QueryAction query_action;
    char * results_path;            // path to JSON array object that conatins the results from some query type/action tuple      
    char * continuation_token_path; // path to continuation token   
} ParseProfile;

bool parse_profile_init  (ParseProfile * profile, json * raw_profile) ;
void parse_profile_free  (ParseProfile * profile) ;
void parse_profile_print (const ParseProfile * profile) ;

typedef struct {
    ParseProfile * profiles;
    size_t count;
} ProfileList;

bool profile_list_init (ProfileList * profile_list, json * config) ;
void profile_list_free (ProfileList * profile_list) ;

// paths to 'pathTemplates' objects in paths.json 

#define PATH_TEMPLATES_OBJECT           "pathTemplates"
#define PATH_TEMPLATES_NAME_OBJECT      "NAME"
#define PATH_TEMPLATES_JSON_PATH_OBJECT "JSON_PATH"

typedef struct {
    char * field_paths[FIELD_COUNT]; 
    char * response_path;
    YoutubeJSONResponse response_type;
} PathTemplate ;

bool path_template_init     (PathTemplate * ptemplate, json * raw_template) ;
void path_template_free     (PathTemplate * ptemplate) ;
bool path_template_is_ready (const PathTemplate * ) ;

typedef struct {
    PathTemplate * templates;
    size_t count;
} PathTemplateList;

bool path_template_list_init (PathTemplateList * template_list, json * config) ;
void path_template_list_free (PathTemplateList * template_list) ;
PathTemplate * find_path_template (PathTemplateList * list, json * item) ;

typedef struct {
    ProfileList profile_list;
    PathTemplateList template_list;
} YoutubeParseContext;

bool youtube_parse_context_init (YoutubeParseContext * context, const char * pase_config_path) ;
void youtube_parse_context_free (YoutubeParseContext * context) ;

bool parse_youtube_search_result (YoutubeSearchResult * dest, json * json, PathTemplate * ptemplate) ;
int  get_youtube_search_results (json * youtube_response, YoutubeParseContext * parse, LinkedList * results_dest, char ** continuation_token, const QueryType type, const QueryAction action) ;

// TODO : this implementation is shit, put identifiers in config

#define LIVE_VIDEO_IDENTIFIER_PATH ".badges[0].metadataBadgeRenderer.label"
#define LIVE_VIDEO_EXPECTED_VALUE  "LIVE"

#define PLAYLIST_IDENTIFIER_PATH   ".contentType"
#define PLAYLIST_EXPECTED_VALUE    "LOCKUP_CONTENT_TYPE_PLAYLIST"

json * create_search_result_json (const YoutubeSearchResult * result) ;

#endif
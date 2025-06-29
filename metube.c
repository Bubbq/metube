#include <ctype.h>
#include <openssl/types.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <cjson/cJSON.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#define MINUTE 60
#define CACHED_THUMBNAIL_LIFETIME (MINUTE * 1)
#define MAX_ITEMS_PER_PAGE 20

typedef struct
{
	double startTime;
	double lifeTime;
} Timer;

void start_timer(Timer *timer, double lifetime) 
{
	timer->startTime = GetTime();
	timer->lifeTime = lifetime;
}

bool is_timer_done(Timer timer)
{ 
	return (GetTime() - timer.startTime) >= timer.lifeTime; 
} 

typedef struct {
    size_t size;
    char* memory;
} MemoryBlock;

bool is_memory_ready(const MemoryBlock chunk)
{
    return ((chunk.size > 0) && (chunk.memory != NULL));
}

MemoryBlock create_memory_block(size_t inital)
{
    MemoryBlock chunk = { 0 };

    chunk.memory = malloc(inital);
    if (!chunk.memory) printf("create_memory_block: not enough memory, malloc returned NULL\n");
    else chunk.size = inital;
    
    return chunk;
}

void unload_memory_block(MemoryBlock* chunk)
{
    free(chunk->memory);
    chunk->memory = NULL;
    chunk->size = 0;
}

typedef enum {
    CONTENT_TYPE_VIDEO = 0,
    CONTENT_TYPE_CHANNEL = 1,
    CONTENT_TYPE_PLAYLIST = 2,
    CONTENT_TYPE_LIVE = 3,
    CONTENT_TYPE_ANY = 4,
} ContentType;

typedef enum 
{
    SORT_PARAM_RELEVANCE = 0,
    SORT_PARAM_UPLOAD_DATE = 1,
    SORT_PARAM_VIEW_COUNT = 2,
    SORT_PARAM_RATING = 3,
} SortParameter;

typedef struct YoutubeSearchNode {
	char id[64];
	char title[128];
	char author[128];
	char subs[128];
	char views[128];
	char date[128];
	char length[128];
    char video_count[128];
    bool thumbnail_loaded;
    char thumbnail_link[128];
    Texture thumbnail;
    ContentType type;
    bool is_live;
    struct YoutubeSearchNode* next;
} YoutubeSearchNode;

typedef struct {
    int count;
    YoutubeSearchNode* head;
    YoutubeSearchNode* tail;
} YoutubeSearchList;

YoutubeSearchList create_youtube_search_list() 
{
    YoutubeSearchList list;
    list.head = list.tail = NULL;
    list.count = 0;
    return list;
}

void add_node(YoutubeSearchList* list, const YoutubeSearchNode node)
{
    // adding the first node to a list
    if (list->count == 0) {
        list->head = list->tail = malloc(sizeof(YoutubeSearchNode));
        if (!list->head) {
            printf("add_node: not enough memory, malloc returned null\n");
            return;
        }

        // set both ends of the list to the first node of the list
        *list->head = *list->tail = node;
    } 
    else {
        // allocate space for the new node
        list->tail->next = malloc(sizeof(YoutubeSearchNode));
        if (!list->tail->next) {
            printf("add_node: not enough memory, malloc returned null\n");
            return;
        }

        // append node to list
        *list->tail->next = node;
        
        // adjust tail ptr pos.
        list->tail = list->tail->next;
        list->tail->next = NULL;
    }
    
    // update list size
    list->count++;
}

void unload_node(YoutubeSearchNode* node)
{
    if (!node) return;
    free(node);
}

void unload_list(YoutubeSearchList* list) 
{
    YoutubeSearchNode* prev = NULL;
    YoutubeSearchNode* current = list->head;

    while (current) {
        prev = current;
        current = current->next;
        unload_node(prev);
    }

    list->head = list->tail = NULL;
    list->count = 0;
}

void print_node(const YoutubeSearchNode* node) 
{
    printf("id) %s title) %s author) %s subs) %s views) %s date) %s length) %s video count) %s thumbnail id) %d type) %d\n", 
            node->id, node->title, node->author, node->subs, node->views, node->date, node->length, node->video_count, node->thumbnail.id, node->type);
}

void print_list(const YoutubeSearchList* list)
{
    for (YoutubeSearchNode *current = list->head; current; current = current->next) print_node(current);
}

typedef struct {
    char url_encoded_query[256];
    SortParameter sort;
    ContentType type;
    bool allow_shorts;
} Query;

void configure_search_url(const int maxlen, char search_url[maxlen], const Query query)
{
    snprintf(search_url, maxlen, "/results?search_query=%s&sp=", query.url_encoded_query);


    // possible sorting params
    const char* relevance = "CAA";
    const char* upload_date = "CAI";
    const char* popularity = "CAM";
    const char* rating = "CAE";
    
    switch (query.sort) {
        case SORT_PARAM_RELEVANCE: strcat(search_url, relevance); break;
        case SORT_PARAM_UPLOAD_DATE: strcat(search_url, upload_date); break;
        case SORT_PARAM_VIEW_COUNT: strcat(search_url, popularity); break;
        case SORT_PARAM_RATING: strcat(search_url, rating); break;
    }

    // possible type params
    const char* video = "SAhAB"; 
    const char* channel = "SAhAC";
    const char* playlist = "SAhAD";
    const char* none = "%253D";
    const char *live = "SBBABQAE%253D";

    switch (query.type) {
        case CONTENT_TYPE_VIDEO: strcat(search_url, video); break;
        case CONTENT_TYPE_CHANNEL: strcat(search_url, channel); break;
        case CONTENT_TYPE_PLAYLIST: strcat(search_url, playlist); break;
        case CONTENT_TYPE_ANY: strcat(search_url, none); break;
        case CONTENT_TYPE_LIVE: strcat(search_url, live);
    }  
}


void format_youtube_views(const char* views_str, const int maxlen, char dst[maxlen])
{
    // remove  all non numeric chars
    char no_commas[32] = "\0";
    for (int i = 0, j = 0; views_str[i]; i++) 
        if (isdigit(views_str[i])) no_commas[j++] = views_str[i];

    // convert to int
    long long views = atoll(no_commas);

    // formatting string
    
    // hundreds
    if (views < 1e3) snprintf(dst, maxlen, "%lld", views);
    
    // thousands
    else if (views < 1e6) {
        if (views < 1e5) snprintf(dst, maxlen, "%.1fk", (views / 1e3));
        else snprintf(dst, maxlen, "%dk", (int)(views / 1e3));
    }

    // millions
    else if (views < 1e9) {
        if (views < 1e8) snprintf(dst, maxlen, "%.1fM", (views / 1e6));
        else snprintf(dst, maxlen, "%dM", (int)(views / 1e6));
    }
    
    // billions
    else if (views < 1e12) {
        if (views < 1e11) snprintf(dst, maxlen, "%.1fB", (views / 1e9));
        else snprintf(dst, maxlen, "%dB", (int)(views / 1e9));
    }

    // trim '.0' if present
    char* loc = strstr(dst, ".0");
    if (loc) *loc = '\0';
}

Texture2D get_thumbnail_from_memory (const MemoryBlock chunk, const float width, const float height)
{
    if (is_memory_ready(chunk)) {
        Image image = LoadImageFromMemory(".jpg", (unsigned char*) chunk.memory, chunk.size);
        if (IsImageReady(image)) {
            ImageResize(&image, width, height);
            Texture2D ret = LoadTextureFromImage(image);
            UnloadImage(image);
            return ret;
        }
        else printf("get_thumbnail_from_memory: failed to load image data\n");
    }

    return (Texture){ 0 };
}

int bound_index_to_array (const int pos, const int array_size)
{
    return (pos + array_size) % array_size;
}

char *content_type_to_host (const ContentType content_type)
{
    switch (content_type) {
        case CONTENT_TYPE_LIVE:
        case CONTENT_TYPE_VIDEO:
        case CONTENT_TYPE_PLAYLIST: return "i.ytimg.com";
        case CONTENT_TYPE_CHANNEL: return "yt3.ggpht.com";
        default: break;
    }

    return NULL;
}

char* content_type_to_text (const ContentType content_type)
{
    switch (content_type) {
        case CONTENT_TYPE_VIDEO: return "VIDEO";
        case CONTENT_TYPE_CHANNEL: return "CHANNEL";
        case CONTENT_TYPE_PLAYLIST: return "PLAYLIST";
        case CONTENT_TYPE_LIVE: return "LIVE";
        case CONTENT_TYPE_ANY: return "ANY";
    }

    return NULL;
}

char* sort_parameter_to_text (const SortParameter sort_parameter)
{
    switch (sort_parameter) {
        case SORT_PARAM_RELEVANCE: return "RELEVANCE";
        case SORT_PARAM_UPLOAD_DATE: return "UPLOAD DATE";
        case SORT_PARAM_VIEW_COUNT: return "VIEWS"; 
        case SORT_PARAM_RATING: return "RATING";
    }

    return NULL;
}

// Draw text using font inside rectangle limits with support for text selection
void DrawTextBoxedSelectable(Font font, const char *text, Rectangle rec, float fontSize, float spacing, bool wordWrap, Color tint, int selectStart, int selectLength, Color selectTint, Color selectBackTint)
{
    int length = TextLength(text);  // Total length in bytes of the text, scanned by codepoints in loop

    float textOffsetY = 0;          // Offset between lines (on line break '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/(float)font.baseSize;     // Character rectangle scaling factor

    // Word/character wrapping mechanism variables
    enum { MEASURE_STATE = 0, DRAW_STATE = 1 };
    int state = wordWrap? MEASURE_STATE : DRAW_STATE;

    int startLine = -1;         // Index where to begin drawing (where a line begins)
    int endLine = -1;           // Index where to stop drawing (where a line ends)
    int lastk = -1;             // Holds last value of the character position

    for (int i = 0, k = 0; i < length; i++, k++)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepoint(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        // NOTE: Normally we exit the decoding sequence as soon as a bad byte is found (and return 0x3f)
        // but we need to draw all of the bad bytes using the '?' symbol moving one byte
        if (codepoint == 0x3f) codepointByteCount = 1;
        i += (codepointByteCount - 1);

        float glyphWidth = 0;
        if (codepoint != '\n')
        {
            glyphWidth = (font.glyphs[index].advanceX == 0) ? font.recs[index].width*scaleFactor : font.glyphs[index].advanceX*scaleFactor;

            if (i + 1 < length) glyphWidth = glyphWidth + spacing;
        }

        // NOTE: When wordWrap is ON we first measure how much of the text we can draw before going outside of the rec container
        // We store this info in startLine and endLine, then we change states, draw the text between those two variables
        // and change states again and again recursively until the end of the text (or until we get outside of the container).
        // When wordWrap is OFF we don't need the measure state so we go to the drawing state immediately
        // and begin drawing on the next line before we can get outside the container.
        if (state == MEASURE_STATE)
        {
            // TODO: There are multiple types of spaces in UNICODE, maybe it's a good idea to add support for more
            // Ref: http://jkorpela.fi/chars/spaces.html
            if ((codepoint == ' ') || (codepoint == '\t') || (codepoint == '\n')) endLine = i;

            if ((textOffsetX + glyphWidth) > rec.width)
            {
                endLine = (endLine < 1)? i : endLine;
                if (i == endLine) endLine -= codepointByteCount;
                if ((startLine + codepointByteCount) == endLine) endLine = (i - codepointByteCount);

                state = !state;
            }
            else if ((i + 1) == length)
            {
                endLine = i;
                state = !state;
            }
            else if (codepoint == '\n') state = !state;

            if (state == DRAW_STATE)
            {
                textOffsetX = 0;
                i = startLine;
                glyphWidth = 0;

                // Save character position when we switch states
                int tmp = lastk;
                lastk = k - 1;
                k = tmp;
            }
        }
        else
        {
            if (codepoint == '\n')
            {
                if (!wordWrap)
                {
                    textOffsetY += (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
                    textOffsetX = 0;
                }
            }
            else
            {
                if (!wordWrap && ((textOffsetX + glyphWidth) > rec.width))
                {
                    textOffsetY += (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
                    textOffsetX = 0;
                }

                // When text overflows rectangle height limit, just stop drawing
                if ((textOffsetY + font.baseSize*scaleFactor) > rec.height) break;

                // Draw selection background
                bool isGlyphSelected = false;
                if ((selectStart >= 0) && (k >= selectStart) && (k < (selectStart + selectLength)))
                {
                    DrawRectangleRec((Rectangle){ rec.x + textOffsetX - 1, rec.y + textOffsetY, glyphWidth, (float)font.baseSize*scaleFactor }, selectBackTint);
                    isGlyphSelected = true;
                }

                // Draw current character glyph
                if ((codepoint != ' ') && (codepoint != '\t'))
                {
                    DrawTextCodepoint(font, codepoint, (Vector2){ rec.x + textOffsetX, rec.y + textOffsetY }, fontSize, isGlyphSelected? selectTint : tint);
                }
            }

            if (wordWrap && (i == endLine))
            {
                textOffsetY += (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
                textOffsetX = 0;
                startLine = endLine;
                endLine = -1;
                glyphWidth = 0;
                selectStart += lastk - k;
                k = lastk;

                state = !state;
            }
        }

        if ((textOffsetX != 0) || (codepoint != ' ')) textOffsetX += glyphWidth;  // avoid leading spaces
    }
}

// Draw text using font inside rectangle limits
void DrawTextBoxed(Font font, const char *text, Rectangle rec, float fontSize, float spacing, bool wordWrap, Color tint)
{
    DrawTextBoxedSelectable(font, text, rec, fontSize, spacing, wordWrap, tint, 0, 0, WHITE, WHITE);
}

// applies some padding to a rectangle
Rectangle padded_rectangle (const float padding, const Rectangle rect)
{
    return (Rectangle) { rect.x + padding, rect.y + padding, rect.width - padding, rect.height - padding };
}

typedef struct ThumbnailData {
    char id[256];
    MemoryBlock data;
    struct ThumbnailData *next;
} ThumbnailData;

typedef struct {
    int count;
    ThumbnailData *head;
    ThumbnailData *tail;  
    pthread_mutex_t mutex;
} ThumbnailList;

typedef struct CachedThumbnailNode {
    char id[64];
    Timer lifespan;
    Texture2D texture;
    struct CachedThumbnailNode *next;
} CachedThumbnailNode;

typedef struct {
    int count;
    CachedThumbnailNode *head;
    CachedThumbnailNode *tail;
} CachedThumbnailList;

CachedThumbnailList cached_thumbnails;

Texture2D texture_cpy (const Texture2D src)
{
    Image image = LoadImageFromTexture(src);
    if (IsImageReady(image)) {
        Texture2D ret = LoadTextureFromImage(image);
        UnloadImage(image);
        return ret;
    }
    else 
        printf("texture_cpy: image data is invalid\n");
    return (Texture2D){ 0 };
}

CachedThumbnailNode *get_cached_node_by_id (const char *id, CachedThumbnailList *thumbnail_list)
{
    for (CachedThumbnailNode *node = thumbnail_list->head; node; node = node->next) {
        if (strcmp(node->id, id) == 0)
            return node;
    }
    return NULL;
}

void add_cached_thumbnail (CachedThumbnailNode *node, CachedThumbnailList *list)
{
    if (list->head == NULL) {
        list->head = list->tail = node;
    }
    else {
        node->next = NULL;
        list->tail->next = node;
        list->tail = list->tail->next;
    }
    list->count++;
}

void delete_cached_thumbnails (CachedThumbnailList *list) 
{
    CachedThumbnailNode *current = list->head;
    CachedThumbnailNode *prev = NULL;

    while (current) {
        if (is_timer_done(current->lifespan)) {
            // printf("cache entry expired id: %s\n", current->id);
            
            // update pointers
            if (!prev) 
                list->head = current->next;
            else    
                prev->next = current->next;

            if (current == cached_thumbnails.tail) 
                cached_thumbnails.tail = prev;

            // unload node info
            if (IsTextureReady(current->texture))
                UnloadTexture(current->texture);
            free(current);
            cached_thumbnails.count--;
            break;
        }
        else {
            prev = current;
            current = current->next;
        }
    }
}

ThumbnailList create_thumbnail_list ()
{
    ThumbnailList tl;
    tl.head = tl.tail = NULL;
    pthread_mutex_init(&tl.mutex, NULL);
    return tl;
}

typedef struct {
    char link[256];
    char id[256];
    Query query;
    ContentType type;
    YoutubeSearchList* search_results;
    ThumbnailList *thumbnail_list;
} ThreadArgs;

#define MAX_THREADS 4
pthread_t thread_pool[MAX_THREADS];
int current_thread = 0;

void add_thread_to_pool (void*(*thread_funct)(void*), void *args) 
{
    pthread_create(&thread_pool[current_thread], NULL, thread_funct, args);
    pthread_detach(thread_pool[current_thread]);
    current_thread = bound_index_to_array((current_thread + 1), MAX_THREADS);
}

void add_thumbnail_node (ThumbnailData *node, ThumbnailList *list) 
{
    if (list->head == NULL) {
        list->head = list->tail = node;
    }
    else {
        node->next = NULL;
        list->tail->next = node;
        list->tail = list->tail->next;
    }
    list->count++;
}

void draw_thumbnail_subtext (const Rectangle container, const Font font, const Color text_color, const int font_size, const int spacing, const int padding, const char* text)
{
    const Vector2 text_size = MeasureTextEx(font, text, font_size, spacing);
    const float width = text_size.x + (padding * 2);
    const float height = text_size.y + (padding * 2);
    const Rectangle length_area = {
        container.x + container.width - width - padding,
        container.y + container.height - height - padding,
        width,
        height
    };

    DrawRectangleRec(length_area, Fade(BLACK, 0.7));
    DrawTextBoxed(font, text, padded_rectangle(padding, length_area), font_size, spacing, true, text_color);
}

bool search_finished = true;
bool searching = false;
bool delete_old_search_results = false;

void create_file_from_memory(const char* filename, const char* memory, const size_t nbytes) 
{
    FILE* fp = fopen(filename, "wb");
    if (!fp) printf("could not write memory into \"%s\"\n", filename);
    else {
        fwrite(memory, 1, nbytes, fp);
        fclose(fp);
    } 
}

void extract_yt_data(MemoryBlock *html) 
{
    const char* needle = "itemSectionRenderer";
    const char* location = strstr(html->memory, needle);
    if (!location) {
        printf("extract_yt_data2: \"%s\" was not found in html argument\n", needle);
        return;
    }

    // move pointer to the first '{' after the needle
    const char* start = strchr(location, '{');
    if (!start) {
        printf("extract_yt_data2: Couldn't find starting '{'\n");
        return;
    }

    const char* current = start;
    int depth = 0;
    while (*current) {
        if (*current == '{') depth++;
        else if (*current == '}') depth--;
        current++;

        if (depth == 0) break;
    }

    if (depth != 0) {
        printf("extract_yt_data: JSON object appears to be unbalanced\n");
        return;
    }
    
    // alter the data in the memory block in place

    const char *end = current;
    const size_t nchars = end - start + 1;
    
    // write valid data over the beginning portion 
    memcpy(html->memory, start, nchars);

    // free redundant memory after this block 
    html->memory = realloc(html->memory, nchars + 1);
    if (!html->memory) {
        perror("realloc failed");
        exit(EXIT_FAILURE);
    }

    html->memory[nchars] = '\0';
}

clock_t start_search_time;
clock_t end_search_time;

bool is_chunked_encoding(const char* response)
{
    return strstr(response, "Transfer-Encoding: chunked");
}

// return the number of bytes written from src to a memory block
size_t write_data_to_memory_block(const void* src, const size_t nbytes, MemoryBlock* chunk)
{
    // allocate space to hold new data
    char* new_memory = realloc(chunk->memory, (chunk->size + nbytes + 1));
    if (!new_memory) {
        printf("write_data_to_memory_block: not enough memory, realloc returned null\n");
        return 0;
    }

    // update params
    chunk->memory = new_memory;
    memcpy(&chunk->memory[chunk->size], src, nbytes);
    chunk->size += nbytes;

    return nbytes;
}

size_t get_content_len (const char *header_response)
{
    // find the content length parameter
    char *location = strstr(header_response, "Content-Length:");
    if (location) {
        // find the first numeric char
        char *first_numeric = location;
        while (first_numeric && !isdigit(*first_numeric)) {
            first_numeric++;
        } 

        // read every numeric char into a buffer
        int i = 0;
        char bytes[16] = {0};
        while (first_numeric && isdigit(*first_numeric)) {
            bytes[i++] = *first_numeric;
            first_numeric++;
        }

        // return numeric representation
        return atoi(bytes);
    }

    return 0;
}

// reads until a line or buffer end from ssl read stream
size_t ssl_read_line (SSL *ssl, char *buff, const size_t n) 
{
    if (!buff) {
        printf("ssl_read_line: buffer is NULL\n");
        return 0;
    }

    memset(buff, 0, n);
    
    const char *line_end = "\r\n";
    const size_t line_end_length = strlen(line_end);

    size_t pos = 0;
    char c;

    while (pos < n - 1) {
        // read one char
        int byte = SSL_read(ssl, &c, 1);
        if (byte <= 0) {
            printf("ssl_read_line: SSL_read returned %d\n", byte);
            return 0;
        }

        // add character to buffer
        buff[pos++] = c;

        // checking if we've reached end of line
        if ((pos >= line_end_length) && strstr(buff, line_end)) {
            break;
        }
    }

    buff[pos] = '\0';
    return pos;
}

// read up to n bytes of the header of a http response into some buffer
size_t read_header(SSL *ssl, char *buff, size_t n)
{
    size_t total_len = 0;
    const char *header_end = "\r\n\r\n";

    // until the end of a header is not found in the buffer, continue to read lines
    while ((strstr(buff, header_end) == NULL) && (total_len < n - 1)) {
        size_t line_len = ssl_read_line(ssl, (buff + total_len), (n - total_len));
        if (line_len == 0) {
            printf("read_header: read_line returned 0 bytes read\n");
            break;  
        }

        total_len += line_len;
    }   

    return total_len;
}

// writes up to n bytes to a memory block object from ssl read stream
void ssl_read_n (SSL *ssl, MemoryBlock *dst, const size_t n)
{
    size_t bytes_remaining = n;
    while (bytes_remaining > 0) {
        // find how many bytes are to be read and process into temp buffer 
        char buffer[4096] = {0};
        const size_t bytess_to_read = bytes_remaining < sizeof(buffer) - 1 ? bytes_remaining : sizeof(buffer) - 1;
        const int bytes_read = SSL_read(ssl, buffer, bytess_to_read);
        if (bytes_read < 0) {
            printf("ssl_read_end: error with SSL_read\n");
            break;
        }

        // read the processed data into the memory block object
        write_data_to_memory_block(buffer, bytes_read, dst);
        bytes_remaining -= bytes_read;
    }       
}

bool offline = false;
SSL_CTX *ctx = NULL;
struct addrinfo *cached_addr = NULL;

MemoryBlock fetch_url(const char *host, const char *path, const char *port, const char *debug_filename)
{
    // only retry connection if application is offline
    if (offline) {
        struct addrinfo desired_addr_info = {0};
        desired_addr_info.ai_family = AF_UNSPEC;
        desired_addr_info.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, port, &desired_addr_info, &cached_addr) != 0) {
            perror("getaddrinfo");
            printf("fetch_url: failed reconnection attempt using getaddrinfo\n");
            return (MemoryBlock){0};
        }
    }

    // initializing socket
    int sockfd = socket(cached_addr->ai_family, cached_addr->ai_socktype, cached_addr->ai_protocol);
    if (connect(sockfd, cached_addr->ai_addr, cached_addr->ai_addrlen) != 0) {
        printf("fetch_url: issue with establishing socket connection\n");
        close(sockfd);
        return (MemoryBlock){0};
    }

    // initializing ssl
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sockfd);
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        close(sockfd);
        return (MemoryBlock){0};
    }

    // constructing request
    char request[512];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
        "Connection: closed\r\n"
        "\r\n",
        path, host);
    
    // sending request
    int write_status;
    if ((write_status = SSL_write(ssl, request, strlen(request))) <= 0) {
        SSL_get_error(ssl, write_status);
        SSL_free(ssl);
        close(sockfd);
        return (MemoryBlock){0};
    } 

    MemoryBlock http_response = (MemoryBlock){0};

    // read header of GET request to see the charateristics of the response
    char header[4096] = {0};
    size_t header_len = header_len = read_header(ssl, header, sizeof(header));
    header[header_len] = '\0';
    if (header_len == 0) {
        printf("fetch_url: read_header data is invalid\n");
        return (MemoryBlock){0};
    }

    // read the response body, the response is either chunk encoded, or contains its size in a label called 'Connection-Length'
    size_t content_len = get_content_len(header);
    if (content_len > 0) {
        ssl_read_n(ssl, &http_response, content_len);
    }

    else if (is_chunked_encoding(header)) {
        const char *CRLF = "\r\n";
        const size_t CRLF_len = strlen(CRLF);
        
        size_t chunk_size = 1; 
        while (chunk_size != 0) {
            // read chunk size line
            char hex[16] = {0};
            int len = ssl_read_line(ssl, hex, sizeof(hex));
            if (len <= 0) {
                printf("fetch_url: failed to read chunk size\n");
                break;
            }

            // parse hex
            if (len >= CRLF_len) {
                hex[len - CRLF_len] = '\0';
            }
            chunk_size = strtoul(hex, NULL, 16);
            
            // read 'chunk_size' bytes in memory block
            ssl_read_n(ssl, &http_response, chunk_size);
                
            // absorb trailing CRLF from ssl read stream
            char trailing[16];
            ssl_read_line(ssl, trailing, sizeof(trailing));
        }
    }
    
    // deinit
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sockfd);

    return http_response;
}

// returns an allocated string that is the URL encoding of the argument passed
char *url_encode(const char *str)
{
    if (!str) {
        printf("url_encode: arguement is NULL");
        return NULL;
    }

    const size_t str_len = strlen(str);
    const size_t url_enconde_len = 3;

    // worst case senario is when all characters are url encoded
    char *encoded_str = malloc((str_len * 3) + 1);
    char *ptr = encoded_str;

    for (int i = 0; i < str_len; i++) {
        char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *ptr++ = c;
        }
        // every non alpha character is replace with a % and 2 hex digits
        else {
            sprintf(ptr, "%%%02x", c);
            ptr += url_enconde_len;
        }
    }

    *ptr = '\0';
    return encoded_str;
}

void *load_thumbnail(void *args)
{
    ThreadArgs *targs = (ThreadArgs *) args;
    MemoryBlock chunk = fetch_url(content_type_to_host(targs->type), targs->link, "443", NULL);
    
    if (is_memory_ready(chunk)) {
        ThumbnailData *thumbnail_data = malloc(sizeof(ThumbnailData));
        if (thumbnail_data) {
            thumbnail_data->data = chunk;
            strcpy(thumbnail_data->id, targs->id);
            thumbnail_data->next = NULL;
            
            pthread_mutex_lock(&targs->thumbnail_list->mutex);
            add_thumbnail_node(thumbnail_data, targs->thumbnail_list);
            pthread_mutex_unlock(&targs->thumbnail_list->mutex);
        }
        else {
            printf("load_thumbnail: malloc returned NULL for thumbnail_data\n");
        }
    }
    else {
        printf("load_thumbnail: fetched data is not valid\n");
    }
    
    free(targs);
    return NULL;
}

// writes a list of search result nodes to some list
int elements_added = 0;
void* get_results_from_query(void* args)
{
    search_finished = false;
    searching = true;

    ThreadArgs* targs = (ThreadArgs *) args;

    printf("query \"%s\"\n", targs->query.url_encoded_query);

    // append the query to the yt query string
    char path[512] = "\0";
    configure_search_url(512, path, targs->query);

    // get the page source of this url
    MemoryBlock http_response = fetch_url("www.youtube.com", path, "443", NULL);
    
    offline = (is_memory_ready(http_response) == false);
    if (offline) {
        printf("get_results_from_query: fetch_url returned invalid\n");
        searching = false;
        search_finished = true;
        free(targs);
        return NULL;
    }
    
    extract_yt_data(&http_response);
    if (!is_memory_ready(http_response)) {
        printf("get_results_from_query: extracting yt data corrupted memory\n");
        searching = false;
        search_finished = true;
        free(targs);
        return NULL;
    }

    // get json obj
    cJSON* search_json = cJSON_Parse(http_response.memory);
    unload_memory_block(&http_response);
    if (!search_json) {
        printf("get_results_from_query: cJSON_Parse returned NULL\n");
        searching = false;
        search_finished = true;
        free(targs);
        return NULL;
    }

    elements_added = 0;
    cJSON *contents = cJSON_GetObjectItem(search_json, "contents");
    if (contents && cJSON_IsArray(contents)) {
        // loop through every item and get the node equivalent 
        cJSON *item;
        cJSON_ArrayForEach (item, contents) {
            YoutubeSearchNode node = { 0 };
            cJSON *channelRenderer = cJSON_GetObjectItem(item, "channelRenderer");
            cJSON *videoRenderer = cJSON_GetObjectItem(item, "videoRenderer");
            cJSON *lockupViewModel = cJSON_GetObjectItem(item, "lockupViewModel");

            if (videoRenderer) {
                // check if the video is a short, i fucking hate yt shorts...
                if (!targs->query.allow_shorts) {
                    bool yt_short = false;
                    cJSON *navigationEndpoint = cJSON_GetObjectItem(videoRenderer, "navigationEndpoint");
                    cJSON *commandMetadata = navigationEndpoint ? cJSON_GetObjectItem(navigationEndpoint, "commandMetadata") : NULL;
                    cJSON *webCommandMetadata = commandMetadata ? cJSON_GetObjectItem(commandMetadata, "webCommandMetadata") : NULL;
                    cJSON *url = webCommandMetadata ? cJSON_GetObjectItem(webCommandMetadata, "url") : NULL;
                    if (url && cJSON_IsString(url)) {
                        yt_short = strstr(url->valuestring, "/shorts");
                    }

                    if (yt_short) {
                        continue;
                    }
                }

                node.type = CONTENT_TYPE_VIDEO;

                // video id
                cJSON *videoId = cJSON_GetObjectItem(videoRenderer, "videoId");
                if (videoId && cJSON_IsString(videoId)) {
                    strcpy(node.id, videoId->valuestring);
                }
                
                // video title
                cJSON *title = cJSON_GetObjectItem(videoRenderer, "title");
                cJSON* title_runs = title ? cJSON_GetObjectItem(title, "runs") : NULL;
                if (title_runs && cJSON_IsArray(title_runs)) {
                    cJSON* first_run = cJSON_GetArrayItem(title_runs, 0);
                    if (first_run) {
                        cJSON *text = cJSON_GetObjectItem(first_run, "text");
                        if (text && cJSON_IsString(text)) {
                            strcpy(node.title, text->valuestring);
                        }
                    }
                }

                // thumbnail link
                snprintf(node.thumbnail_link, sizeof(node.thumbnail_link), "/vi/%s/mqdefault.jpg", node.id);

                // creator of video
                cJSON *ownerTextRuns = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "ownerText"), "runs");
                if (ownerTextRuns && cJSON_IsArray(ownerTextRuns)) {
                    cJSON *first_run = cJSON_GetArrayItem(ownerTextRuns, 0);
                    if (first_run) {
                        cJSON *text = cJSON_GetObjectItem(first_run, "text");
                        if (text && cJSON_IsString(text)) {
                            strcpy(node.author, text->valuestring);
                        }
                    }
                }

                // checking if live video
                cJSON *viewCountTextRuns = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "viewCountText"), "runs");
                if (viewCountTextRuns && cJSON_IsArray(viewCountTextRuns)) {

                    cJSON *first_element = cJSON_GetArrayItem(viewCountTextRuns, 0);
                    cJSON *text = first_element ? cJSON_GetObjectItem(first_element, "text") : NULL;
                    if (text && cJSON_IsString(text)) {
                        format_youtube_views(text->valuestring, sizeof(node.views), node.views);
                    }
                    node.is_live = true;
                }
                
                // normal video
                else {
                    cJSON *viewCountTextSimple = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "viewCountText"), "simpleText");
                    if (viewCountTextSimple && cJSON_IsString(viewCountTextSimple)) {
                        format_youtube_views(viewCountTextSimple->valuestring, sizeof(node.views), node.views);
                    }
                    node.is_live = false;
                }

                // publish date
                cJSON *publishedTimeText = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "publishedTimeText"), "simpleText");
                if (publishedTimeText && cJSON_IsString(publishedTimeText)) {
                    strcpy(node.date, publishedTimeText->valuestring);
                }

                // video length
                cJSON *lengthText = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "lengthText"), "simpleText");
                if (lengthText && cJSON_IsString(lengthText)) {
                    strcpy(node.length, lengthText->valuestring);
                }
            }

            else if (channelRenderer) {
                node.type = CONTENT_TYPE_CHANNEL;

                // channel id
                cJSON* channelId = cJSON_GetObjectItem(channelRenderer, "channelId");
                if (channelId && cJSON_IsString(channelId)) {
                    strcpy(node.id, channelId->valuestring);
                }

                // channel title
                cJSON *title = cJSON_GetObjectItem(cJSON_GetObjectItem(channelRenderer, "title"), "simpleText");
                if (title && cJSON_IsString(title)) {
                    strcpy(node.title, title->valuestring);
                }

                // subscriber count
                cJSON *subCount = cJSON_GetObjectItem(cJSON_GetObjectItem(channelRenderer, "videoCountText"), "simpleText");
                if(subCount && cJSON_IsString(subCount)) {
                    strcpy(node.subs, subCount->valuestring);
                }

                // thumbnail link
                cJSON *thumbnails = cJSON_GetObjectItem(cJSON_GetObjectItem(channelRenderer, "thumbnail"), "thumbnails");
                if (thumbnails && cJSON_IsArray(thumbnails)) {
                    cJSON *first_thumbnail = cJSON_GetArrayItem(thumbnails, 0);
                    cJSON *url = first_thumbnail ? cJSON_GetObjectItem(first_thumbnail, "url") : NULL;
                    if(url && cJSON_IsString(url)) {
                        char *potential_channel_url = strstr(url->valuestring,"/ytc");
                        if (!potential_channel_url) {
                            potential_channel_url = strrchr(url->valuestring, '/');
                        } 
                        strcpy(node.thumbnail_link, potential_channel_url);
                    }
                }
            }

            else if (lockupViewModel) {
                node.type = CONTENT_TYPE_PLAYLIST;

                // playlist id
                cJSON *contentId = cJSON_GetObjectItem(lockupViewModel, "contentId");
                if (contentId && cJSON_IsString(contentId)) {
                    strcpy(node.id, contentId->valuestring);
                }

                // playlist title
                cJSON *metadata = cJSON_GetObjectItem(lockupViewModel, "metadata");
                cJSON *lockupMetadataViewModel = metadata ? cJSON_GetObjectItem(metadata, "lockupMetadataViewModel") : NULL;
                cJSON *title = lockupMetadataViewModel ? cJSON_GetObjectItem(lockupMetadataViewModel, "title") : NULL;
                cJSON *content = title ? cJSON_GetObjectItem(title, "content") : NULL;
                if (content && cJSON_IsString(content)) {
                    strcpy(node.title, content->valuestring);
                }

                cJSON *contentImage = cJSON_GetObjectItem(lockupViewModel, "contentImage");
                cJSON *collectionThumbnailViewModel = contentImage ? cJSON_GetObjectItem(contentImage, "collectionThumbnailViewModel") : NULL;
                cJSON *primaryThumbnail = collectionThumbnailViewModel ? cJSON_GetObjectItem(collectionThumbnailViewModel, "primaryThumbnail") : NULL;
                cJSON *thumbnailViewModel = primaryThumbnail ? cJSON_GetObjectItem(primaryThumbnail, "thumbnailViewModel") : NULL;

                // playlist thumbnail
                cJSON *image = thumbnailViewModel ? cJSON_GetObjectItem(thumbnailViewModel, "image") : NULL;
                cJSON *sources = image ? cJSON_GetObjectItem(image, "sources") : NULL;
                if (sources && cJSON_IsArray(sources)) {
                    cJSON* first_source = cJSON_GetArrayItem(sources, 0);
                    cJSON *url = first_source ? cJSON_GetObjectItem(first_source, "url") : NULL;
                    if (url && cJSON_IsString(url)) {
                        strcpy(node.thumbnail_link, strstr(url->valuestring, "/vi"));
                    }
                }

                // number of videos in playlist
                cJSON *overlays = thumbnailViewModel ? cJSON_GetObjectItem(thumbnailViewModel, "overlays") : NULL;
                cJSON *overlay;
                if (overlays && cJSON_IsArray(overlays)) {
                    cJSON_ArrayForEach (overlay, overlays) {
                        cJSON *thumbnailOverlayBadgeViewModel = cJSON_GetObjectItem(overlay, "thumbnailOverlayBadgeViewModel");
                        cJSON *thumbnailBadges = thumbnailOverlayBadgeViewModel ? cJSON_GetObjectItem(thumbnailOverlayBadgeViewModel, "thumbnailBadges") : NULL;
                        if (thumbnailBadges && cJSON_IsArray(thumbnailBadges)) {
                            cJSON *thumbnailBadge;
                            cJSON_ArrayForEach (thumbnailBadge, thumbnailBadges) {
                                cJSON *thumbnailBadgeViewModel = cJSON_GetObjectItem(thumbnailBadge, "thumbnailBadgeViewModel");
                                if (thumbnailBadgeViewModel) {
                                    cJSON *text = cJSON_GetObjectItem(thumbnailBadgeViewModel, "text");
                                    if (text && cJSON_IsString(text)) {
                                        strcpy(node.video_count, text->valuestring);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if ((node.id[0] != '\0')) {
                elements_added++;

                // get cached thumbnail, if it exists
                CachedThumbnailNode *cached_thumbnail = get_cached_node_by_id(node.id, &cached_thumbnails);
                if (cached_thumbnail) {
                    node.thumbnail = cached_thumbnail->texture;
                }
                
                // append node to search results
                add_node(targs->search_results, node);

                // only initalize thread if the texture is not found in the cache
                if (!IsTextureReady(node.thumbnail)) {
                    // create thread arguement
                    ThreadArgs *load_thumbnail_args = malloc(sizeof(ThreadArgs));
                    strcpy(load_thumbnail_args->link, node.thumbnail_link);
                    strcpy(load_thumbnail_args->id, node.id);
                    load_thumbnail_args->thumbnail_list = targs->thumbnail_list;
                    load_thumbnail_args->type = node.type;
                    
                    // add to pool 
                    add_thread_to_pool(load_thumbnail, load_thumbnail_args);
                }
            }
        }
    }

    cJSON_Delete(search_json);

    search_finished = true;
    searching = false;
    delete_old_search_results = true;
    end_search_time = clock();

    free(targs);
    
    printf("%d elements found\n", elements_added);
    printf("search process took %f seconds\n", (end_search_time - start_search_time) / (CLOCKS_PER_SEC * 1.0f) * 10);
    return NULL;
}

bool is_whitespace (const char c)
{
    return (c == ' ') || (c == '\n') || (c == '\t');
}

void sanitize_query (char *query)
{
    if (!query) {
        printf("sanitize_query: 'query' arguement is NULL\n");
        return;
    }

    char *first_non_whitespace = query;
    for (; first_non_whitespace && is_whitespace(*first_non_whitespace); first_non_whitespace++)
        ;
    if (*first_non_whitespace != '\0') {
        const size_t n = strlen(query);
        
        char *last_non_whitespace = query + n - 1;
        for (; last_non_whitespace && is_whitespace(*last_non_whitespace); last_non_whitespace--)
            ;

        int nchars = last_non_whitespace - first_non_whitespace + 1;
        memmove(query, first_non_whitespace, nchars);
        query[nchars] = '\0';
    }
    else {
        query[0] = '\0';
    }
}

int main()
{
    struct addrinfo desired_addr_info = { 0 };
    desired_addr_info.ai_family = AF_UNSPEC;
    desired_addr_info.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("www.youtube.com", "443", &desired_addr_info, &cached_addr) != 0) {
        printf("main: failed inital connection attempt using getaddrinfo\n");
        offline = true;
    }

    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        printf("main: error initalizing SSL_CTX object\n");
    } 

    // list containing the search results from a query
    YoutubeSearchList search_results = create_youtube_search_list();

    // list containing the image data of thumbails from a search
    ThumbnailList thumbnail_list = create_thumbnail_list();

    cached_thumbnails.head = cached_thumbnails.tail = NULL;
    cached_thumbnails.count = 0;

    // init app
    SetTargetFPS(60);
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1000, 750, "metube");

    const Font FONT = GetFontDefault();
    
    // flag used to start search thread
    bool search = false;
    
    // object containing elements required for the search
    // the string the user passes in the textbox
    // how the results shall be sorted
    // what type of items the user is looking for (videos, channels, playlists, etc.) 
    Query query = {0};

    // flag determining "GuiTextBox" functionality
    bool edit_mode = false;
    
    // data for the filter window
    bool show_filter_window = false;
    
    int current_type = 0;
    ContentType availible_types[] = { 
        CONTENT_TYPE_ANY, 
        CONTENT_TYPE_VIDEO, 
        CONTENT_TYPE_CHANNEL, 
        CONTENT_TYPE_PLAYLIST,
        CONTENT_TYPE_LIVE,
    };
    
    int current_sort = 0;
    SortParameter availible_sorts[] = {
        SORT_PARAM_RELEVANCE,
        SORT_PARAM_UPLOAD_DATE,
        SORT_PARAM_VIEW_COUNT,
        SORT_PARAM_RATING 
    };

    // for scroll panel
    Vector2 scroll = { 10, 10 };
    Rectangle scrollView = { 0, 0 };

    char search_buffer[256] = {0};

    while (!WindowShouldClose()) {
        // deleting cached thumbnails after they've been unused for CACHED_THUMBNAIL_LIFETIME seconds 
        delete_cached_thumbnails(&cached_thumbnails);

        // loading thumbnails from data list
        if (pthread_mutex_trylock(&thumbnail_list.mutex) == 0) {
            while (thumbnail_list.head) {
                ThumbnailData *thumbnail_data = thumbnail_list.head;
                
                // Find matching search node and load texture
                for (YoutubeSearchNode *search_node = search_results.head; search_node; search_node = search_node->next) {
                    if (strcmp(thumbnail_data->id, search_node->id) == 0) {
                        // clear texture data (if any)
                        if (IsTextureReady(search_node->thumbnail)) {
                            UnloadTexture(search_node->thumbnail);
                        }

                        search_node->thumbnail = get_thumbnail_from_memory(thumbnail_data->data, 150, 80);

                        if (IsTextureReady(search_node->thumbnail)) {
                            // add new texture to cache
                            CachedThumbnailNode *cached_thumbnail_node = malloc(sizeof(CachedThumbnailNode));
                            cached_thumbnail_node->texture = search_node->thumbnail;
                            strcpy(cached_thumbnail_node->id, search_node->id);
                            cached_thumbnail_node->next = NULL;
                            start_timer(&cached_thumbnail_node->lifespan, CACHED_THUMBNAIL_LIFETIME);
                            
                            add_cached_thumbnail(cached_thumbnail_node, &cached_thumbnails);
                        }
                        break;
                    }
                }
                
                // Remove processed thumbnail data
                thumbnail_list.head = thumbnail_list.head->next;
                unload_memory_block(&thumbnail_data->data);
                free(thumbnail_data);
            }
            pthread_mutex_unlock(&thumbnail_list.mutex);
        }

        if (searching) {
            SetWindowTitle(TextFormat("[%s(loading)] - metube", query.url_encoded_query));
        }
        
        else if (offline) {
            SetWindowTitle("[offline] - metube");
        }

        else if (search_finished && search_results.count > 0) {
            SetWindowTitle(TextFormat("[search results(%d)] - metube", elements_added));
        }

        if (search) {
            search = false;

            // configure thread arguements for routine
            ThreadArgs *targs = malloc(sizeof(ThreadArgs));
            targs->query = query;
            targs->search_results = &search_results;
            targs->thumbnail_list = &thumbnail_list;

            // get the results of this query in this thread
            pthread_t thread;
            pthread_create(&thread, NULL, get_results_from_query, targs);
            pthread_detach(thread);

            search_buffer[0] = '\0';
	    }
        
        if (delete_old_search_results) {
            delete_old_search_results = false;

            // find out how many old elements there are
            int old_elements = search_results.count - elements_added;
            
            // delete old nodes
            for (int i = 0; (i < old_elements) && (search_results.head); i++) {
                YoutubeSearchNode *node = search_results.head;
                search_results.head = search_results.head->next;
                search_results.count--;
                free(node);
            }
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);

            // the space elements gives one another
            const int padding = 5;
            
            // searching UI
            const Rectangle search_bar = { padding, padding, 350, 25 };
            const Rectangle search_button = { (search_bar.x + search_bar.width + padding), search_bar.y, 50, 25 };
 
            // toggle edit mode engaging or leaving the text box window
            // pressing enter returns 1 and clicking outside of this window returns 2
            int text_box_status; 
            if ((text_box_status = GuiTextBox(search_bar, search_buffer, sizeof(search_buffer), edit_mode))) {
                edit_mode = !edit_mode;
            } 

            if (GuiButton(search_button, "SEARCH") || (text_box_status == 1)) {
                if (!edit_mode) {
                    sanitize_query(search_buffer);

                    // configure the query upon valid search entry
                    if (search_buffer[0] != '\0') {
                        char *buffer = url_encode(search_buffer);
                        if (buffer) {
                            strcpy(query.url_encoded_query, buffer);
                            free(buffer);
                        }
                        query.sort = availible_sorts[current_sort];
                        query.type = availible_types[current_type];
                        
                        // only search when last search is done 
                        search = (search_finished);
                        if (search) start_search_time = clock();
                    }
                }
            }

            // filtering UI
            const Rectangle filter_button = { 
                search_button.x + search_button.width + padding, 
                padding, 
                50, 
                25 
            };
            
            // toggle filter window on press
            if (GuiButton(filter_button, "FILTER")) show_filter_window = !show_filter_window;
            
            const Rectangle filter_window_area = { padding, (search_bar.y + search_bar.height + padding), search_bar.width, 75 };
            if (show_filter_window) {
                DrawRectangleLinesEx(filter_window_area, 1, GRAY);
                const int font_size = 11;

                // buttons to switch filter params (the type of content and how they will be sorted)
                const char* button_text = "SWITCH";
                const Rectangle sort_type_button = { (filter_window_area.x + filter_window_area.width - 55), (filter_window_area.y + padding), 50, 17.5 };
                const Rectangle content_type_button = { (filter_window_area.x + filter_window_area.width - 55), (sort_type_button.y + sort_type_button.height + padding), 50, 17.5 };
                const Rectangle toggle_yt_shorts_button = { (filter_window_area.x + filter_window_area.width - 55), (content_type_button.y + content_type_button.height + padding), 50, 17.5 };
                
                // update the index of filter params when pressed
                if (GuiButton(sort_type_button, button_text)) current_sort = bound_index_to_array((current_sort + 1), 4);
                if (GuiButton(content_type_button, button_text)) current_type = bound_index_to_array((current_type + 1), 5);
                if (GuiButton(toggle_yt_shorts_button, button_text)) query.allow_shorts = !query.allow_shorts;
                
                // filters availible
                DrawTextEx(FONT, "Order:", (Vector2){ filter_window_area.x + padding, sort_type_button.y + padding }, font_size, 2, BLACK);
                DrawTextEx(FONT, "Type:", (Vector2){ filter_window_area.x + padding, content_type_button.y + padding }, font_size, 2, BLACK);
                DrawTextEx(FONT, "Allow Shorts:", (Vector2){ filter_window_area.x + padding, toggle_yt_shorts_button.y + padding }, font_size, 2, BLACK);
                
                // current param value
                DrawTextEx(FONT, content_type_to_text(availible_types[current_type]), (Vector2){ ((filter_window_area.x + filter_window_area.width) * 0.4f), (content_type_button.y + padding) }, font_size, 2, BLACK);
                DrawTextEx(FONT, sort_parameter_to_text(availible_sorts[current_sort]), (Vector2){ ((filter_window_area.x + filter_window_area.width) * 0.4f), (sort_type_button.y + padding) }, font_size, 2, BLACK);
                DrawTextEx(FONT, (query.allow_shorts ? "YES" : "NO"), (Vector2){ ((filter_window_area.x + filter_window_area.width) * 0.4f), (toggle_yt_shorts_button.y + padding) }, font_size, 2, BLACK);
            }

            // display search results

            // the bound of the scroll panel
            const Rectangle scroll_panel_area = { 
                search_bar.x, 
                search_bar.y + search_bar.height + (show_filter_window ? (padding + filter_window_area.height) : 0) + padding, 
                search_bar.width, 
                GetScreenHeight() - scroll_panel_area.y - padding, 
            };
            
            // the area of the content drawn in the window
            const int content_height = 80;
            const Rectangle content_area = {
                scroll_panel_area.x,
                scroll_panel_area.y,
                scroll_panel_area.width,
                content_height * search_results.count,
            };

            // the width of the scrollbar is only felt when it's visible
            const int SCROLLBAR_WIDTH = content_area.height > scroll_panel_area.height ? 14 : 2;
            
            GuiScrollPanel(scroll_panel_area, NULL, content_area, &scroll, &scrollView);

            // clip drawings within the scroll panel
            BeginScissorMode((scroll_panel_area.x + 1), (scroll_panel_area.y + 1), scroll_panel_area.width, (scroll_panel_area.height - 2));
                // the y value of the ith rectangle to be drawn
                float y_level = scroll_panel_area.y;
                
                // for every search result, draw a container and display its data
                int i = 0;
                for (YoutubeSearchNode* search_result = search_results.head; search_result; search_result = search_result->next, i++, y_level += content_height) {
                    // area of the ith rectangle
                    const Rectangle content_rect = { 
                        padding, 
                        y_level + scroll.y, // scroll is added so moving the scrollbar offsets all elements
                        scroll_panel_area.width - SCROLLBAR_WIDTH,
                        content_height 
                    };

                    // extend the life of the cached node if texture is still in use
                    CachedThumbnailNode *cached_node = get_cached_node_by_id(search_result->id, &cached_thumbnails);
                    if (cached_node) {
                        start_timer(&cached_node->lifespan, CACHED_THUMBNAIL_LIFETIME);
                    }

                    // only process items that are onscreen
                    if (CheckCollisionRecs(content_rect, scroll_panel_area)) {
                        const Rectangle thumbnail_bounds = { 
                            content_rect.x, 
                            content_rect.y, 
                            content_rect.width * 0.45f, 
                            content_rect.height 
                        };

                        const Rectangle title_bounds = {
                            thumbnail_bounds.x + thumbnail_bounds.width,
                            content_rect.y,
                            content_rect.width - thumbnail_bounds.width,
                            content_rect.height * 0.75f
                        };

                        const Rectangle subtext_bounds = {
                            thumbnail_bounds.x + thumbnail_bounds.width,
                            title_bounds.y + title_bounds.height,
                            title_bounds.width,
                            content_rect.height - title_bounds.height
                        };

                        const int padding = 5;
                        const int font_size = 12;
                        const int spacing = 2;
                        const bool wrap_word = true; // words move to next line if there's enough space, rather than getting cut in half

                        Color background_color = (i % 2) ? WHITE : RAYWHITE;

                        // content backgound
                        DrawRectangleRec(content_rect, background_color);
                        
                        // thumbnail
                        if (IsTextureReady(search_result->thumbnail)) 
                            DrawTextureEx(search_result->thumbnail, (Vector2){ thumbnail_bounds.x, thumbnail_bounds.y }, 0.0f, 1.0f, RAYWHITE);

                        // title
                        DrawTextBoxed(FONT, search_result->title, padded_rectangle(padding, title_bounds), font_size, spacing, wrap_word, BLACK);
                        switch(search_result->type) {
                            case CONTENT_TYPE_VIDEO:
                                // if video -> "date published - <view count> views" 
                                // if live -> "<view count> watching" 
                                DrawTextBoxed(FONT, TextFormat("%s %s %s %s", (search_result->date[0] ? search_result->date : ""), (search_result->date[0] ? "-" : ""), search_result->views, search_result->is_live ? "watching" : "views"), 
                                            padded_rectangle(padding, subtext_bounds), font_size, spacing, wrap_word, BLACK);
                                draw_thumbnail_subtext(thumbnail_bounds, FONT, RAYWHITE, font_size, spacing, 5, (search_result->length[0] != '\0' ? search_result->length : "LIVE"));
                                break;
                            case CONTENT_TYPE_CHANNEL:
                                DrawTextBoxed(FONT, search_result->subs, padded_rectangle(padding, subtext_bounds), font_size, spacing, wrap_word, BLACK);
                                draw_thumbnail_subtext(thumbnail_bounds, FONT, RAYWHITE, font_size, spacing, padding, "CHANNEL");
                                break;
                            case CONTENT_TYPE_PLAYLIST:
                                draw_thumbnail_subtext(thumbnail_bounds, FONT, RAYWHITE, font_size, spacing, padding, search_result->video_count);
                                break;
                            default: break;
                        }
                    }
                }
            EndScissorMode();
            DrawFPS(GetScreenWidth() - 70, GetScreenHeight() - 20);
        EndDrawing();
    }

    // deinit app
    {
        EVP_cleanup();
        SSL_CTX_free(ctx);
        freeaddrinfo(cached_addr);
        
        if (search_results.count > 0) {
            unload_list(&search_results);
        }

        while (thumbnail_list.head) {
            ThumbnailData *node = thumbnail_list.head;
            thumbnail_list.head = thumbnail_list.head->next;
            unload_memory_block(&node->data);
            free(node);
        }

        pthread_mutex_destroy(&thumbnail_list.mutex);

        while (cached_thumbnails.head) {
            CachedThumbnailNode *node = cached_thumbnails.head;
            cached_thumbnails.head = cached_thumbnails.head->next;
            if (IsTextureReady(node->texture))
                UnloadTexture(node->texture);
            free(node);
        }

        CloseWindow();
        return 0;
    }
}

// to do
    // pagination 
    // clean code
    // show video information when double clicking video
    // clean everything
    // actually play video when pressed
    // cleanup when prematurley deleting
        // thumbnail list
        // search arguements

    // after everythings done:
        // persistent socket connection
        // fonts for L.O.T.E.

// for read me
    // -lssl -lcrypto -lcjson and raylib/raygui
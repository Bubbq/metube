
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// this contains all the functions needed to maintain and update lists in the program // 

typedef struct
{
    char thumbnail_path[256];
    char title[256];
    char id[64];
} MediaInfo;

void print_media_info(const MediaInfo* media_info)
{
    if (media_info == NULL) return;
    printf("ID:%s TITLE:%s PATH:%s\n", media_info->id, media_info->title, media_info->thumbnail_path);
}

typedef struct
{
    MediaInfo media_info;
    char author_id[64];
    char age[32];
    char duration[16];
    char view_count[16];
} VideoResult;

VideoResult* init_video_result()
{
    return calloc(1, sizeof(VideoResult));
}

void print_video_result(const VideoResult* video_result)
{
    if (video_result == NULL) return;
    print_media_info(&video_result->media_info);
    printf("date_published:%s duration:%s author_id:%s view_count:%s\n", video_result->age, video_result->duration, video_result->author_id, video_result->view_count);
}

typedef struct
{
    MediaInfo media_info;
    char subscriber_count[32];
} ChannelResult;

ChannelResult* init_channel_result()
{
    return calloc(1, sizeof(ChannelResult));
}

void print_channel_result(ChannelResult* channel_result)
{
    if (channel_result == NULL) return;
    print_media_info(&channel_result->media_info);
    printf("subscibers:%s\n", channel_result->subscriber_count);
}

typedef struct
{
    MediaInfo media_info;
    char video_count[32];
} PlaylistResult;

PlaylistResult* init_playlist_result()
{
    return calloc(1, sizeof(PlaylistResult));
}

void print_playlist_result(PlaylistResult* playlist_result)
{
    if (playlist_result == NULL) return;
    print_media_info(&playlist_result->media_info);
    printf("video_count:%s\n", playlist_result->video_count);
}

typedef enum
{
    NODE_TYPE_LIVE,
    NODE_TYPE_SHORT,
    NODE_TYPE_VIDEO,
    NODE_TYPE_CHANNEL,
    NODE_TYPE_PLAYLIST,
    NODE_TYPE_RAW_THUMBNAIL,
    NODE_TYPE_WORKER_TASK,
    NODE_TYPE_UNDF,
} NodeType;

typedef struct Node 
{
    void* content;
    struct Node* next;
    NodeType type;
} Node;

Node* init_node()
{
    Node* node = malloc(sizeof(Node));
    if (node == NULL) {
        printf("init_node: malloc returned NULL\n");
        return NULL;
    }

    node->type = NODE_TYPE_UNDF;
    node->content = node->next = NULL;
    return node;
}

void free_node(Node* node)
{
    if (node == NULL) return;

    switch (node->type) {
        case NODE_TYPE_LIVE:
        case NODE_TYPE_SHORT:
        case NODE_TYPE_VIDEO:
        case NODE_TYPE_CHANNEL:
        case NODE_TYPE_PLAYLIST:
            free(node->content);
            break;
        case NODE_TYPE_RAW_THUMBNAIL:
        case NODE_TYPE_WORKER_TASK:
        case NODE_TYPE_UNDF:
        break;
    }

    free(node); node = NULL;
}

typedef struct
{
    Node* head; 
    Node* tail; 
} List;

List init_list()
{
    List list;
    list.head = list.tail = NULL;
    return list;
}

void list_append(List* list, Node* node)
{
    if ((list == NULL) || (node == NULL)) return;

    node->next = NULL;

    if (list->head == NULL) {
        list->head = list->tail = node;
    }

    else {
        list->tail->next = node;
        list->tail = node;
    }
} 

Node* list_dequeue(List* list)
{
    if (list == NULL) return NULL;

    Node* detached = list->head;

    list->head = list->head->next;
    if (list->head == NULL) {
        list->tail = NULL;
    }

    return detached;
}

void list_free(List* list)
{
    if (list == NULL) return;

    while (list->head) {
        free_node(list_dequeue(list));
    }
}

void list_print(List* list)
{
    if (list == NULL) return;
    
    for (Node* current = list->head; current; current = current->next) {
        switch (current->type) {
            case NODE_TYPE_LIVE:
            case NODE_TYPE_SHORT:
            case NODE_TYPE_VIDEO:
                print_video_result(current->content);
                break;
            case NODE_TYPE_CHANNEL:
                print_channel_result(current->content);
                break;
            case NODE_TYPE_PLAYLIST:
                print_playlist_result(current->content);
                break;
            case NODE_TYPE_RAW_THUMBNAIL:
            case NODE_TYPE_WORKER_TASK:
            case NODE_TYPE_UNDF:
                break;
        }
    }
}
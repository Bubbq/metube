
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct
{
    char thumbnail_path[256];
    char title[256];
    char id[64];
} MediaInfo;

void media_info_print(const MediaInfo* media_info)
{
    if (media_info == NULL) return;
    printf("ID:%s TITLE:%s PATH:%s\n", media_info->id, media_info->title, media_info->thumbnail_path);
}

typedef struct
{
    MediaInfo media_info;
    char author_id[64];
    char date_published[32];
    char duration[16];
    char view_count[16];
} VideoResult;

VideoResult* video_result_init()
{
    return calloc(1, sizeof(VideoResult));
}

void video_result_print(const VideoResult* video_result)
{
    if (video_result == NULL) return;
    media_info_print(&video_result->media_info);
    printf("date_published:%s duration:%s author_id:%s view_count:%s\n", video_result->date_published, video_result->duration, video_result->author_id, video_result->view_count);
}

typedef struct
{
    MediaInfo media_info;
    char subscriber_count[32];
} ChannelResult;

ChannelResult* channel_result_init()
{
    return calloc(1, sizeof(ChannelResult));
}

void channel_result_print(ChannelResult* channel_result)
{
    if (channel_result == NULL) return;
    media_info_print(&channel_result->media_info);
    printf("subscibers:%s\n", channel_result->subscriber_count);
}

typedef struct
{
    MediaInfo media_info;
    char video_count[32];
} PlaylistResult;

PlaylistResult* playlist_result_init()
{
    return calloc(1, sizeof(PlaylistResult));
}

void playlist_result_print(PlaylistResult* playlist_result)
{
    if (playlist_result == NULL) return;
    media_info_print(&playlist_result->media_info);
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

Node* node_init()
{
    Node* node = malloc(sizeof(Node));
    if (node == NULL) {
        printf("node_init: malloc returned NULL\n");
        return NULL;
    }

    node->type = NODE_TYPE_UNDF;
    node->content = node->next = NULL;
    return node;
}

void node_free(Node* node)
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
    pthread_mutex_t mutex;
} List;

List list_init()
{
    List list;
    list.head = list.tail = NULL;
    pthread_mutex_init(&list.mutex, NULL);
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
    while (list->head) node_free(list_dequeue(list));
    pthread_mutex_destroy(&list->mutex);
}

void list_print(List* list)
{
    if (list == NULL) return;
    
    for (Node* current = list->head; current; current = current->next) {
        switch (current->type) {
            case NODE_TYPE_LIVE:
            case NODE_TYPE_SHORT:
            case NODE_TYPE_VIDEO:
                video_result_print(current->content);
                break;
            case NODE_TYPE_CHANNEL:
                channel_result_print(current->content);
                break;
            case NODE_TYPE_PLAYLIST:
                playlist_result_print(current->content);
                break;
            case NODE_TYPE_RAW_THUMBNAIL:
            case NODE_TYPE_WORKER_TASK:
            case NODE_TYPE_UNDF:
                break;
        }
    }
}
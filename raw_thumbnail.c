#include "include/raw_thumbnail.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

RawThumbnail* raw_thumbnail_init()
{
    RawThumbnail* raw_thumbnail = malloc(sizeof(RawThumbnail));
    if (raw_thumbnail == NULL){
        fprintf(stderr, "raw_thumbnail_init: malloc returned null\n");
        return NULL;
    }

    raw_thumbnail->next = NULL;
    raw_thumbnail->data = buffer_init();
    raw_thumbnail->media_type = MEDIA_TYPE_UNDF;
    memset(raw_thumbnail->id, 0, sizeof(raw_thumbnail->id));

    return raw_thumbnail;
}

void raw_thumbnail_free(RawThumbnail* raw_thumbnail)
{
    if (raw_thumbnail == NULL) return;

    if (buffer_is_ready(&raw_thumbnail->data)) buffer_free(&raw_thumbnail->data);

    free(raw_thumbnail); raw_thumbnail = NULL;
}
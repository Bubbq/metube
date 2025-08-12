#include "include/raw_thumbnail.h"
#include "include/raylib.h"
#include "include/texture_cache.h"

#include <stdbool.h>
#include <stdio.h>

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

void process_raw_thumbnail(RawThumbnail* raw_thumbnail, TextureCache* texture_cache)
{   
    if ((raw_thumbnail == NULL) || (buffer_is_ready(&raw_thumbnail->data) == false)) return;
    
    const Vector2 dimension = media_type_to_thumbnail_dim(raw_thumbnail->media_type);
    const Texture2D thumbnail = load_texture_from_memory(raw_thumbnail->data, dimension.x, dimension.y);
    if (IsTextureReady(thumbnail)) {
        TextureCacheEntry* entry = texture_cache_entry_init(thumbnail, raw_thumbnail->id);
        if (texture_cache_entry_is_ready(entry) ) {
            texture_cache_add_entry(texture_cache, entry);
        }
    }
}

Texture load_texture_from_memory(const Buffer buffer, const float width, const float height)
{
    Texture ret = (Texture){0};
    
    if (buffer_is_ready(&buffer) == false) return ret;

    Image image = LoadImageFromMemory(".jpg", (unsigned char*) buffer.data, buffer.size);
    if (IsImageReady(image) == false) {
        fprintf(stderr, "load_texture_from_memory: failed to load image buffer data\n");
        return ret;
    }

    ImageResize(&image, width, height);
    
    ret = LoadTextureFromImage(image);

    UnloadImage(image);

    return ret;
}

#include "include/texture_cache.h"

#include "include/utils.h"
#include "include/media_type.h"

#include <stdio.h>
#include <stdlib.h>

TextureCacheEntry* texture_cache_entry_init(const Texture texture, const char* id)
{
    if ((IsTextureReady(texture) == false) || (valid_string(id) == false)) return NULL;
    
    TextureCacheEntry* entry = (TextureCacheEntry*) malloc(sizeof(TextureCacheEntry));
    if (entry == NULL) {
        fprintf(stderr, "texture_cache_entry_init: malloc returned null\n");
        exit(EXIT_FAILURE);
    }

    timer_start(&entry->timer, CACHED_TEXTURE_LIFETIME);

    entry->texture = texture;

    const int written = snprintf(entry->id, sizeof(entry->id), "%s", id);
    if ((written < 0) || (written >= sizeof(entry->id))) {
        fprintf(stderr, "texture_cache_entry_init: snprintf returned %d\n", written);
        free(entry); entry = NULL;
        return NULL;
    }

    return entry;
}

void texture_cache_entry_free(TextureCacheEntry* entry) 
{
    if (entry == NULL) return;

    if (IsTextureReady(entry->texture)) UnloadTexture(entry->texture);
    free(entry); entry = NULL;
}

bool texture_cache_entry_is_ready(TextureCacheEntry* entry) 
{
    return (entry) && (valid_string(entry->id)) && (IsTextureReady(entry->texture));
}

void process_raw_thumbnail(RawThumbnail* raw_thumbnail, TextureCache* texture_cache)
{   
    if ((raw_thumbnail == NULL) || (buffer_is_ready(&raw_thumbnail->data) == false)) return;
    
    const float thumbnail_w = media_type_to_thumbnail_width(raw_thumbnail->media_type);
    const float thumbnail_h = media_type_to_thumbnail_height(raw_thumbnail->media_type);

    Texture thumbnail = {0};

    Image image = LoadImageFromMemory(".jpeg", (const unsigned char *) raw_thumbnail->data.data, raw_thumbnail->data.size);
    if (IsImageReady(image)) {
        ImageResize(&image, thumbnail_w, thumbnail_h);
        thumbnail = LoadTextureFromImage(image);
        UnloadImage(image);
    }

    TextureCacheEntry* entry = texture_cache_entry_init(thumbnail, raw_thumbnail->id);
    if (texture_cache_entry_is_ready(entry) ) {
        texture_cache_add_entry(texture_cache, entry);
    }
}

void texture_cache_add_entry(TextureCache* texture_cache, TextureCacheEntry* entry)
{
    if (entry == NULL) return;

    HASH_ADD_STR(*texture_cache, id, entry);
}

void texture_cache_remove_entry(TextureCache* texture_cache, TextureCacheEntry* entry)
{
    if ((HASH_COUNT(*texture_cache) == 0) || (entry == NULL)) return;

    HASH_DEL(*texture_cache, entry);

    texture_cache_entry_free(entry);
}

void texture_cache_free(TextureCache* texture_cache) 
{
    TextureCacheEntry *current, *tmp;

    HASH_ITER(hh, *texture_cache, current, tmp) {
        texture_cache_remove_entry(texture_cache, current);
    }

    texture_cache = NULL;
}

void texture_cache_remove_expried_entries(TextureCache* texture_cache) 
{
    TextureCacheEntry *current, *tmp;

    HASH_ITER(hh, *texture_cache, current, tmp) {
        if (timer_is_done(current->timer)) {
            printf("%s expired\n", current->id);
            texture_cache_remove_entry(texture_cache, current);
        }
    }
}

TextureCacheEntry* texture_cache_find_entry(TextureCache* texture_cache, const char* id)
{
    if ((HASH_COUNT(*texture_cache) == 0) || (valid_string(id) == false)) return NULL;

    TextureCacheEntry *found = NULL;
    
    HASH_FIND_STR(*texture_cache, id, found);
    
    return found;
}
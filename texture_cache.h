#pragma once

#include "uthash.h"
#include "raylib.h"
#include "timer.h"

#define MINUTE 60
#define CACHED_TEXTURE_LIFETIME (MINUTE * 1)

typedef struct
{
    char id [64];
    UT_hash_handle hh;
    Texture texture;
    Timer timer;
} TextureCacheEntry;

typedef TextureCacheEntry* TextureCache;

TextureCacheEntry* texture_cache_entry_init(const Texture texture, const char* id);
void texture_cache_entry_free(TextureCacheEntry* entry);
bool texture_cache_entry_is_ready(TextureCacheEntry* entry);
void texture_cache_add_entry(TextureCache* texture_cache, TextureCacheEntry* entry);
void texture_cache_remove_entry(TextureCache* texture_cache, TextureCacheEntry* entry);
void texture_cache_free(TextureCache* texture_cache);
void texture_cache_remove_expried_entries(TextureCache* texture_cache);
TextureCacheEntry* texture_cache_find_entry(TextureCache* texture_cache, const char* id);
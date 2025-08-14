#ifndef HIGHLIGHTED_CHANNEL_H
#define HIGHLIGHTED_CHANNEL_H

#include "search_result.h"
#include "texture_cache.h"

typedef struct
{
    SearchResult info;
    TextureCacheEntry* cached;
    bool is_subscribed;
} HighlightedChannel;

#endif
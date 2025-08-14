#ifndef HIGHLIGHTED_VIDEO_H
#define HIGHLIGHTED_VIDEO_H

#include "search_result.h"

typedef struct
{
    SearchResult info;
    char* description;
} HighlightedVideo;

#endif
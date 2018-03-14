/*
 * cache.h - 代理服务器的缓存部分。 
 * cache.h - caching web object in proxy server
 */
#ifndef __CACHE_H__
#define __CACHE_H__

#include "debug.h"
#include "error.h"
#include "csapp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


struct cache_node
{
    char *uri;
    char *content;
    struct cache_node *prev;
    struct cache_node *next;
    unsigned int size;
};

typedef struct 
{
    unsigned int size_all;
    struct cache_node *head;
    struct cache_node *tail;
} cache;



int init_cache(cache *ca);

int find_cache(cache *ca, char *uri, char *bufout, unsigned int *size);
int add_cache(cache *ca, char *uri, char *bufin, unsigned int size);

int clear_all_cache(cache *ca);
int deinit_cahce(cache *ca);

#endif /* __CACHE_H__ */
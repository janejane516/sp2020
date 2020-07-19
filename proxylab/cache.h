#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CachedItem {
	char *index;
	char *content;
	struct CachedItem *prev;
	struct CachedItem *next;
	unsigned int length;
} CachedItem;

typedef struct CacheList {
	CachedItem *head;
	CachedItem *tail;
	pthread_rwlock_t *lock;
	unsigned int bytes_left;
} CacheList;

CacheList *cache_init();
void cache_destruct(CacheList *list);
void init_node(CachedItem *node);

void set_node(CachedItem *node, char *index, unsigned int len);
void delete_node(CachedItem *node);

CachedItem *search_node(CacheList *list, char *index);
void add_node(CachedItem *node, CacheList *list);
CachedItem *remove_node(char *index, CacheList *list);
CachedItem *evict_list(CacheList *list);

int read_node_content(CacheList *list, char *index, char *content, unsigned int *len);
int insert_content_node(CacheList *list, char *index, char *content, unsigned int len);


#endif

#include "cache.h"

CacheList *cache_init(){
	CacheList *list = Malloc(sizeof(CacheList));
	list->head = NULL;
	list->tail = NULL;
	list->lock = Malloc(sizeof(*(list->lock)));
	pthread_rwlock_init((list->lock), NULL);
	list->bytes_left = MAX_CACHE_SIZE;

	return list;
}

void init_node(CachedItem *node){
	if(node) {
		node->next = NULL;
		node->prev = NULL;
		node->length = 0;
	}
}

void set_node(CachedItem *node, char *index, unsigned int len){
	if(node) {
		node->index = Malloc(sizeof(char)*len);
		memcpy(node->index, index,len);
		node->length = len;
	}
}

void delete_node(CachedItem *node){
	if(node){
		if(node->index)
			free(node->index);
		if(node->content)
			free(node->content);
		free(node);
	}
}

void cache_destruct(CacheList *list) {
	if(list) {
		CachedItem *curr = list->head;
		while(curr) {
			CachedItem *tmp = curr;
			curr = tmp->next;
			delete_node(tmp);
		}
		pthread_rwlock_destroy((list->lock));
		free(list->lock);
		free(list);
	}
}

CachedItem *search_node(CacheList *list, char *index) {
	if(list) {
		CachedItem *tmp = list->head;
		while(tmp) {
			if(!strcmp(tmp->index, index))
				return tmp;
			tmp = tmp->next;
		}
	}
	return NULL;
}

void add_node(CachedItem *node, CacheList *list){
	if(list) {
		if(node){
			while(list->bytes_left < node->length) {
				CachedItem *tmp_node = evict_list(list);
				delete_node(tmp_node);
			}
			if(!list->tail) {
				list->head = list->tail = node;
				list->bytes_left -= node->length;
			}
			else {
				list->tail->next = node;
				node->prev = list->tail;
				list->tail = node;
				list->bytes_left -= node->length;
			}
		}
	}
}

CachedItem *remove_node(char *index, CacheList *list){
	if(list){
		CachedItem *tmp = search_node(list, index);
		if(tmp) {
			if(tmp == list->head)
				tmp = evict_list(list);
			else {
				if(tmp->prev) 
					tmp->prev->next = tmp->next;
				if(tmp->next)
					tmp->next->prev = tmp->prev;
				else
					list->tail = tmp->prev;
				list->bytes_left += tmp->length;
			}
			tmp->prev = NULL;
			tmp->next = NULL;
		}
		return tmp;
	}
	
	return NULL;
}

/* LRU(least-recently-used) eviction */
CachedItem *evict_list(CacheList *list){
	if(list){
		CachedItem *curr = list->head;
		if(curr) {
			if(curr->next)
				curr->next->prev = NULL;
			list->bytes_left += curr->length;
			if(list->head == list->tail)
				list->tail = NULL;
			list->head = curr->next;
			return curr;
		}
	}
	return NULL;
}

int read_node_content(CacheList *list, char *index, char *content, unsigned int *len){
	if(!list)
		return -1;
	
	pthread_rwlock_rdlock((list->lock));
	
	CachedItem *tmp = search_node(list, index);
	
	if(!tmp)
	{
		pthread_rwlock_unlock((list->lock));
		return -1;
	}
	
	*len = tmp->length;
	memcpy(content, tmp->content,*len);
	
	pthread_rwlock_unlock((list->lock));
	
	pthread_rwlock_wrlock((list->lock));
	add_node(remove_node(index, list), list);
	pthread_rwlock_unlock((list->lock));
	
	return 0;
}

int insert_content_node(CacheList *list, char *index, char *content, unsigned int len){
	if(!list)
		return -1;
	
	CachedItem *tmp = Malloc(sizeof(*tmp));
	init_node(tmp);
	set_node(tmp, index, len);
	
	if(!tmp)
		return -1;
		
	tmp->content = Malloc(sizeof(char)*len);
	memcpy(tmp->content, content,len);

	pthread_rwlock_wrlock((list->lock));
	add_node(tmp, list);
	pthread_rwlock_unlock((list->lock));
	return 0;
}

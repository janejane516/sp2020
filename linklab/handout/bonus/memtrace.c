//------------------------------------------------------------------------------
//
// memtrace
//
// trace calls to the dynamic memory manager
//
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memlog.h>
#include <memlist.h>
#include "callinfo.h"

//
// function pointers to stdlib's memory management functions
//
static void *(*mallocp)(size_t size) = NULL;
static void (*freep)(void *ptr) = NULL;
static void *(*callocp)(size_t nmemb, size_t size);
static void *(*reallocp)(void *ptr, size_t size);

//
// statistics & other global variables
//
static unsigned long n_malloc  = 0;
static unsigned long n_calloc  = 0;
static unsigned long n_realloc = 0;
static unsigned long n_allocb  = 0;
static unsigned long n_freeb   = 0;
static item *list = NULL;

void *malloc(size_t size) {
	char *error;
	mallocp = dlsym(RTLD_NEXT, "malloc");
	if ((error = dlerror()) != NULL) {
		fputs(error, stderr);
		exit(1);
	}
	void *ptr = mallocp(size);
	n_malloc ++;
	n_allocb += size;
	alloc(list, ptr, size);
	LOG_MALLOC(size, ptr);
	return ptr;
}

void *calloc(size_t nmemb, size_t size) {
	char *error;
	callocp = dlsym(RTLD_NEXT, "calloc");
	if ((error = dlerror()) != NULL) {
		fputs(error, stderr);
		exit(1);
	}
	void *ptr = callocp(nmemb, size);
	n_calloc ++;
	n_allocb += nmemb * size;
	alloc(list, ptr, nmemb * size);
	LOG_CALLOC(nmemb, size, ptr);
	return ptr;
}

void *realloc(void *ptr, size_t size) {
	char *error;
	void *new_ptr;
	item *re = NULL;
	reallocp = dlsym(RTLD_NEXT, "realloc");
	if ((error = dlerror()) != NULL) {
		fputs(error, stderr);
		exit(1);
	}
	re = find(list, ptr);
	if(!re) {
		new_ptr = reallocp(NULL, size);
		LOG_REALLOC(ptr, size, new_ptr);
		LOG_ILL_FREE();
	}
	else if((re->cnt) <= 0) {
		new_ptr = reallocp(NULL, size);
		LOG_REALLOC(ptr, size, new_ptr);
		LOG_DOUBLE_FREE();
	}
	else {
		new_ptr = reallocp(ptr, size);
		n_freeb += (dealloc(list, ptr)->size);
		LOG_REALLOC(ptr, size, new_ptr);
	}
	n_realloc ++;
	n_allocb += (alloc(list, new_ptr, size)->size);
	return new_ptr;
}

void free(void *ptr) {
	char *error;
	item *fr = NULL;
	if (!ptr) {
		LOG_FREE(ptr);
		LOG_ILL_FREE();
		return;
	}
	freep = dlsym(RTLD_NEXT, "free");
	if ((error = dlerror()) != NULL) {
		fputs(error, stderr);
		exit(1);
	}
	LOG_FREE(ptr);
	fr = find(list, ptr);
	if (!fr) {
		LOG_ILL_FREE();
		return;
	}
	if ((fr->cnt) <= 0) {
		LOG_DOUBLE_FREE();
		return;
	}
	freep(ptr);
	n_freeb += (dealloc(list, ptr)->size); 
}
//
// init - this function is called once when the shared library is loaded
//
__attribute__((constructor))
void init(void)
{
  char *error;

  LOG_START();

  // initialize a new list to keep track of all memory (de-)allocations
  // (not needed for part 1)
  list = new_list();

  // ...
}

//
// fini - this function is called once when the shared library is unloaded
//
__attribute__((destructor))
void fini(void)
{
	unsigned long alloc_avg = 0;
	if((n_malloc + n_calloc + n_realloc) != 0) {
  	alloc_avg = n_allocb / (n_malloc + n_calloc + n_realloc);
	}
  LOG_STATISTICS(n_allocb, alloc_avg, n_freeb);

	item *cur = list->next;
	int start = 0;
	while (cur) {
		if((cur->cnt) > 0) {
			if(start == 0) {
				start = 1;
				LOG_NONFREED_START();
			}
			LOG_BLOCK(cur->ptr, cur->size, cur->cnt, cur->fname, cur->ofs);
		}
		cur = cur->next;
	}
  LOG_STOP();

  // free list (not needed for part 1)
  free_list(list);
}

// ...

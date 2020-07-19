/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/************************
 * 2016-18223 Jane Shin
 ************************/

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4	/* Word and header/footer size (bytes) */
#define DSIZE 8	/* Double word sie (bytes) */
#define CHUNKSIZE (1<<7) /* Extend heap by this amount (bytes) */
#define MINSEGSIZE 32	/* Min block size of segmented lists */
#define SEG_N 20	/* Number of segmented lists */

#define MAX(x, y) ((x) > (y)? (x) : (y))

/*Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read and write a pointer(address) at address p */
#define GET_PTR(p) ((char *)(*(unsigned int **)(p)))
#define PUT_PTR(p, val)	(*(unsigned int **)(p) = (unsigned int *)(val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((void *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((void *)(bp) - DSIZE)))

/* Given block ptr bp, compute address of bp's next and previous entries */
#define PREVP(bp) ((char **)(bp))
#define NEXTP(bp) ((char **)(bp + WSIZE))

/* Given block ptr bp, compute address of bp's next and previous free blocks in the segregated list */
#define PREV_FP(bp)	((char **)(bp))
#define NEXT_FP(bp) (((char **)(bp)) + WSIZE)

/* Gobal variables */
char *heap_listp;
char *seg_hdrp;

/* Functions */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert(void *bp, size_t asize);
static void delete(void *bp);

/* 
 * mm_init - Initialize the malloc package.
 */
int mm_init(void)
{
	heap_listp = NULL;
	//heap_freelistp = NULL;

	/* Create the initial empty heap */
	if ((heap_listp = mem_sbrk(4 * WSIZE + (1 + SEG_N) * DSIZE)) == (void *) - 1)
		return -1;

	seg_hdrp = heap_listp;
	/* Initialize segregated free lists */
	for (int i = 0; i <= SEG_N; i++) {
		PUT_PTR(seg_hdrp + (i * DSIZE), 0);
	}

	heap_listp = heap_listp + ((1 + SEG_N) * DSIZE);
	PUT(heap_listp, 0);								/* Alignment Padding */
	PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));	/* Prologue header */
	PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));	/* Prologue footer */
	PUT(heap_listp + (3 * WSIZE), PACK(0, 1));		/* Epilogue header */
	heap_listp = heap_listp + (2 * WSIZE);

	/* Extend the empty heap with a free block of CHUNKSIZE bytes */
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return -1;

    return 0;
}

/*
 * extend_heap - Extend the heap with a new free block.
 */

static void *extend_heap(size_t words)
{
	char *bp;
	size_t asize;

	/* Allocate an even number of words to maintain alignment */
	asize = ALIGN(words);
	if ((bp = mem_sbrk(asize)) == (void *) -1)
		return NULL;

	/* Initialize free block header/footer and the epilogue header */
	PUT(HDRP(bp), PACK(asize, 0));			/* Free block header */
	PUT(FTRP(bp), PACK(asize, 0));			/* Free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));	/* New epilogue header */
	insert(bp, asize);

	return coalesce(bp);
}

/*
 * coalesce - Merge a block with any adjacent free blocks.
 */
static void *coalesce(void *bp)
{
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && next_alloc) {			/* Case 1 */
		return bp;
	}

	else if (prev_alloc && !next_alloc) {	/* Case 2 */
		delete(bp);
		delete(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	
	else if (!prev_alloc && next_alloc) {	/* Case 3 */
		delete(bp);
		delete(PREV_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}	

	else {									/* Case 4 */
		delete(bp);
		delete(PREV_BLKP(bp));
		delete(NEXT_BLKP(bp));
		size += (GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp))));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}

	insert(bp, size);

	return bp;
}

/*
 * find_fit - Perform a fit search.
 */
static void *find_fit(size_t asize)
{
	void *bp;
	int n = 0;

	while (n <= SEG_N) {
		bp = seg_hdrp + DSIZE * n;
		if (GET_PTR(bp) != NULL) {
			for (bp = GET_PTR(bp); bp != NULL; bp = GET_PTR(NEXTP(bp))) {
				if ((asize <= GET_SIZE(HDRP(bp))))
					return bp;
			}
		}
		n++;
	}
	return NULL; /* No fit */
}

/* 
 * place - Place the requested block at the beginning of the free block.
 */
static void place(void *bp, size_t asize) 
{
	size_t csize = GET_SIZE(HDRP(bp));
	
	delete(bp);

	if((csize - asize) >= 2 * DSIZE) { /* Split */
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(csize - asize, 0));
		PUT(FTRP(bp), PACK(csize - asize, 0));
		insert(bp, (csize - asize));
	}
	else {
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
}

/*
 * insert - Insert a block into the segmented list.
 */
static void insert(void *bp, size_t asize)
{
	int n = 0;
	void *search;
	void *loc = NULL;
	void *current;
	size_t size = MINSEGSIZE;

	while (n <= SEG_N) {
		if (asize <= size)
			break;
		size *= 2;
		n ++;
	}

	search = GET_PTR((char *) seg_hdrp + n * DSIZE);
	current = search;

	while ((search != NULL) && (asize > GET_SIZE(HDRP(search)))) {
		loc = search;
		search = PREV_FP(search);
	}

	if (search != NULL) {
		PUT_PTR(NEXTP(search), bp);
		PUT_PTR(PREVP(bp), search);
		PUT_PTR(NEXTP(bp), loc);
		if (loc != NULL) {
			PUT_PTR(PREVP(loc), bp);
		}
		else {
			PUT_PTR(current, bp);
		}
	}
	else {
		PUT_PTR(PREVP(bp), NULL);
		if (loc != NULL) {
			PUT_PTR(NEXTP(bp), loc);
			PUT_PTR(PREVP(loc), bp);
		}
		else {
			PUT_PTR(NEXTP(bp), NULL);
			PUT_PTR(current, bp);
		}
	}
}

/*
 * delete - Delete a block from the segmented list.
 */
static void delete(void *bp) 
{
	int n = 0;
	void *current;
	size_t asize = GET_SIZE(HDRP(bp));
	size_t size = MINSEGSIZE;

	while (n <= SEG_N) {
		if (asize <= size)
			break;
		size *= 2;
		n++;
	}
	current = seg_hdrp + n * DSIZE;

	if (PREV_FP(bp) != NULL) {
		PUT_PTR(NEXTP(PREV_FP(bp)), NEXT_FP(bp));
		if (NEXT_FP(bp) != NULL)
			PUT_PTR(PREVP(NEXT_FP(bp)), PREV_FP(bp));
		else
			PUT_PTR(current, PREV_FP(bp));
	}
	else {
		if (NEXT_FP(bp) != NULL)
			PUT_PTR(PREVP(NEXT_FP(bp)), NULL);
		else 
			PUT_PTR(current, NULL);
	}
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
	size_t asize;		/* Adjusted block size */
	size_t extendsize;	/* Amount to extend heap if no fit */
	char *bp;

	if (heap_listp == 0)
		mm_init();

	/* Ignore spurious requests */
	if (size == 0)
		return NULL;

	/* Adjust block size to include overhead and alignment reqs. */
	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else 
		asize = ALIGN(size + DSIZE);

	/* Search the free list for a fit */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}
	
	/* No fit found. Get more memory and place the block. */
	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;
	place(bp, asize);

	return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
	if (ptr == NULL)
		return;
	size_t size = GET_SIZE(HDRP(ptr));
	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));

	insert(ptr, size);
	coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
	
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
	if (size == 0) {
		mm_free(ptr);
		return NULL;
	}
	if (oldptr == NULL) 
		return mm_malloc(size);

	newptr = mm_malloc(size);
	
    if (newptr == NULL)
      return NULL;

    copySize = GET_SIZE(HDRP(ptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);

    return newptr;
}

static int mm_check() {
	int n;
	char *bp = heap_listp;
	printf("\nInitial seg list blocks");
	for (n = 0; n <= SEG_N; n++) {
		printf("The block number %d which is located at", n);
		printf("%p has the address : [%p] \n", bp, GET_PTR(bp));
	}
	return 0;
}












/*
 * mm.c - using segregated free list(seglist) & first fit search.
 * - Block Information: Header(4 bytes), payload and padding, Footer(4 bytes)
 * - Free Blocks additionally contain information of previous&next free pointers.
 * - The heap stores the pointers for each class of the seglist.
 * - The number of classes of seglist is SEG_N(0~SEG_N-1), which is defined as the macro.
 * - The smallest size class stores 0~MINSEGSIZE. The class size powers by 2.
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

#define WSIZE 4       		 /* Word and header/footer size (bytes) */
#define DSIZE 8       		 /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)   /* Extend heap by this amount (bytes) */
#define INITCHUNKSIZE (1<<5) /* Initial CHUNKSIZE */
#define MINSEGSIZE 128     	 /* Minimum seglist size. */
#define SEG_N 26         	 /* The number of classes of seglist */
#define MINSPLITSIZE 80	     /* The index used in place function, whether to split or not. */

#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

/* Read and write an address(double word) at address p */
#define GET_PTR(p)       ((char *) *(unsigned int **)(p))
#define PUT_PTR(p, val)  (*(unsigned int **)(p) = (unsigned int *)(val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its next and previous free ptrs */
#define NEXT_FP(bp) ((char**)bp)
#define PREV_FP(bp) ((char**)(bp + WSIZE))

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((void *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((void *)(bp) - DSIZE)))


/* Global variables */
static char *heap_listp;  /* Pointer to first block */
static char *seg_hdrp;
static char *epil_addr;

/* Helper functions */
static void *extend_heap(size_t words);
static void *place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static char *get_class_address(void *bp);
static int get_class(size_t asize);
static void insert(void *bp);
static void delete(void *bp);
static int mm_check(void);



/*
 * mm_init - Initialize the memory manager.
 */
int mm_init(void)
{
    int i;
    heap_listp = NULL;

    // The heap stores all the seglist' pointers
    if ((heap_listp = mem_sbrk(4*WSIZE + ((1+SEG_N) * DSIZE))) == (void *)-1)
        return -1;

    seg_hdrp = heap_listp;

    for(i=0; i<SEG_N; i++)
    {
        PUT_PTR(seg_hdrp + (i*DSIZE), 0);
    }

    heap_listp += ((1+SEG_N) * DSIZE);
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp+ (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
	epil_addr = heap_listp + (3*WSIZE);
    heap_listp += (2*WSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(INITCHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}


/*
 * mm_malloc - Allocate a block with the adjusted size.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    void *bp;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
		asize = ALIGN(size + DSIZE);

    /* Search the free list for a fit. */
    if ((bp = find_fit(asize)) != NULL) {
        bp = place(bp, asize);
        return bp;
    }

	/* No fit found. Extend the heap area. */
	if (!GET_ALLOC((char *)(epil_addr - WSIZE))) { /* If the last block is free. */
		extendsize = asize - GET_SIZE((char *)(epil_addr - WSIZE));
	}
	else {
		extendsize = MAX(asize, CHUNKSIZE);
	}

    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    bp = place(bp, asize);

    return bp;
}


/*
 * mm_free - Free a block and coalesce.
 */
void mm_free(void *bp)
{
	size_t asize;

    if (bp == NULL)
        return;

	asize = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(asize, 0));
    PUT(FTRP(bp), PACK(asize, 0));

    coalesce(bp);
}


/*
* mm_realloc - Naive implementation of realloc.
*/
void *mm_realloc(void *ptr, size_t size)
{
	void *oldptr = ptr;
    void *newptr;
	size_t copySize;

    if(size == 0) {
        mm_free(ptr);
        return NULL;
	}

    if(oldptr == NULL)
        return mm_malloc(size);

    newptr = mm_malloc(size);

    /* If realloc() fails. */
    if(newptr == NULL)
		return NULL;

    /* Copy the old data and free the old ptr. */
    copySize = GET_SIZE(HDRP(ptr));
    if(size < copySize) 
		copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);

    return newptr;
}


/*
 * place - Place block of asize bytes at start of free block bp
 *         and split if following conditions are met.
 */
static void *place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));

	delete(bp);

	if ((csize - asize) <= 2*DSIZE) {	/* Do not split */
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
	else if (asize >= MINSPLITSIZE) {	/* Split */
		PUT(HDRP(bp), PACK(csize-asize, 0));
		PUT(FTRP(bp), PACK(csize-asize, 0));
		insert(bp);
        PUT(HDRP(NEXT_BLKP(bp)), PACK(asize, 1));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(asize, 1));
		return NEXT_BLKP(bp);
	}
	else { 								/*Split */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        PUT(HDRP(NEXT_BLKP(bp)), PACK(csize-asize, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(csize-asize, 0));
        insert(NEXT_BLKP(bp));
    }
	return bp;
}


/*
 * coalesce - Boundary tag coalescing. Return bp to coalesced block.
 */

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {				/* Case 1 */
        insert(bp);
		return bp;
    }

    else if (prev_alloc && !next_alloc) {		/* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
    }

    else if (!prev_alloc && next_alloc) { 		/* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        delete(PREV_BLKP(bp));
		bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
    }

    else {										/* Case 4 */
        size += (GET_SIZE(HDRP(NEXT_BLKP(bp)))+GET_SIZE(HDRP(PREV_BLKP(bp))));
        delete(NEXT_BLKP(bp));
        delete(PREV_BLKP(bp));
		bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
    }

	insert(bp);
    return bp;
}


/*
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words)
{
    void *bp;
    size_t size;


    /* Allocate an even number of words to maintain alignment */
    size = (words%2)? ((words+1)*WSIZE) : (words*WSIZE);
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* Initialize free block header/footer,
     * next Free, previous Free and the epilogue header */

    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));         /* Free block header */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */
	epil_addr = HDRP(NEXT_BLKP(bp));

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}


/* 
 * insert - Inserts the block to the seglist.
*/
static void insert(void *bp)
{
	void *search;
	void *current;

	current = get_class_address(bp);
	search = GET_PTR(current);

	if (search != NULL) {
		PUT_PTR(PREV_FP(search), bp);
		PUT_PTR(PREV_FP(bp), NULL);
		PUT_PTR(NEXT_FP(bp), search);
		PUT_PTR(current, bp);
	}
	else {
		PUT_PTR(PREV_FP(bp), NULL);
		PUT_PTR(NEXT_FP(bp), NULL);
		PUT_PTR(current, bp);
	}
}


/* 
 * delete - deletes the block from the seglist.
*/
static void delete(void *bp)
{
    void *current;
	void *next;
	void *prev;

	current = get_class_address(bp);
	next = GET_PTR(NEXT_FP(bp));
	prev = GET_PTR(PREV_FP(bp));

	if (prev != NULL) {
		PUT_PTR(NEXT_FP(prev), next);
		if (next != NULL)
			PUT_PTR(PREV_FP(next), prev);
	}
	else {
		if (next != NULL) {
			PUT_PTR(PREV_FP(next), NULL);
			PUT_PTR(current, next);
		}
		else 
			PUT_PTR(current, NULL);
	}
}


/* 
 * find_fit - First fit search for a block with asize bytes.
*/
static void *find_fit(size_t asize)
{
    int n = get_class(asize);
    void *bp;

    while (n < SEG_N) {
        bp = seg_hdrp + n*DSIZE;
        if(GET_PTR(bp) != NULL) {
            for (bp = GET_PTR(bp); bp != NULL; bp = GET_PTR(NEXT_FP(bp))) {
                if ((asize <= GET_SIZE(HDRP(bp))))
                    return bp;
            }

        }
		n++;
    }
    return NULL; /* No fit */
}


/* 
 * get_class_address - Returns the pointer to the bp's class of the seglist.
*/
static char *get_class_address(void *bp)
{
    int n;
    size_t asize = GET_SIZE(HDRP(bp));
    n = get_class(asize);
    return((char *) seg_hdrp + n*DSIZE);
}


/* 
 * get_class - Returns the class num of the input asize.
*/
static int get_class(size_t asize)
{
    int n;
    size_t size = MINSEGSIZE;
    for(n=0; n<SEG_N; n++) {
        if(asize <= size)
            break;
        size *= 2;
    }
    return n;
}

/*
 * (static) mm_check - A heap checker that scans the heap and checks the consistency.
 */
static int mm_check(void)
{
	void *bp;

	printf("\n[HEAP CHECKER STARTED]\n");
	printf("Heap starting address: [%p]\n", heap_listp);

	bp = heap_listp;
	while(bp != NULL && GET_SIZE(bp) != 0) {
		/* Header, footer consistency */
		if((GET_ALLOC(HDRP(bp)) != GET_ALLOC(FTRP(bp))) || (GET_SIZE(HDRP(bp)) != GET_SIZE(FTRP(bp)))) {
			printf("\nError: Header/Footer Inconsistency in block %p", bp);
			fflush(stdout);
			exit(0);
		}
		/* Do the allocated block pointers inside the heap area? */
		if ((bp < mem_heap_lo()) || (bp > mem_heap_hi())) {
			printf("\nError: free block pointer %p is not in the heap.\n", bp);
			fflush(stdout);
			exit(0);
		}
		bp = NEXT_BLKP(bp);
	}

	printf("\n--Free list info.--\n");
	for (int n=0; n<SEG_N; n++) {
		bp = ((char *) seg_hdrp + n*DSIZE);
		printf("%dth free list has the address: [%p]\n", n, GET_PTR(bp));

		while (bp != NULL && GET_SIZE(bp) != 0) {
			/* Do the pointers in a free list inside the heap area? */
			if ((bp < mem_heap_lo()) || (bp > mem_heap_hi())) {
				printf("\nError: free block pointer %p is not in the heap.\n", bp);
				fflush(stdout);
				exit(0);
			}
			/* Is every block in the free list marked as free? */
			if (GET_ALLOC(HDRP(bp))) {
				printf("\nError: %dth free list has an allocated block pointer %p\n.", n, bp);
				fflush(stdout);
				exit(0);
			}	
			bp = PREV_FP(bp);
		}
	}

	printf("\n[HEAP CHECKER TERMINATED]\n");

	return 0;
}



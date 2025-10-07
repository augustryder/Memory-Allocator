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
#include "mm.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team August",
    /* First member's full name */
    "August",
    /* First member's email address */
    "dontmatter@aol.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

#define WSIZE 4               // word size
#define DSIZE 8               // double word size
#define CHUNK_SIZE (1 << 12)  // amount to extend heap by

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// pack size and allocated bit into a word
#define PACK(size, alloc) ((size) | (alloc))

// read and write a ptr at p
#define GET_PTR(p) (*((void **)(p)))
#define PUT_PTR(p, v) (*((void **)(p))) = (v)

// read word and write a word at p
#define GET(p) (*(uint32_t *)(p))
#define PUT(p, val) (*(uint32_t *)(p) = (val))

// get size and alloc bit from header/footer at p
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

// returns the header or footer pointer from a bp
#define HEADER_PTR(bp) ((uint32_t *)((char *)(bp) - WSIZE))
#define FOOTER_PTR(bp) ((uint32_t *)((char *)(bp) + (GET_SIZE(HEADER_PTR(bp)) - DSIZE)))

// returns a pointer to the pointer of the next/prev free block
#define NEXT_FREE_PTR(bp) ((uintptr_t *)(bp))
#define PREV_FREE_PTR(bp) ((uintptr_t *)(bp) + 1)

// returns a pointer to the next or previous block
#define NEXT_BLOCK_PTR(bp) ((void *)((char *)(bp) + GET_SIZE(HEADER_PTR(bp))))
#define PREV_BLOCK_PTR(bp) ((void *)((char *)(bp) - GET_SIZE((char *)bp - DSIZE)))

/* single word (4) or double word (8) alignment */
#define ALIGNMENT DSIZE

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define MIN_FREE_BLOCK_SIZE ALIGN(DSIZE + 2 * sizeof(void *))

static unsigned char *heap_listp;
static void *free_listp;
static void *extend_heap(uint32_t words);
static void *coalesce(void *bp);
static void *first_fit(uint32_t size);
static void place(void *bp, uint32_t size);
static void insert_free(void *bp);
static void remove_free(void *bp);
static void *next_free(void *bp);
static void *prev_free(void *bp);

void print_heap_list();
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // printf("mm_init()\n");
    heap_listp = mem_sbrk(4 * WSIZE);
    if (heap_listp == (void *)-1) return -1;
    PUT(heap_listp, 0);                             // 4 byte padding word
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));        // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));  // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));      // epilogue

    heap_listp += (2 * WSIZE);
    if (extend_heap(CHUNK_SIZE / WSIZE) == NULL) return -1;
    // print_heap_list();
    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    if (size == 0) return NULL;

    print_heap_list();
    printf("mm_malloc: size %zu\n", size);

    void *bp;
    size_t aligned_size = DSIZE + ALIGN(size);

    if ((bp = first_fit(aligned_size)) != NULL)
    {
        remove_free(bp);
        place(bp, aligned_size);
        return bp;
    }
    // No space found, extend heap
    size_t extend_size = MAX(CHUNK_SIZE, aligned_size);
    if ((bp = extend_heap(extend_size / WSIZE)) == NULL) return NULL;
    remove_free(bp);
    place(bp, aligned_size);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL) return;
    uint32_t *header = HEADER_PTR(ptr);
    uint32_t *footer = FOOTER_PTR(ptr);
    if (GET_SIZE(header) != GET_SIZE(footer) || GET_ALLOC(header) != GET_ALLOC(footer)) return;

    print_heap_list();
    printf("mm_free()\n");

    uint32_t size = GET_SIZE(HEADER_PTR(ptr));
    PUT(HEADER_PTR(ptr), PACK(size, 0));
    PUT(FOOTER_PTR(ptr), PACK(size, 0));
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

    newptr = mm_malloc(size);
    if (newptr == NULL) return NULL;
    copySize = *(size_t *)((char *)oldptr - ALIGN(sizeof(size_t)));
    if (size < copySize) copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static void *extend_heap(uint32_t words)
{
    uint32_t size = (words % 2 == 0) ? words * WSIZE : (words + 1) * WSIZE;
    char *bp = mem_sbrk(size);
    if (bp == (void *)-1) return NULL;

    PUT(HEADER_PTR(bp), PACK(size, 0));               // set header
    PUT(FOOTER_PTR(bp), PACK(size, 0));               // set footer
    PUT(HEADER_PTR(NEXT_BLOCK_PTR(bp)), PACK(0, 1));  // set epilogue

    return coalesce(bp);
}

static void *coalesce(void *bp)
{
    uint32_t prev_allocated = GET_ALLOC(FOOTER_PTR(PREV_BLOCK_PTR(bp)));
    uint32_t next_allocated = GET_ALLOC(HEADER_PTR(NEXT_BLOCK_PTR(bp)));
    uint32_t size = GET_SIZE(HEADER_PTR(bp));

    if (prev_allocated == 1 && next_allocated == 0)
    {
        size += GET_SIZE(HEADER_PTR(NEXT_BLOCK_PTR(bp)));
        PUT(HEADER_PTR(bp), PACK(size, 0));
        PUT(FOOTER_PTR(bp), PACK(size, 0));
    }
    else if (prev_allocated == 0 && next_allocated == 1)
    {
        size += GET_SIZE(HEADER_PTR(PREV_BLOCK_PTR(bp)));
        PUT(HEADER_PTR(PREV_BLOCK_PTR(bp)), PACK(size, 0));
        PUT(FOOTER_PTR(bp), PACK(size, 0));
        bp = PREV_BLOCK_PTR(bp);
    }
    else if (prev_allocated == 0 && next_allocated == 0)
    {
        size += GET_SIZE(HEADER_PTR(NEXT_BLOCK_PTR(bp))) + GET_SIZE(HEADER_PTR(PREV_BLOCK_PTR(bp)));
        PUT(HEADER_PTR(PREV_BLOCK_PTR(bp)), PACK(size, 0));
        PUT(FOOTER_PTR(NEXT_BLOCK_PTR(bp)), PACK(size, 0));
        bp = PREV_BLOCK_PTR(bp);
    }
    insert_free(bp);
    return bp;
}

static void *first_fit(uint32_t size)
{
    void *bp = free_listp;
    while (bp != NULL)
    {
        if (GET_SIZE(HEADER_PTR(bp)) >= size) return bp;
        bp = next_free(bp);
    }
    return NULL;
}

static void place(void *bp, uint32_t size)
{
    uint32_t block_size = GET_SIZE(HEADER_PTR(bp));
    size = MAX(size, MIN_FREE_BLOCK_SIZE);
    if (block_size - size >= MIN_FREE_BLOCK_SIZE)
    {
        // split block
        PUT(HEADER_PTR(bp), PACK(size, 1));
        PUT(FOOTER_PTR(bp), PACK(size, 1));

        PUT(HEADER_PTR(NEXT_BLOCK_PTR(bp)), PACK(block_size - size, 0));
        PUT(FOOTER_PTR(NEXT_BLOCK_PTR(bp)), PACK(block_size - size, 0));
        insert_free(NEXT_BLOCK_PTR(bp));
    }
    else
    {
        // don't split
        PUT(HEADER_PTR(bp), PACK(block_size, 1));
        PUT(FOOTER_PTR(bp), PACK(block_size, 1));
    }
}

void print_heap_list()
{
    void *bp = (void *)heap_listp;
    while (GET_SIZE(HEADER_PTR(bp)) != 0)
    {
        int size = GET_SIZE(HEADER_PTR(bp));
        int allocated = GET_ALLOC(HEADER_PTR(bp));
        printf("%d, %d\n", allocated, size);
        bp = NEXT_BLOCK_PTR(bp);
    }
}

// Free list functionality

static void insert_free(void *bp)
{
    // set prev and next for current block
    PUT_PTR(NEXT_FREE_PTR(bp), free_listp);
    PUT_PTR(PREV_FREE_PTR(bp), NULL);
    // update prev for free list head and update head
    if (free_listp != NULL)
    {
        PUT_PTR(PREV_FREE_PTR(free_listp), bp);
    }

    free_listp = bp;
}

static void remove_free(void *bp)
{
    void *prev = prev_free(bp);
    void *next = next_free(bp);

    if (prev != NULL && next != NULL)
    {
        PUT_PTR(NEXT_FREE_PTR(prev), next);
        PUT_PTR(PREV_FREE_PTR(next), prev);
    }
    else if (prev == NULL && next != NULL)
    {
        free_listp = next;
        PUT_PTR(PREV_FREE_PTR(free_listp), NULL);
    }
    else if (prev != NULL && next == NULL)
    {
        PUT_PTR(NEXT_FREE_PTR(prev), NULL);
    }
    else
    {
        free_listp = NULL;
    }

    PUT_PTR(PREV_FREE_PTR(bp), NULL);
    PUT_PTR(NEXT_FREE_PTR(bp), NULL);
}

static void *next_free(void *bp)
{
    if (bp == NULL) return NULL;
    return GET_PTR(NEXT_FREE_PTR(bp));
}

static void *prev_free(void *bp)
{
    if (bp == NULL) return NULL;
    return GET_PTR(PREV_FREE_PTR(bp));
}
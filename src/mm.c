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

// read word and write a word at p
#define GET(p) (*(uint32_t *)(p))
#define PUT(p, val) (*(uint32_t *)(p) = (val))

// get size and alloc bit from header/footer at p
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

// returns the header or footer pointer from a bp
#define HEADER_PTR(bp) ((unsigned char *)(bp) - WSIZE)
#define FOOTER_PTR(bp) ((unsigned char *)(bp) + (GET_SIZE(HEADER_PTR(bp)) - DSIZE))

// returns a pointer to the next or previous block
#define NEXT_BLOCK_PTR(bp) ((unsigned char *)(bp) + GET_SIZE(HEADER_PTR(bp)))
#define PREV_BLOCK_PTR(bp) ((unsigned char *)(bp) - GET_SIZE((unsigned char *)bp - DSIZE))

/* single word (4) or double word (8) alignment */
#define ALIGNMENT DSIZE

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static unsigned char *heap_listp;
static void *extend_heap(size_t words);
static void *coalesce(void *bp);

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    heap_listp = mem_sbrk(4 * WSIZE);
    if (heap_listp == (void *)-1) return -1;

    PUT(heap_listp, 0);                             // 4 byte padding word
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));        // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));  // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));      // epilogue

    heap_listp += (2 * WSIZE);

    if (extend_heap(CHUNK_SIZE / WSIZE) == NULL) return -1;
    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
        return NULL;
    else
    {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {}

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
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize) copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static void *extend_heap(size_t words)
{
    size_t size = (words % 2 == 0) ? words * WSIZE : (words + 1) * WSIZE;
    char *bp = mem_sbrk(size);
    if (bp == (void *)-1) return NULL;

    PUT(HEADER_PTR(bp), PACK(size, 0));
    PUT(FOOTER_PTR(bp), PACK(size, 0));
    PUT(HEADER_PTR(NEXT_BLOCK_PTR(bp)), PACK(0, 1));

    return coalesce(bp);
}

static void *coalesce(void *bp)
{
    uint32_t prev_allocated = GET_ALLOC(FOOTER_PTR(PREV_BLOCK_PTR(bp)));
    uint32_t next_allocated = GET_ALLOC(HEADER_PTR(NEXT_BLOCK_PTR(bp)));
    size_t size = GET_SIZE(HEADER_PTR(bp));

    if (prev_allocated == 1 && next_allocated == 1) return bp;

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
    else
    {
        size += GET_SIZE(HEADER_PTR(NEXT_BLOCK_PTR(bp))) + GET_SIZE(HEADER_PTR(PREV_BLOCK_PTR(bp)));
        PUT(HEADER_PTR(PREV_BLOCK_PTR(bp)), PACK(size, 0));
        PUT(FOOTER_PTR(NEXT_BLOCK_PTR(bp)), PACK(size, 0));
        bp = PREV_BLOCK_PTR(bp);
    }
    return bp;
}
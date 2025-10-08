
#include "mm.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"

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

#define ALIGNMENT DSIZE

// rounds up to the nearest multiple of ALIGNMENT
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define MIN_BLOCK_SIZE ALIGN(DSIZE + 2 * sizeof(void *))

#define NUM_LISTS 12
static void *segregated_lists[NUM_LISTS];

static void *heap_list_ptr;

static void *extend_heap(uint32_t words);
static void *coalesce(void *bp);
static void place(void *bp, uint32_t size);

static void *first_fit(uint32_t size);

static int get_list_index(uint32_t size);
static void insert_free(void *bp);
static void remove_free(void *bp);
static void *next_free(void *bp);
static void *prev_free(void *bp);

static void print_heap_list();
static void print_free_list(void *free_list_ptr);
static void print_segregated_lists();

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // Initialize segregated lists
    for (int i = 0; i < NUM_LISTS; ++i)
    {
        segregated_lists[i] = NULL;
    }

    // Initialize start of heap
    heap_list_ptr = mem_sbrk(4 * WSIZE);
    if (heap_list_ptr == (void *)-1) return -1;
    PUT(heap_list_ptr, 0);                             // 4 byte padding word
    PUT(heap_list_ptr + WSIZE, PACK(DSIZE, 1));        // prologue header
    PUT(heap_list_ptr + (2 * WSIZE), PACK(DSIZE, 1));  // prologue footer
    PUT(heap_list_ptr + (3 * WSIZE), PACK(0, 1));      // epilogue
    heap_list_ptr += (2 * WSIZE);

    // Add first free block
    if (extend_heap(CHUNK_SIZE / WSIZE) == NULL) return -1;
    return 0;
}

/*
 * mm_malloc - Allocate a block using first fit with a segregated list.
 */
void *mm_malloc(size_t size)
{
    if (size == 0) return NULL;

    uint32_t aligned_size = DSIZE + ALIGN(size);
    void *bp = first_fit(aligned_size);

    if (bp != NULL)
    {
        remove_free(bp);
        place(bp, aligned_size);
        return bp;
    }

    // No space found, extend heap
    uint32_t extend_size = MAX(CHUNK_SIZE, aligned_size);
    if ((bp = extend_heap(extend_size / WSIZE)) == NULL) return NULL;
    remove_free(bp);
    place(bp, aligned_size);
    return bp;
}

/*
 * mm_free - Free a block and coalesce.
 */
void mm_free(void *ptr)
{
    // Make sure pointer is valid
    if (ptr == NULL) return;
    uint32_t *header = HEADER_PTR(ptr);
    uint32_t *footer = FOOTER_PTR(ptr);
    if (GET_SIZE(header) != GET_SIZE(footer) || GET_ALLOC(header) != GET_ALLOC(footer)) return;

    // Update headers and coalesce
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

/*
 * Static Helper Functions
 */

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
        // Coalesce with next block
        remove_free(NEXT_BLOCK_PTR(bp));
        size += GET_SIZE(HEADER_PTR(NEXT_BLOCK_PTR(bp)));
        PUT(HEADER_PTR(bp), PACK(size, 0));
        PUT(FOOTER_PTR(bp), PACK(size, 0));
    }
    else if (prev_allocated == 0 && next_allocated == 1)
    {
        // Coalesce with previous block
        remove_free(PREV_BLOCK_PTR(bp));
        size += GET_SIZE(HEADER_PTR(PREV_BLOCK_PTR(bp)));
        PUT(HEADER_PTR(PREV_BLOCK_PTR(bp)), PACK(size, 0));
        PUT(FOOTER_PTR(bp), PACK(size, 0));
        bp = PREV_BLOCK_PTR(bp);
    }
    else if (prev_allocated == 0 && next_allocated == 0)
    {
        // Coalesce with both neighbor blocks
        remove_free(PREV_BLOCK_PTR(bp));
        remove_free(NEXT_BLOCK_PTR(bp));
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
    for (int i = get_list_index(size); i < NUM_LISTS; ++i)
    {
        void *bp = segregated_lists[i];
        while (bp != NULL)
        {
            if (GET_SIZE(HEADER_PTR(bp)) >= size) return bp;
            bp = next_free(bp);
        }
    }
    return NULL;
}

static void place(void *bp, uint32_t size)
{
    uint32_t block_size = GET_SIZE(HEADER_PTR(bp));
    size = MAX(size, MIN_BLOCK_SIZE);

    if (block_size - size >= MIN_BLOCK_SIZE)
    {
        // Split block
        PUT(HEADER_PTR(bp), PACK(size, 1));
        PUT(FOOTER_PTR(bp), PACK(size, 1));

        PUT(HEADER_PTR(NEXT_BLOCK_PTR(bp)), PACK(block_size - size, 0));
        PUT(FOOTER_PTR(NEXT_BLOCK_PTR(bp)), PACK(block_size - size, 0));
        insert_free(NEXT_BLOCK_PTR(bp));
    }
    else
    {
        // Don't split block
        PUT(HEADER_PTR(bp), PACK(block_size, 1));
        PUT(FOOTER_PTR(bp), PACK(block_size, 1));
    }
}

/*
 * Free List Functionality
 */

static int get_list_index(uint32_t size)
{
    if (size == 24) return 0;
    if (size == 32) return 1;
    if (size == 40) return 2;
    if (size == 48) return 3;
    if (size <= 64) return 4;
    if (size <= 128) return 5;
    if (size <= 256) return 6;
    if (size <= 512) return 7;
    if (size <= 1024) return 8;
    if (size <= 2048) return 9;
    if (size <= 4096) return 10;
    return 11;
}

static void insert_free(void *bp)
{
    int idx = get_list_index(GET_SIZE(HEADER_PTR(bp)));
    void *free_list_ptr = segregated_lists[idx];

    // Set prev and next for current block
    PUT_PTR(NEXT_FREE_PTR(bp), free_list_ptr);
    PUT_PTR(PREV_FREE_PTR(bp), NULL);

    // Connect the rest of the list and update head
    if (free_list_ptr != NULL) PUT_PTR(PREV_FREE_PTR(free_list_ptr), bp);

    segregated_lists[idx] = bp;
}

static void remove_free(void *bp)
{
    int idx = get_list_index(GET_SIZE(HEADER_PTR(bp)));
    void *free_list_ptr = segregated_lists[idx];

    void *prev = prev_free(bp);
    void *next = next_free(bp);

    if (prev != NULL && next != NULL)
    {
        PUT_PTR(NEXT_FREE_PTR(prev), next);
        PUT_PTR(PREV_FREE_PTR(next), prev);
    }
    else if (prev == NULL && next != NULL)
    {
        segregated_lists[idx] = next;
        PUT_PTR(PREV_FREE_PTR(segregated_lists[idx]), NULL);
    }
    else if (prev != NULL && next == NULL)
    {
        PUT_PTR(NEXT_FREE_PTR(prev), NULL);
    }
    else
    {
        segregated_lists[idx] = NULL;
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

/*
 * Printers
 */

static void print_heap_list()
{
    printf("HEAP LIST:\n");
    void *bp = heap_list_ptr;
    while (GET_SIZE(HEADER_PTR(bp)) != 0)
    {
        uint32_t size = GET_SIZE(HEADER_PTR(bp));
        uint32_t allocated = GET_ALLOC(HEADER_PTR(bp));
        printf("%d, %d\n", allocated, size);
        bp = NEXT_BLOCK_PTR(bp);
    }
    printf("\n");
}

static void print_free_list(void *free_list_ptr)
{
    printf("FREE LIST: ");
    void *bp = free_list_ptr;
    while (bp != NULL)
    {
        uint32_t size = GET_SIZE(HEADER_PTR(bp));
        printf("(size: %d, addr: %p, prev: %p, next: %p) ", size, bp, GET_PTR(PREV_FREE_PTR(bp)),
               GET_PTR(NEXT_FREE_PTR(bp)));
        bp = next_free(bp);
    }
    printf("\n");
}

static void print_segregated_lists()
{
    printf("--- SEGREGATED LIST ---\n");
    for (int i = 0; i < NUM_LISTS; ++i)
    {
        print_free_list(segregated_lists[i]);
    }
    printf(" ---------------------- \n");
}
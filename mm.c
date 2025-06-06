/*
    mm-naive.c - The fastest, least memory-efficient malloc package.

    In this naive approach, a block is allocated by simply incrementing
    the brk pointer.  A block is pure payload. There are no headers or
    footers.  Blocks are never coalesced or reused. Realloc is
    implemented directly using mm_malloc and mm_free.

    NOTE TO STUDENTS: Replace this header comment with your own header
    comment that gives a high level description of your solution.
*/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {"Team8", "Junsik Park", "qkrwns1478@gmail.com", "", ""};

/*
    Basic constants and macros for manipulating the free list
*/
#define WSIZE 4             // Word and header/footer size (bytes)
#define DSIZE 8             // Double word size (bytes)
#define CHUNKSIZE (1<<8)    // Extend heap by this amount (256 bytes for EC2 optimization)
#define MINSIZE 16          // Minimum block size
#define SEGSIZE 20          // Segregated free list size

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// Pack a size and allocated bit into a word
#define PACK(size, alloc) ((size) | (alloc))

// Read and write a word at address p
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

// Read the size and allocated fields from address p
#define GET_SIZE(p) (GET(p) & ~0x7) // 11111000 마스크로 하위 3비트를 제거하고 size만 남김
#define GET_ALLOC(p) (GET(p) & 0x1)

// Given block ptr bp, compute address of its header and footer
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// Given block ptr bp, compute address of next and previous blocks
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

// Given block ptr bp, compute address of next and previous free blocks
// GCC가 32비트 모드로 실행되기 때문에 포인터 크기는 4바이트 (Makefile 참고)
#define NEXT_FREE(bp) (*(void **)(bp))
#define PREV_FREE(bp) (*(void **)(bp + WSIZE))

#define SEG_ROOT(i) (*(void **)(seglistp + (i*WSIZE)))
#define SEG_ROOT_ADDR(size) &SEG_ROOT(get_index(size))

int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);

static char *heap_listp = NULL;
static char *seglistp = NULL;
static void *extend_heap(size_t words);
static void *find_first_fit(size_t asize, void *root);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
static void append_free(void *bp, void *root_addr);
static void remove_free(void *bp, void *root_addr);
static int get_index(size_t size);
/*-----------------------------------------------------------------------------------------------*/
/* 
    mm_init - initialize the malloc package.
*/
int mm_init(void) {
    // Create the initial empty heap
    if ((heap_listp = mem_sbrk((SEGSIZE+4)*WSIZE)) == (void *)-1) return -1;
    PUT(heap_listp, 0);                                         // Alignment padding
    for (int i=1; i<=SEGSIZE; i++) {
        PUT(heap_listp + (i*WSIZE), NULL);                      // Seglist root for size class
    }
    PUT(heap_listp + ((SEGSIZE+1)*WSIZE), PACK(DSIZE, 1));      // Prologue header
    PUT(heap_listp + ((SEGSIZE+2)*WSIZE), PACK(DSIZE, 1));      // Prologue footer
    PUT(heap_listp + ((SEGSIZE+3)*WSIZE), PACK(0, 1));          // Epilogue header
    seglistp = heap_listp + (1*WSIZE);
    heap_listp += ((SEGSIZE+2)*WSIZE);
    // Extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;
    return 0;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    // Allocate an even number of words to maintain alignment
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    // Initialize free block header/footer and the epilogue header
    PUT(HDRP(bp), PACK(size, 0));           // Free block header
    PUT(FTRP(bp), PACK(size, 0));           // Free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   // New epilogue header

    // Coalesce if the previous block was free
    return coalesce(bp);
}
/*-----------------------------------------------------------------------------------------------*/
/* 
    mm_malloc - Allocate a block by incrementing the brk pointer.
    Always allocate a block whose size is a multiple of the alignment.
*/
void *mm_malloc(size_t size) {
    size_t asize;       // Adjusted block size
    size_t extendsize;  // Amount to extend heap if no fit
    char *bp;

    // Ignore spurious requests
    if (size == 0) return NULL;
    
    // Adjust block size to include overhead and alignment reqs
    if (size <= DSIZE) asize = MINSIZE;
    else asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    // Search the free list for a fit
    // 요청된 크기 클래스에서 맞는 블록을 찾지 못했다면 다음 크기의 클래스를 위한 가용 리스트를 검색함
    for (int i = get_index(asize); i < SEGSIZE; i++) {
        if ((bp = find_first_fit(asize, SEG_ROOT(i))) != NULL) {
            place(bp, asize);
            return bp;
        }
    }

    // No fit found: Get more memory and place the block
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) return NULL;
    place(bp, asize);
    return bp;
}

static void *find_first_fit(size_t asize, void *root) {
    for (void *bp = root; bp != NULL; bp = NEXT_FREE(bp)) {
        if ((asize <= GET_SIZE(HDRP(bp)))) return bp;
    }
    return NULL;
}

static void place(void *bp, size_t asize) {
    if (bp == NULL) return;
    size_t csize = GET_SIZE(HDRP(bp));
    void *root_addr = SEG_ROOT_ADDR(csize);
    remove_free(bp, root_addr);

    if ((csize - asize) >= MINSIZE) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *nbp = NEXT_BLKP(bp);
        PUT(HDRP(nbp), PACK(csize-asize, 0));
        PUT(FTRP(nbp), PACK(csize-asize, 0));

        void *root_addr = SEG_ROOT_ADDR(csize-asize);
        append_free(nbp, root_addr);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}
/*-----------------------------------------------------------------------------------------------*/
/*
    mm_free - Freeing a block does nothing.
*/
void mm_free(void *ptr)
{
    if (ptr == NULL) return;

    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // [Case 1] 이전과 다음 블록이 모두 할당상태
    if (prev_alloc && next_alloc) {
    }
    // [Case 2] 이전 블록은 할당상태, 다음 블록은 가용상태
    else if (prev_alloc && !next_alloc) {
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));
        size += next_size;
        void *next_root_addr = SEG_ROOT_ADDR(next_size);
        remove_free(NEXT_BLKP(bp), next_root_addr);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    // [Case 3] 이전 블록은 가용상태, 다음 블록은 할당상태
    else if (!prev_alloc && next_alloc) {
        size_t prev_size = GET_SIZE(HDRP(PREV_BLKP(bp)));
        size += prev_size;
        void *prev_root_addr = SEG_ROOT_ADDR(prev_size);
        remove_free(PREV_BLKP(bp), prev_root_addr);
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // [Case 4] 이전 블록과 다음 블록 모두 가용상태
    else {
        size_t prev_size = GET_SIZE(HDRP(PREV_BLKP(bp)));
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));
        size += prev_size + next_size;
        void *prev_root_addr = SEG_ROOT_ADDR(prev_size);
        void *next_root_addr = SEG_ROOT_ADDR(next_size);
        remove_free(PREV_BLKP(bp), prev_root_addr);
        remove_free(NEXT_BLKP(bp), next_root_addr);
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    void *root_addr = SEG_ROOT_ADDR(size);
    append_free(bp, root_addr);
    return bp;
}
/*-----------------------------------------------------------------------------------------------*/
// Append bp in list as the new root
static void append_free(void *bp, void *root_addr) {
    void **rootp = (void **)root_addr;
    void *root = *rootp;
    NEXT_FREE(bp) = root;
    if (root != NULL) PREV_FREE(root) = bp;
    PREV_FREE(bp) = NULL;
    *rootp = bp;
}

// Remove bp from free list
static void remove_free(void *bp, void *root_addr) {
    if (bp == NULL) return;
    void **rootp = (void **)root_addr;
    if (bp == *rootp) *rootp = NEXT_FREE(bp);
    else {
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
        if (NEXT_FREE(bp) != NULL) PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
    }
}

// Return class index for seglist
// static int get_index(size_t size) {
//     size_t a = 1;
//     size_t b = 2;
//     if (size <= a) return 0;
//     for (int i = 0; i < SEGSIZE; i++) {
//         if (a < size && size <= b) return i; // return i-th size clas
//         a <<= 1;
//         b <<= 1;
//     }
//     return SEGSIZE-1; // return the largest size class
// }
static int get_index(size_t size) {
    if (size <= 15) return 0;
    else if (size == 16) return 1;
    else if (size <= 64) return 2;
    else if (size <= 72) return 3;
    else if (size <= 112) return 4;
    else if (size <= 128) return 5;
    else if (size <= 256) return 6;
    else if (size <= 512) return 7;
    else if (size <= 4071) return 8;
    else if (size == 4072) return 9;
    else if (size <= 4094) return 10;
    else if (size == 4095) return 11;
    else if (size <= (1<<13)) return 12;
    else if (size <= (1<<14)) return 13;
    else if (size <= (1<<15)) return 14;
    else if (size <= (1<<16)) return 15;
    else if (size <= (1<<17)) return 16;
    else if (size <= (1<<18)) return 17;
    else if (size <= (1<<19)) return 18;
    else return 19;
}
/*-----------------------------------------------------------------------------------------------*/
/*
    mm_realloc - Implemented simply in terms of mm_malloc and mm_free
*/
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    size_t csize = GET_SIZE(HDRP(ptr));
    size_t asize;
    if (size <= DSIZE) asize = MINSIZE;
    else asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    // [Case 1] 이미 충분한 크기일 경우: 그대로 사용 or 분할
    if (asize <= csize) {
        if (csize-asize >= MINSIZE) {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            void *nbp = NEXT_BLKP(ptr);
            PUT(HDRP(nbp), PACK(csize-asize, 0));
            PUT(FTRP(nbp), PACK(csize-asize, 0));
            append_free(nbp, SEG_ROOT_ADDR(csize-asize));
        }
        return ptr;
    }

    // [Case 2] 다음 블록이 가용상태 + 확장 가능할 경우: 확장 후 리턴
    void *nbp = NEXT_BLKP(ptr);
    if (!GET_ALLOC(HDRP(nbp))) {
        size_t nsize = csize + GET_SIZE(HDRP(nbp));
        if (nsize >= asize) {
            void *next_root_addr = SEG_ROOT_ADDR(GET_SIZE(HDRP(nbp)));
            remove_free(nbp, next_root_addr);
            PUT(HDRP(ptr), PACK(nsize, 1));
            PUT(FTRP(ptr), PACK(nsize, 1));
            return ptr;
        }
    }

    // [Case 3] 이전 블록이 가용상태 + 합쳐서 충분한 경우: 합친 후 리턴
    void *pbp = PREV_BLKP(ptr);
    if (!GET_ALLOC(HDRP(pbp))) {
        size_t psize = GET_SIZE(HDRP(pbp));
        if (csize+psize >= asize) {
            remove_free(pbp, SEG_ROOT_ADDR(psize));
            memmove(pbp, ptr, csize);
            PUT(HDRP(pbp), PACK(csize+psize, 1));
            PUT(FTRP(pbp), PACK(csize+psize, 1));
            return pbp;
        }
    }

    // [Case 4] 기존 Case: 새로운 블록 할당 후 복사
    void *newptr = mm_malloc(size);
    if (newptr == NULL) return NULL;
    size_t copySize = csize;
    if (size < copySize) copySize = size;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}
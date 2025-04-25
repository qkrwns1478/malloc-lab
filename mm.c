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
team_t team = {
    /* Team name */
    "Team8",
    /* First member's full name */
    "Junsik Park",
    /* First member's email address */
    "qkrwns1478@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*-----------------------------------------------------------------------------------------------*/
/*
    Basic constants and macros for manipulating the free list
*/
#define WSIZE 4 // Word and header/footer size (bytes)
#define DSIZE 8 // Double word size (bytes)
#define CHUNKSIZE (1<<12) // Extend heap by this amount (bytes)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// Pack a size and allocated bit into a word
// size와 할당 여부(alloc)를 하나의 워드(4바이트 정수)로 패킹한다.
// size는 항상 8의 배수(하위 3비트가 0), alloc은 0 또는 1로 사용되므로 OR 연산으로 둘을 하나로 표현할 수 있다.
#define PACK(size, alloc) ((size) | (alloc))

// Read and write a word at address p
// 포인터 p가 가리키는 주소에서 4바이트 값을 읽는다.
// 메모리에서 해당 위치의 값을 가져오는 매크로
#define GET(p) (*(unsigned int *)(p))
// 포인터 p가 가리키는 메모리 주소에 val 값을 저장한다.
#define PUT(p, val) (*(unsigned int *)(p) = (val))

// Read the size and allocated fields from address p
// 주소 p에 있는 헤더 또는 푸터에서 size만 추출한다.
// 하위 3비트는 할당 정보로 쓰이므로 ~0x7 마스크로 이를 제거하고 size만 남긴다.
#define GET_SIZE(p) (GET(p) & ~0x7)
// 주소 p의 헤더 또는 푸터에서 할당 여부만 추출한다.
// 최하위 비트(0x1)가 1이면 할당, 0이면 미할당
#define GET_ALLOC(p) (GET(p) & 0x1)

// Given block ptr bp, compute address of its header and footer
// 블록 포인터 bp는 payload를 가리키고 있으므로, 그보다 앞에 있는 헤더 위치는 WSIZE(4바이트)만큼 앞이다.
#define HDRP(bp) ((char *)(bp) - WSIZE)
// 블록 포인터 bp로부터 블록 크기를 계산하여, 그 크기만큼 뒤로 가서 푸터 위치를 계산한다.
// 전체 블록 크기에서 헤더(4바이트)와 푸터(4바이트)를 고려해 DSIZE(8바이트)를 빼는 것이다.
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// Given block ptr bp, compute address of next and previous blocks
// 다음 블록의 시작 주소를 계산한다.
// 현재 블록의 시작 위치(bp)에 해당 블록의 전체 크기를 더하면 된다.
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
// 이전 블록의 시작 주소를 계산한다.
// 현재 포인터에서 앞에 있는 푸터 위치(bp - DSIZE)에서 블록 크기를 읽어내어, 그만큼 거슬러 올라간다.
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);

static char *heap_listp;
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
/*-----------------------------------------------------------------------------------------------*/
/* 
    mm_init - initialize the malloc package.

    Before calling mm_malloc mm_realloc or mm free, the application program (i.e.,
    the trace-driven driver program that you will use to evaluate your implementation) calls mm_init to
    perform any necessary initializations, such as allocating the initial heap area.
    The return value should be -1 if there was a problem in performing the initialization, 0 otherwise.
*/
// malloc 패키지의 초기화를 수행하는 함수다. 사용자가 mm_malloc, mm_free 등을 호출하기 전에 반드시 호출되어야 한다.
// 초기 힙 영역을 설정하고, 최소한의 메타데이터(프롤로그, 에필로그)를 설정한다.
int mm_init(void)
{
    /* Create the initial empty heap */
    // 힙을 처음 시작할 때 4워드(4 * 4바이트 = 16바이트)를 요청한다.
    // mem_sbrk()는 힙 영역을 확장해주는 함수다.
    // 반환값이 (void *)-1이면 요청 실패를 의미하므로, 실패 시 -1을 반환한다.
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) return -1;
    
    // 첫 번째 워드는 사용하지 않는 padding이다. 힙의 정렬(8바이트)을 맞추기 위한 용도
    PUT(heap_listp, 0);                             // Alignment padding
    // 8바이트 크기의 할당된 블록처럼 보이도록 "프롤로그 헤더"를 설정한다.
    // 항상 할당된 것으로 표시 (alloc = 1)하고, 크기는 8바이트로 설정한다.
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));    // Prologue header
    // 위의 헤더에 대응하는 푸터다. 프롤로그 블록은 최소 블록 크기인 8바이트로 구성되어 헤더와 푸터를 모두 필요로 한다.
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));    // Prologue footer
    // 초기 힙의 끝을 나타내는 에필로그 블록이다. 크기는 0, 할당된 상태다.
    // 에필로그는 언제나 heap의 마지막에 위치하며, 새로 할당된 블록 뒤에 갱신된다.
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));        // Epilogue header
    // heap_listp를 실제 usable 블록(프롤로그 블록의 payload 시작 위치)으로 옮긴다.
    // 이 위치부터 새로운 블록들이 이어서 추가된다.
    heap_listp += (2*WSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    // 초기 힙 생성 후, CHUNKSIZE(보통 4KB = 2^12)를 힙에 추가한다.
    // 블록 크기는 워드 단위로 받기 때문에 CHUNKSIZE / WSIZE를 전달한다.
    // 확장에 실패하면 -1을 반환한다.
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;
    return 0;
}

// 힙을 words 개수만큼 확장하는 함수다. 새로운 free 블록을 만들어서 연결하고, 필요하면 연결(coalesce)까지 한다.
static void *extend_heap(size_t words) {
    // 새로 만든 블록의 payload 시작 주소를 가리킬 포인터
    char *bp;
    // 요청된 크기(words)를 바이트 단위로 계산해서 저장할 변수
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    // words가 홀수면 +1 해서 짝수로 만든 다음 바이트 단위로 바꾼다. 이렇게 하면 8바이트 정렬(alignment)을 유지할 수 있다.
    // 즉, 항상 짝수 워드(= 8바이트 배수)만큼 확장하게 된다.
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    // 힙을 size 바이트만큼 확장하고, 시작 주소를 bp에 저장한다.
    // 실패하면 -1을 반환하니까 NULL 반환해서 실패 처리
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;
    
    /* Initialize free block header/footer and the epilogue header */
    // 새로 확장한 블록의 헤더에 크기(size)와 할당 안 됨(0) 표시를 저장
    PUT(HDRP(bp), PACK(size, 0));           // Free block header
    // 새 블록의 푸터에도 같은 정보 기록
    PUT(FTRP(bp), PACK(size, 0));           // Free block footer
    // 힙의 끝을 갱신. 이전 에필로그는 중간으로 밀렸고, 새 위치에 크기 0인 새로운 에필로그 헤더를 만든다.
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   // New epilogue header

    /* Coalesce if the previous block was free */
    // 새로 만든 free 블록과 인접한 free 블록이 있다면 coalesce해서 반환한다.
    return coalesce(bp);
}
/*-----------------------------------------------------------------------------------------------*/
/* 
    mm_malloc - Allocate a block by incrementing the brk pointer.
    Always allocate a block whose size is a multiple of the alignment.

    The mm_malloc routine returns a pointer to an allocated block payload of at least size bytes.
    The entire allocated block should lie within the heap region and should not overlap with
    any other allocated chunk.

    We will comparing your implementation to the version of malloc supplied in the standard C library (libc).
    Since the libc malloc always returns payload pointers that are aligned to 8 bytes,
    your malloc implementation should do likewise and always return 8-byte aligned pointers.
*/
// size 바이트를 요청받아 그에 맞는 크기의 메모리 블록을 힙에서 할당하고, 그 블록의 포인터를 반환한다.
void *mm_malloc(size_t size)
{
    size_t asize;       // Adjusted block size
    size_t extendsize;  // Amount to extend heap if no fit
    char *bp;

    /* Ignore spurious requests */
    // 요청된 크기가 0인 경우, 의미 없는 요청으로 간주하여 NULL을 반환한다.
    if (size == 0) return NULL;
    
    /* Adjust block size to include overhead and alignment reqs */
    // 요청된 크기가 DSIZE(8바이트) 이하이면, 최소 블록 크기인 16바이트(2*DSIZE)로 할당한다.
    // 이외의 경우에는 size에 헤더와 푸터를 포함한 오버헤드를 더하고, 8바이트 배수로 올림하여 asize를 계산한다.
    if (size <= DSIZE) asize = 2*DSIZE;
    else asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    /* Search the free list for a fit */
    // 적절한 크기의 빈 블록을 find_fit 함수를 통해 탐색한다.
    // 적절한 블록이 존재할 경우, 해당 위치에 블록을 배치하고 포인터를 반환한다.
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found: Get more memory and place the block */
    // 적절한 블록이 없을 경우, 힙을 확장해야 하므로 asize와 CHUNKSIZE(기본 확장 크기) 중 더 큰 값으로 확장 크기를 결정한다.
    extendsize = MAX(asize, CHUNKSIZE);
    // 위에서 결정한 크기만큼 힙을 확장한다. 단위는 워드(4바이트)이므로 바이트를 워드 단위로 변환한다.
    // 확장에 실패하면 NULL을 반환한다.
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) return NULL;
    // 새로 확장된 공간에 요청한 크기만큼의 블록을 배치한다.
    place(bp, asize);
    // 최종적으로 할당된 블록의 포인터를 반환한다.
    return bp;
}

// asize 바이트를 수용할 수 있는 빈 블록을 탐색하여 해당 블록의 포인터를 반환한다.
// 현재 구현 방식은 first-fit 전략이다. 즉, 처음으로 맞는 블록을 발견하면 즉시 반환한다.
static void *find_fit(size_t asize) {
    /* First-fit search */
    // 순회에 사용할 블록 포인터이다. 현재 탐색 중인 블록을 가리킨다.
    void *bp;

    // heap_listp에서 시작하여 힙의 마지막(에필로그 블록)까지 순차적으로 블록을 탐색한다.
    // 헤더에서 블록의 크기를 읽어들였을 때 0이면 에필로그 블록에 도달한 것이다.
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        // 현재 블록이 할당되지 않았고, 요청한 크기 asize보다 크거나 같다면 적절한 블록으로 판단한다.
        // 해당 블록의 포인터를 반환한다.
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) return bp;
    }
    // 탐색을 마칠 때까지 적절한 블록을 찾지 못한 경우, NULL을 반환한다.
    return NULL; // No fit
}

// bp 위치의 빈 블록에 asize 크기의 블록을 배치한다.
// 블록이 크다면 나머지 공간을 분할하여 새로운 free 블록으로 남겨둘 수 있다.
static void place(void *bp, size_t asize) {
    if (bp == NULL) return;

    // 현재 블록의 전체 크기를 읽어 csize에 저장한다.
    size_t csize = GET_SIZE(HDRP(bp));
    // 현재 블록을 할당한 후 남는 공간이 2*DSIZE(최소 블록 크기 = 16바이트) 이상이면, 블록을 분할한다.
    // 1. 앞부분은 요청한 크기 asize로 할당 상태로 표시한다.
    // 2. 남은 뒷부분은 새로운 빈 블록으로 만들고 헤더와 푸터를 설정한다.
    if ((csize - asize) >= (2*DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
    }
    // 남은 공간이 너무 작아서 나누기 어렵다면, 블록 전체를 할당 상태로 설정한다.
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}
/*-----------------------------------------------------------------------------------------------*/
/*
    mm_free - Freeing a block does nothing.

    The mm_free routine frees the block pointed to by ptr. It returns nothing.
    This routine is only guaranteed to work when the passed pointer (ptr) was returned
    by an earlier call to mm_malloc or mm_realloc and has not yet been freed.
*/
// ptr이 가리키는 블록을 해제(free)한다.
// 해당 블록은 이전에 mm_malloc 또는 mm_realloc을 통해 반환받은 포인터여야 하며, 아직 해제되지 않은 상태여야 한다.
void mm_free(void *ptr)
{
    if (ptr == NULL) return;

    // 포인터 ptr이 가리키는 블록의 헤더를 통해 전체 블록의 크기를 읽어온다.
    size_t size = GET_SIZE(HDRP(ptr));
    // 해당 블록의 헤더와 푸터를 갱신하여 "할당되지 않은 상태(alloc=0)"로 설정한다.
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    // 인접한 블록들도 비어 있다면 하나로 연결하기 위해 coalesce 함수를 호출한다.
    coalesce(ptr);
}

// 포인터 bp가 가리키는 블록과 이전/다음 블록이 비어 있다면 이들을 연결하여 하나의 큰 free 블록으로 만든다.
// 연결 후 새롭게 만들어진 free 블록의 포인터를 반환한다.
static void *coalesce(void *bp) {
    // 바로 앞 블록의 푸터를 통해 해당 블록이 할당되어 있는지를 확인한다.
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    // 바로 다음 블록의 헤더를 통해 해당 블록의 할당 여부를 확인한다.
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    // 현재 블록의 크기를 저장한다.
    size_t size = GET_SIZE(HDRP(bp));

    // [Case 1] 이전과 다음 블록이 모두 할당상태
    // 앞, 뒤 블록이 모두 할당되어 있는 경우 병합하지 않고 현재 블록 포인터를 그대로 반환한다.
    if (prev_alloc && next_alloc) {
        return bp;
    }
    // [Case 2] 이전 블록은 할당상태, 다음 블록은 가용상태
    // 뒤 블록이 비어 있다면, 현재 블록과 다음 블록을 병합한다.
    // 새로 합쳐진 블록의 크기를 계산하고, 헤더와 푸터를 갱신한다.
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    // [Case 3] 이전 블록은 가용상태, 다음 블록은 할당상태
    // 앞 블록이 비어 있는 경우, 앞 블록과 현재 블록을 병합한다.
    // 새로운 헤더는 앞 블록의 헤더 위치에, 푸터는 현재 블록의 푸터 위치에 기록된다.
    // 이후 병합된 블록의 시작 위치를 반환하기 위해 bp를 갱신한다.
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // [Case 4] 이전 블록과 다음 블록 모두 가용상태
    // 앞과 뒤 블록 모두 비어 있는 경우 세 블록을 하나로 병합한다.
    // 가장 앞쪽의 헤더와 가장 뒤쪽의 푸터에 병합된 크기를 기록하고, bp를 갱신한다.
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // 병합된 블록의 시작 주소를 반환한다.
    return bp;
}
/*-----------------------------------------------------------------------------------------------*/
/*
    mm_realloc - Implemented simply in terms of mm_malloc and mm_free

    The mm_realloc routine returns a pointer to an allocated region of
    at least size bytes with the following constraints.
    – if ptr is NULL, the call is equivalent to mm_malloc(size);
    – if size is equal to zero, the call is equivalent to mm_free(ptr);
    – if ptr is not NULL, it must have been returned by an earlier call to mm_malloc or mm_realloc.

    The call to mm realloc changes the size of the memory block pointed to by ptr (the old
    block) to size bytes and returns the address of the new block. Notice that the address of the
    new block might be the same as the old block, or it might be different, depending on your implementation,
    the amount of internal fragmentation in the old block, and the size of the realloc request.

    The contents of the new block are the same as those of the old ptr block, up to the minimum of
    the old and new sizes. Everything else is uninitialized. For example, if the old block is 8 bytes
    and the new block is 12 bytes, then the first 8 bytes of the new block are identical to the first 8 bytes
    of the old block and the last 4 bytes are uninitialized. Similarly, if the old block is 8 bytes
    and the new block is 4 bytes, then the contents of the new block are identical to the first 4 bytes
    of the old block.
*/
// 기존에 할당된 블록의 크기를 size 바이트로 조정(reallocate)하는 함수이다.
// ptr은 기존에 mm_malloc 또는 mm_realloc에 의해 반환된 유효한 포인터이어야 하며, 새로운 크기에 맞게 메모리를 재할당하고 데이터를 복사한다.
void *mm_realloc(void *ptr, size_t size)
{
    // ptr이 NULL인 경우는 malloc(size)를 호출한 것과 동일하므로, 새로 블록을 할당하여 반환한다.
    if (ptr == NULL) return mm_malloc(size);
    // size가 0이면 free(ptr)과 동일한 효과를 갖는다. 기존 블록을 해제하고, 아무것도 반환하지 않는다.
    if (size == 0) {
        mm_free(ptr);
        return;
    }

    // 기존의 블록 포인터를 oldptr에 저장한다.
    void *oldptr = ptr;
    // 새롭게 할당될 블록의 포인터를 저장할 변수이다.
    void *newptr;
    // 기존 블록에서 새 블록으로 복사할 데이터의 크기를 저장할 변수이다.
    size_t copySize;
    
    // 새로 요청된 크기만큼의 블록을 할당한다.
    // 이때 내부적으로 find_fit → place 또는 extend_heap이 호출되어 실제 메모리를 할당받게 된다.
    newptr = mm_malloc(size);
    // 만약 메모리 할당에 실패할 경우, NULL을 반환한다.
    if (newptr == NULL) return NULL;
    // 기존 블록의 크기를 읽어와 복사할 데이터의 최대 크기로 설정한다.
    copySize = GET_SIZE(HDRP(ptr));
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    // 만약 요청한 새 크기보다 기존 크기가 더 크다면, 복사 크기를 요청한 크기로 제한한다.
    // 즉, 데이터를 잘라서 복사한다.
    if (size < copySize) copySize = size;
    // oldptr에서 newptr로 copySize 바이트만큼의 데이터를 복사한다.
    memcpy(newptr, oldptr, copySize);
    // 기존 블록을 해제해서 메모리 누수를 방지한다.
    mm_free(oldptr);
    // 재할당된 새 블록의 포인터를 반환한다.
    return newptr;
}
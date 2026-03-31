#include <stdio.h>
#include "heap_allocator.h"
       
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

static void *alloc_pages(size_t n) {
    return VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

#else
#  include <sys/mman.h>

static void *alloc_pages(size_t n) {
    return mmap(NULL, n,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

#endif
 
void        *heap_start      = NULL;
void        *heap_brk        = NULL;
BlockHeader *first_block     = NULL;
FreeBlock   *free_lists[NUM_CLASSES];

// Maps a block size to a bucket index.
// Buckets are power-of-two ranges starting at 16:
//   0->[16,31]  1->[32,63]  2->[64,127]  ...  8->[4096,inf)
 
int size_class(int block_size) {
    int cls       = 0;
    int threshold = 16;
    while (cls < NUM_CLASSES - 1 && block_size >= threshold * 2) {
        threshold *= 2;
        cls++;
    }
    return cls;
}

// insert at the head of the appropriate size-class list.
 
void list_insert(FreeBlock *block) {
    int cls = size_class((int)(*((BlockHeader *)block) & ~7u));
    block->next = free_lists[cls];
    block->prev = NULL;
    if (free_lists[cls] != NULL)
        free_lists[cls]->prev = block;
    free_lists[cls] = block;
}

// unlink from whichever size-class list the block currently lives in.
 
void list_remove(FreeBlock *block) {
    int cls = size_class((int)(*((BlockHeader *)block) & ~7u));

    if (block->prev != NULL)
        block->prev->next = block->next;
    else
        free_lists[cls] = block->next;  // block was the head               

    if (block->next != NULL)
        block->next->prev = block->prev;

    block->next = NULL;
    block->prev = NULL;
}

// Allocates one page and lays out:
//   [4B padding][8B prologue][...free block...][4B epilogue]
// The initial free block is inserted into the segregated free list.
 
void bp_init(void) {
    if (heap_start != NULL) {
        fprintf(stderr, "ERROR: can only allocate BlockParty heap once.\n");
        exit(1);
    }

    for (int i = 0; i < NUM_CLASSES; i++)
        free_lists[i] = NULL;

    heap_start = alloc_pages(PAGE_SIZE);
    heap_brk   = (char *)heap_start + PAGE_SIZE;

    // 8-byte padding so that first_block (offset 16) is 8-byte aligned, which is required for the FreeBlock next/prev pointer members.
    // Layout: pad(8) + prologue(8) + free-block(...) + epilogue(4)
    // Total fixed overhead: 20 bytes.                                      
    BlockHeader *pad0 = (BlockHeader *)heap_start;
    pad0[0] = 354;
    pad0[1] = 354;   // two 4-byte words of padding 

    // Prologue: size=8, A=1 
    BlockHeader *prologue_header = (BlockHeader *)((char *)heap_start + 8);
    *prologue_header     = 8 | 1;
    *(prologue_header+1) = 354;   // prologue footer (canary) 

    // Initial free block starts at offset 16 (8-byte aligned).
    // Size must be a multiple of 16; round the available space down.
    //   available = PAGE_SIZE - 16 (pad+prologue) - 4 (epilogue) = 4076
    //   rounded down to multiple of 16: 4064
    // P-bit=1 because the prologue is allocated.                           
    int available     = PAGE_SIZE - 16 - 4;          // 4076                
    int initial_size  = available & ~15;              // 4064 (round down)   

    BlockHeader *fb_header = (BlockHeader *)((char *)heap_start + 16);
    *fb_header  = (uint32_t)initial_size | 2;         // A=0, P=1           
    first_block = fb_header;

    BlockHeader *fb_footer = (BlockHeader *)((char *)fb_header + initial_size - 4);
    *fb_footer = (uint32_t)initial_size;

    // Epilogue placed immediately after the free block so the visualize
    // loop terminates correctly (size=0, A=1).                             
    BlockHeader *epilogue = (BlockHeader *)((char *)fb_header + initial_size);
    *epilogue = 0 | 1;

    list_insert((FreeBlock *)fb_header);
}

// Walks the implicit block sequence and prints each block.
 
void bp_visualize(void) {
    int counter = 0;
    printf("Block#: A P Size Start            End\n");
    printf("================================================\n");

    for (BlockHeader *block = first_block;
         (*block & ~7u) != 0 && (void *)block < heap_brk;
         block = (BlockHeader *)((char *)block + (*block & ~7u))) {

        counter++;
        uint32_t size        = *block & ~7u;
        uint32_t footer_size = *(block + size / 4 - 1);
        int      a           = (int)(*block & 1u);
        int      p           = (int)((*block & 2u) >> 1);

        printf("%6d: %1d %1d %5u  %p  %p\n",
               counter, a, p, size,
               (void *)block,
               (void *)((char *)block + size));

        if (size != footer_size)
            printf("              * footer size mismatch: %u\n", footer_size);
    }
}

// Best-fit search within segregated size classes. 
void *bp_alloc(size_t size) {
    if (size == 0) return NULL;
    if (heap_start == NULL) bp_init();

    // Round up to 16-byte-aligned block size.
    // header(4) + payload + footer(4), minimum 16.                         
    int min_block_size = 4 + (int)size + 4;
    if (min_block_size < MIN_FREE_BLOCK) min_block_size = MIN_FREE_BLOCK;
    if (min_block_size % 16 != 0)  min_block_size += 16 - (min_block_size % 16);

    FreeBlock *chosen      = NULL;
    int        chosen_size = 0;

    for (int cls = size_class(min_block_size); cls < NUM_CLASSES; cls++) {
        for (FreeBlock *cur = free_lists[cls]; cur != NULL; cur = cur->next) {
            int cur_size = (int)(*((BlockHeader *)cur) & ~7u);
            if (cur_size >= min_block_size) {
                if (chosen == NULL || cur_size < chosen_size) {
                    chosen      = cur;
                    chosen_size = cur_size;
                    if (chosen_size == min_block_size) goto found; // exact fit 
                }
            }
        }
        // Any candidate from this class beats everything in higher classes. 
        if (chosen != NULL) break;
    }

found:
    if (chosen == NULL) return NULL;

    list_remove(chosen);
    BlockHeader *best_block = (BlockHeader *)chosen;

    // Split only when the remainder is at least MIN_FREE_BLOCK (32) bytes.
    // A smaller remainder cannot hold a FreeBlock (header + next + prev +
    // footer = 4+4+8+8+4 = 28, rounded to 32) and list_insert would write
    // its next/prev pointers over the footer, corrupting the heap.         
    if (chosen_size >= min_block_size + MIN_FREE_BLOCK) {
        //  Split  
        *best_block = (uint32_t)(min_block_size | 1 | (*best_block & 2u));
        BlockHeader *this_footer =
            (BlockHeader *)((char *)best_block + min_block_size - 4);
        *this_footer = (uint32_t)min_block_size;

        int          rem_size   = chosen_size - min_block_size;
        BlockHeader *rem_header =
            (BlockHeader *)((char *)best_block + min_block_size);
        *rem_header = (uint32_t)(rem_size | 2);   // A=0, P=1               
        BlockHeader *rem_footer =
            (BlockHeader *)((char *)rem_header + rem_size - 4);
        *rem_footer = (uint32_t)rem_size;

        list_insert((FreeBlock *)rem_header);

        // The block after the remainder already has its P-bit set correctly
        // because the remainder is free (P=0 for *its* successor is only
        // updated when the remainder is itself freed).                      

    } else {
        //  No split: allocate the whole chosen block  
        *best_block = (uint32_t)(chosen_size | 1 | (*best_block & 2u));
        BlockHeader *this_footer =
            (BlockHeader *)((char *)best_block + chosen_size - 4);
        *this_footer = (uint32_t)chosen_size;

        // Tell the successor that its predecessor is now allocated. 
        BlockHeader *next_header =
            (BlockHeader *)((char *)best_block + chosen_size);
        *next_header = *next_header | 2u;
    }

    return (char *)best_block + 4;
}

// Marks the block free, clears the successor's P-bit, then coalesces. Returns 0 on success, 1 if ptr is out of range, 2 if already free.
 
int bp_free(void *ptr) {
    if (ptr < heap_start || ptr >= heap_brk) return 1;

    BlockHeader *header = (BlockHeader *)((char *)ptr - 4);
    if (!(*header & 1u)) return 2;   // already free 

    int size = (int)(*header & ~7u);

    *header = (uint32_t)(size | (*header & 2u)); // clear A-bit, keep P-bit 

    BlockHeader *footer = (BlockHeader *)((char *)header + size - 4);
    *footer = (uint32_t)size;

    BlockHeader *next_header = (BlockHeader *)((char *)header + size);
    *next_header = *next_header & ~2u;  // clear successor's P-bit          

    bp_coalesce(header);
    return 0;
}

//  Merges the freed block with free neighbours. Each participant is pulled from its free list before the merge;
// the final combined block is re-inserted under the correct size class.
 
void bp_coalesce(BlockHeader *header) {
    int size = (int)(*header & ~7u);

    // Try to merge with next block 
    BlockHeader *next_header = (BlockHeader *)((char *)header + size);
    if (!(*next_header & 1u)) {
        int next_size = (int)(*next_header & ~7u);
        list_remove((FreeBlock *)next_header);
        size   += next_size;
        *header = (uint32_t)(size | (*header & 2u));
        BlockHeader *new_footer = (BlockHeader *)((char *)header + size - 4);
        *new_footer = (uint32_t)size;
    }

    // Try to merge with previous block 
    if (!(*header & 2u)) {   // P-bit = 0 means previous is free 
        BlockHeader *prev_footer = (BlockHeader *)((char *)header - 4);
        int          prev_size   = (int)(*prev_footer);
        BlockHeader *prev_header =
            (BlockHeader *)((char *)header - prev_size);
        list_remove((FreeBlock *)prev_header);
        size       += prev_size;
        *prev_header = (uint32_t)(size | (*prev_header & 2u));
        BlockHeader *new_footer =
            (BlockHeader *)((char *)prev_header + size - 4);
        *new_footer = (uint32_t)size;
        header = prev_header;
    }

    list_insert((FreeBlock *)header);
}
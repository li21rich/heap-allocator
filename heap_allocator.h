#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#define PAGE_SIZE 4096

/* 
 * Block layout
 * 
 *
 *  Allocated block:
 *   [ header (4B) | payload | footer (4B) ]
 *
 *  Free block (minimum 32 bytes - next/prev live in payload area):
 *   [ header (4B) | next* (8B) | prev* (8B) | ... | footer (4B) ]
 *
 *  header / footer bit encoding:
 *    bits[31:3]  block size (always a multiple of 16)
 *    bit  [1]    P-bit  - 1 if the previous block is allocated
 *    bit  [0]    A-bit  - 1 if this block is allocated
 *
 * Segregated size classes (NUM_CLASSES buckets, power-of-two ranges):
 *   0: [16,  31]     3: [128, 255]    6: [1024, 2047]
 *   1: [32,  63]     4: [256, 511]    7: [2048, 4095]
 *   2: [64, 127]     5: [512,1023]    8: [4096, inf)
 *  */

typedef uint32_t BlockHeader;

// Free block - next/prev overlay the payload bytes. 
typedef struct FreeBlock {
    BlockHeader       header;  // coincides with the block's header word     
    struct FreeBlock *next;
    struct FreeBlock *prev;
    // variable-length gap, then a 4-byte footer at the very end
} FreeBlock;

/* Minimum size for a free block.
 * FreeBlock layout on a 64-bit system:
 *   offset  0: header  (4 B, BlockHeader)
 *   offset  4: padding (4 B, compiler alignment)
 *   offset  8: next    (8 B, pointer)
 *   offset 16: prev    (8 B, pointer)
 *   offset 24: footer  (4 B) -- must not overlap next/prev
 * Total: 28 B rounded up to the next 16-byte boundary = 32 B.
 *
 * A block smaller than this cannot be placed in a free list without
 * list_insert overwriting the footer through the next/prev fields.      */
#define MIN_FREE_BLOCK 32

#define NUM_CLASSES 9

//  Globals (defined in heap_allocator.c)  
extern void        *heap_start;
extern void        *heap_brk;
extern BlockHeader *first_block;
extern FreeBlock   *free_lists[NUM_CLASSES];

//  Public API  
void  bp_init(void);
void  bp_visualize(void);
void *bp_alloc(size_t size);
int   bp_free(void *ptr);

//  Internal helpers  
int  size_class(int block_size);
void list_insert(FreeBlock *block);
void list_remove(FreeBlock *block);
void bp_coalesce(BlockHeader *header);
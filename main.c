#include <stdio.h>
#include <stdint.h>
#include "heap_allocator.h"

 // Helper: print a labelled pointer and its offset from heap_start so the output is meaningful on both 32-bit and 64-bit platforms.               
static void print_ptr(const char *label, void *p, size_t req_size) {
    if (p == NULL) {
        printf("%-4s = NULL  (request size: %zu) -- ALLOC FAILED\n",
               label, req_size);
    } else {
        // Cast to char* to ensure byte-wise subtraction for the offset
        printf("%-4s = heap+%-5td  (request size: %zu)\n",
               label, (char *)p - (char *)heap_start, req_size);
    }
}

int main(void) {
    // Initial state  
    printf("=== bp_init ===\n");
    bp_init();
    bp_visualize();

    // Basic allocations  
    printf("\n=== allocate a(8), b(128), c(8) ===\n");
    void *a = bp_alloc(8);   print_ptr("a", a, 8);
    void *b = bp_alloc(128); print_ptr("b", b, 128);
    void *c = bp_alloc(8);   print_ptr("c", c, 8);
    bp_visualize();

    // Free middle block, re-alloc into it  
    printf("\n=== free b ===\n");
    bp_free(b);
    bp_visualize();

    printf("\n=== allocate d(8), e(16), f(8), g(8) ===\n");
    // These will be carved out of the 144-byte block left by 'b' 
    void *d = bp_alloc(8);   print_ptr("d", d, 8);
    void *e = bp_alloc(16);  print_ptr("e", e, 16);
    void *f = bp_alloc(8);   print_ptr("f", f, 8);
    void *g = bp_alloc(8);   print_ptr("g", g, 8);
    bp_visualize();

    // Free non-adjacent blocks, then the block between them  
    printf("\n=== free d and f ===\n");
    bp_free(d);
    bp_free(f);
    bp_visualize();

    printf("\n=== free e (should coalesce d+e+f) ===\n");
    bp_free(e);
    bp_visualize();

    /*h should land in the coalesced d+e+f region  
     * Requesting 128 results in a 144-byte internal block, which 
     * matches the exact size of the hole left by b/d/e/f. */     
    printf("\n=== allocate h(128) ===\n");
    void *h = bp_alloc(128); print_ptr("h", h, 128);
    
    // We compare h to d because d was the start of that coalesced region 
    if (h != NULL && (char*)h == (char*)d)
        printf("  [OK] h reuses coalesced d/e/f region\n");
    else
        printf("  [NOTE] h is at a different address than d\n");
    bp_visualize();

    // Size-class index table  
    printf("\n=== size -> class mapping ===\n");
    for (int sz = 1; sz <= 8192; sz *= 2)
        printf("  size %5d -> class %d\n", sz, size_class(sz));

    // Edge cases  
    printf("\n=== edge cases ===\n");
    void *z0 = bp_alloc(0);
    printf("alloc(0)  = %s\n", z0 == NULL ? "NULL (correct)" : "non-NULL (unexpected)");

    int r1 = bp_free(NULL);
    printf("free(NULL) returned %d (expected 1)\n", r1);

    int r2 = bp_free(a);
    printf("free(a)    returned %d (expected 0)\n", r2);
    
    int r3 = bp_free(a);
    printf("free(a) x2 returned %d (expected 2 - already free)\n", r3);

    printf("\nDone.\n");
    return 0;
}
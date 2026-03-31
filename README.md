## Segregated Free-List Heap Allocator
A fast, explicit segregated free-list allocator implemented in C11. It uses best fit search with power-of-two size classes and supports immediate coalescing via boundary tags.

### Building
Linux / macOS /  Windows (MinGW / Clang)
```bash
gcc -std=c11 -Wall -Wextra -o heap_test heap_allocator.c main.c
./heap_test
```
Windows (MSVC)
```bat
cl /std:c11 /W4 heap_allocator.c main.c /Fe:heap_test.exe
heap_test.exe
```

### References
* COMPUTER SYSTEMS: A Programmer's Perspective (Bryant & O'Hallaron)
* [SHMALL](https://github.com/CCareaga/heap_allocator/blob/master/main.c) (Careaga)
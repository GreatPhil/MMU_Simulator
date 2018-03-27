# MMU_Simulator
This is a simulation of a generic Operating System's Memory Management Unit (MMU) -- including simulations of the virtual memory, backing store, page table, and TLB.  This was written as a HW assignment for CS543 (Operating Systems) @ Drexel University, in response to the "Programming Project" from Chapter 9 (Memory Mangement) of the assigned text, _Operating System Concepts, 9th Edition, by A. Silberschatz et. al_.

## Instructions

To compile, type:
> gcc memory_management.c

To run, type:
> ./a.out

## Source Code:

The file "memory_management.c" is the source code for the book assignment, including the additional modifications as requested by the book. Details on the functions within this code are provided within the code itself.

The code is set up, as is, to run the basic default assignment.  It has the hooks in it to perform the additional functionality as well.  In order to do the "Modifications" (page 461), change the line # 11 in "memory_management.c" as such:

    FROM:    #define PHYS_MEM_SIZE        256
    TO:      #define PHYS_MEM_SIZE        128

In order to perform the additional functionality of testing with both READS AND WRITES:

    FROM:    #define INPUT_ADDRESSES      "addresses/addresses.txt"
    TO:      #define INPUT_ADDRESSES      "addresses/addresses2.txt"

Other Files:
------------

- The file "addresses/addresses.txt" is the provided input address list, from the OSC book.
- The file "addresses/addresses2.txt" is the provided MODIFIED input address list, including reads and writes.
- The file "BACKING_STORE.bin" is the provided "backing store" disk image holding the data for the virtual memory.

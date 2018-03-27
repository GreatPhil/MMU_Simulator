#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_ADDRESSES      "addresses/addresses.txt"

/* Memory Size Parameters */
#define BUFFER_LEN           20
#define PAGE_SIZE            256
#define PAGE_TABLE_SIZE      256
#define PHYS_MEM_SIZE        256
#define TLB_SIZE             16

/* Masks for Virtual Address Processing */
#define VIRTUAL_PAGE_MASK    0x0000FF00
#define VIRTUAL_OFFSET_MASK  0x000000FF
#define VIRTUAL_PAGE_SHIFT   8
#define VIRTUAL_OFFSET_SHIFT 0

/* Masks for Page Table Entries */
#define PT_PAGE_VALID_MASK   0x80000000
#define PT_DIRTY_BIT_MASK    0x40000000
#define PT_LRU_COUNTER_MASK  0x00FFFF00
#define PT_FRAME_NUM_MASK    0x000000FF
#define PT_PAGE_VALID_SHIFT  31
#define PT_DIRTY_BIT_SHIFT   30
#define PT_LRU_COUNTER_SHIFT 8
#define PT_FRAME_NUM_SHIFT   0

/* Note: Must be Smaller than 2^16 based on above settings. */
#define PT_MAX_LRU_COUNTER   65000

/* Masks for TLB Entries */
#define TLB_REG_VALID_MASK   0x80000000
#define TLB_DIRTY_BIT_MASK   0x40000000
#define TLB_PAGE_NUM_MASK    0x0000FF00
#define TLB_FRAME_NUM_MASK   0x000000FF
#define TLB_REG_VALID_SHIFT  31
#define TLB_DIRTY_BIT_SHIFT  30
#define TLB_PAGE_NUM_SHIFT   8
#define TLB_FRAME_NUM_SHIFT  0


/* Counters for Statistics */
static int num_memory_accesses = 0;
static int num_page_faults     = 0;
static int num_tlb_hits        = 0;
static int num_memory_reads    = 0;
static int num_memory_writes   = 0;
static int num_dirty_swaps     = 0;


/* MMU - Page Table and Translation Lookaside Buffer (TLB) */
static int page_table[PAGE_TABLE_SIZE] = {0};
static int trans_look_buffer[TLB_SIZE] = {0};

/* Page Replacement Counter (for LRU Page Replacement) */
static int lru_counter = 0;

/* Physical Memory (i.e. RAM) */
static char physical_memory[PAGE_SIZE * PHYS_MEM_SIZE] = {0};


/****************************************
* Virtual Address Processing Functions
*****************************************/

/* getPageNumber - Returns the Page Number from a Virtual Address */
int getPageNumber (int address) {
  return (address & VIRTUAL_PAGE_MASK) >> VIRTUAL_PAGE_SHIFT;
}

/* getOffset - Returns the Page Offset from a Virtual Address */
int getOffset (int address) {
  return (address & VIRTUAL_OFFSET_MASK) >> VIRTUAL_OFFSET_SHIFT;
}


/****************************************
* Backing Store Processing Functions
*****************************************/

/* writeOutFrameToStore - Writes a Frame from physical memory back out to the
backing store.  Since the physical memory contents are not actually modified
(because no input write value is given in addresses2.txt), this effectively does
nothing except go through the motions of writing the "supposedly dirty" data
back out to the store. */
void writeOutFrameToStore(int phy_frame, int store_frame) {
  FILE *backing_store;
  backing_store = fopen("BACKING_STORE.bin", "rw+");
  fseek(backing_store, store_frame * PAGE_SIZE, SEEK_SET);
  fwrite(&physical_memory[phy_frame * PAGE_SIZE], 1, PAGE_SIZE, backing_store);
  fclose(backing_store);
}

/* readInFrameFromStore - Reads a frame to physical memory from backing store. */
void readInFrameFromStore(int phy_frame, int store_frame) {
  FILE *backing_store;
  backing_store = fopen("BACKING_STORE.bin", "r");
  fseek(backing_store, store_frame * PAGE_SIZE, SEEK_SET);
  fread(&physical_memory[phy_frame * PAGE_SIZE], 1, PAGE_SIZE, backing_store);
  fclose(backing_store);
}


/****************************************
* TLB Access Functions
*****************************************/

/* getFrameFromTLB - Returns the Frame Number from the TLB if the input_page
is contained in the TLB; otherwise this function returns -1 to indicate that
the page was not found within the TLB. */
int getFrameFromTLB(int input_page, int write_to_mem) {

  int valid, page, frame;

  /* Loop Through TLB */
  for (int i = 0; i < TLB_SIZE; i++) {

    /*  Extract Page Data fron TLB Entry */
    valid = (trans_look_buffer[i] & TLB_REG_VALID_MASK) >> TLB_REG_VALID_SHIFT;
    page  = (trans_look_buffer[i] & TLB_PAGE_NUM_MASK)  >> TLB_PAGE_NUM_SHIFT;
    frame = (trans_look_buffer[i] & TLB_FRAME_NUM_MASK) >> TLB_FRAME_NUM_SHIFT;

    /* If Page is Found, return the associated Frame Number */
    if (valid && (page == input_page)) {

      /* Set Dirty Bit on Write */
      if (write_to_mem ) {
        trans_look_buffer[i] = trans_look_buffer[i] | TLB_DIRTY_BIT_MASK;
      }

      /* Update Stats and Return Frame */
      num_tlb_hits++;
      return frame;
    }
  }

  /* If Page is Not Found, return -1 */
  return -1;
}

/* updateTLB - Updates the TLB with a new input page and frame number.  This
function implements a FIFO translation lookaside buffer (TLB), using next_reg
as the index of the next buffer register in the FIFO. */
void updateTLB(int input_page, int input_frame) {

  static int next_reg = 0;
  int valid, page, frame;

  /* New Entry for TLB Register */
  int new_tlb_input = TLB_REG_VALID_MASK |
                      (TLB_PAGE_NUM_MASK & (input_page << TLB_PAGE_NUM_SHIFT)) |
                      (TLB_FRAME_NUM_MASK & (input_frame << TLB_FRAME_NUM_SHIFT));

  /* Check if Page already exists in TLB (i.e. update to Frame Number only) */
  for (int i = 0; i < TLB_SIZE; i++) {
    valid = (trans_look_buffer[i] & TLB_REG_VALID_MASK) >> TLB_REG_VALID_SHIFT;
    page  = (trans_look_buffer[i] & TLB_PAGE_NUM_MASK)  >> TLB_PAGE_NUM_SHIFT;
    frame = (trans_look_buffer[i] & TLB_FRAME_NUM_MASK) >> TLB_FRAME_NUM_SHIFT;
    if (valid && (page == input_page)) {
      trans_look_buffer[i] = new_tlb_input;
      return;
    }
  }

  /* Check if TLB's Old Value has a Dirty Bit */
  if ((trans_look_buffer[next_reg] & TLB_REG_VALID_MASK) &&
      (trans_look_buffer[next_reg] & TLB_DIRTY_BIT_MASK)) {
    page  = (trans_look_buffer[next_reg] & TLB_PAGE_NUM_MASK)  >> TLB_PAGE_NUM_SHIFT;
    frame = (trans_look_buffer[next_reg] & TLB_FRAME_NUM_MASK) >> TLB_FRAME_NUM_SHIFT;
    writeOutFrameToStore(frame, page);
    num_dirty_swaps++;
  }

  /* Update TLB's Next Register within its FIFO */
  trans_look_buffer[next_reg] = new_tlb_input;

  /* Update Next Register Value */
  next_reg = next_reg + 1;
  if (next_reg >= TLB_SIZE) {
    next_reg = 0;
  }

}


/****************************************
* Page Table Access Functions
*****************************************/

/* getLeastRecentlyUsedFrame - Returns Frame Number from Page Table with oldest
LRU counter value, indicating that it was in fact the "least recently used"
frame.  However, if any frame is NOT already in the Page Table, then that frame
is considered to be the "least recently used" frame and is returned instead. */
int getLeastRecentlyUsedFrame() {

  int frame_found[PHYS_MEM_SIZE] = {0};
  int frame, frame_age, counter;
  int LRU_frame = -1;
  int oldest_age = -1;

  /* Check if All Frames are Already in Page Table */
  for (int i = 0; i < PAGE_TABLE_SIZE; i++) {
    if (page_table[i] & PT_PAGE_VALID_MASK) {
      frame = (page_table[i] & PT_FRAME_NUM_MASK) >> PT_FRAME_NUM_SHIFT;
      frame_found[frame] = 1;
    }
  }

  /* If not, return first frame not already in Page Table */
  for (int i = 0; i < PHYS_MEM_SIZE; i++) {
    if (!frame_found[i]) {
      return i;
    }
  }

  /* Otherwise, return the Least Recently Used Frame */
  for (int i = 0; i < PAGE_TABLE_SIZE; i++) {
    if (page_table[i] & PT_PAGE_VALID_MASK) {

      /* Extract Frame and LRU Counter from Page Entry */
      frame = (page_table[i] & PT_FRAME_NUM_MASK) >> PT_FRAME_NUM_SHIFT;
      counter = (page_table[i] & PT_LRU_COUNTER_MASK) >> PT_LRU_COUNTER_SHIFT;

      /* Get the LRU "Age" of the Frame */
      if (counter < lru_counter) {
        frame_age = lru_counter - counter;
      } else {
        frame_age = lru_counter + (PT_MAX_LRU_COUNTER - counter);
      }

      /* Update Oldest Frame */
      if (frame_age > oldest_age) {
        oldest_age = frame_age;
        LRU_frame = frame;
      }

    }
  }

  return LRU_frame;

}

/* getNextFrameForReplacement - Returns the next Frame of Physical Memory to
be replaced with a page from the Backing Store, in the case that a Page Fault
has occured and therefore the physical memory needs to be updated. */
int getNextFrameForReplacement() {
  static int frame = -1;
  if (PHYS_MEM_SIZE == PAGE_TABLE_SIZE) {
    frame = (frame + 1) & 0x000000FF;
  } else {
    frame = getLeastRecentlyUsedFrame();
  }
  return frame;
}

/* removeFrameFromPageTable - Clears a Frame from the Page Table.  If the frame
is "dirty", the memory contents are written out to the backing store, first. */
void removeFrameFromPageTable(int frame) {
  for (int i = 0; i < PAGE_TABLE_SIZE; i++) {

    /* Find Page Table Entry that matches Frame Number */
    if ((page_table[i] & PT_PAGE_VALID_MASK) &&
        ((page_table[i] & PT_FRAME_NUM_MASK) >> PT_FRAME_NUM_SHIFT) == frame) {

      /* If Page Table Dirty Bit is Set, Write Page Back to Store */
      if (page_table[i] & PT_DIRTY_BIT_MASK) {
        writeOutFrameToStore(frame, i);
        num_dirty_swaps++;
      }

      /* Clear the Page Table Entry */
      page_table[i] = 0;

    }
  }
}

/* getFrameFromPageTable - Checks the Page Table and returns the corresponding
frame number given and input page.  If a Page Fault occurs, new data is read
into memory and the page table and TLB are updated.  If the memory access is a
"write to memory", then the "dirty bit" is set in the Page Table. */
int getFrameFromPageTable(int page, int write_to_mem) {

  int frame = 0;

  /* Check if Page is already in Physical Memory */
  if (page_table[page] & PT_PAGE_VALID_MASK) {
    frame = (page_table[page] >> PT_FRAME_NUM_SHIFT) & PT_FRAME_NUM_MASK;
  } else {

    num_page_faults++;

    /* If Page Fault, Read in from Backing Store */
    frame = getNextFrameForReplacement();
    removeFrameFromPageTable(frame);
    readInFrameFromStore(frame, page);

    /* Update Page Table */
    page_table[page] = PT_PAGE_VALID_MASK |
                       ((frame << PT_FRAME_NUM_SHIFT) & PT_FRAME_NUM_MASK);

    /* Update Translation Lookaside Buffer (TLB) */
    updateTLB(page, frame);
  }

  /* Set Dirty Bit on Write */
  if (write_to_mem) {
    page_table[page] = page_table[page] | PT_DIRTY_BIT_MASK;
  }

  return frame;
}


/****************************************
* MAIN FUNCTION
*****************************************/

int main() {

  FILE *address_file;
  char buffer[BUFFER_LEN];
  char *line = buffer;
  char *endptr;
  int write_to_mem   = 0;
	unsigned long len  = BUFFER_LEN;
	unsigned long read = 0;

  int log_address, phy_address;
  int page, offset, frame;

  /* Read in Addresses */
	address_file = fopen(INPUT_ADDRESSES, "r");
	while ((read = getline(&line, &len, address_file)) != -1) {

    /* Determine if the Address is for Read or Write */
    write_to_mem = line[strlen(line)-3] == 'W';
    if (write_to_mem) {
      num_memory_writes++;
    } else {
      num_memory_reads++;
    }

    /* Terminate Line after Number (i.e. Ignore the W/R Character) */
    if (line[strlen(line)-3] == 'R' || line[strlen(line)-3] == 'W') {
      line[strlen(line)-4] = 0;
    }

    /* Calculate Page and Offset */
    log_address = (int) strtol(line, &endptr, 10);
    page = getPageNumber(log_address);
    offset = getOffset(log_address);

    /* Check TLB */
    frame = getFrameFromTLB(page, write_to_mem);

    /* Check Page Table */
    if (frame < 0) {
      frame = getFrameFromPageTable(page, write_to_mem);
    }

    /* Update LRU Counter */
    page_table[page] = page_table[page] & ~PT_LRU_COUNTER_MASK;
    page_table[page] = page_table[page] |
                       ((lru_counter << PT_LRU_COUNTER_SHIFT) & PT_LRU_COUNTER_MASK);
    lru_counter++;
    if (lru_counter == PT_MAX_LRU_COUNTER) {
      lru_counter = 0;
    }

    /* Calculate Physical Address */
    phy_address = ((frame & 0x000000FF) << 8) + (offset & 0x000000FF);

    /* Print Address and Value */
    printf("Virtual address: %d ", log_address);
    printf("Physical address: %d ", phy_address);
    printf("Value: %d\n", physical_memory[phy_address]);

    num_memory_accesses++;

	}

  /* Print Statistics --- QQQ Output More Stats */
  printf("\n====================================\n\n");
  printf("Info / Statistics:\n\n");

  printf("Page Table Size (# Pages):        %d\n", PAGE_TABLE_SIZE);
  printf("Physical Memory Size (# Frames):  %d\n", PHYS_MEM_SIZE);
  printf("TLB Size (# Page-Frame Regs):     %d\n", TLB_SIZE);
  printf("\n-------------------------------------\n\n");

  printf("Total Num Page Faults:  %d\n", num_page_faults);
  printf("Total TLB Hits:         %d\n", num_tlb_hits);
  printf("Page Fault Rate:        %f\n", (float) num_page_faults / (float) num_memory_accesses);
  printf("TLB Hit Rate:           %f\n", (float) num_tlb_hits / (float) num_memory_accesses);
  printf("\n-------------------------------------\n\n");

  printf("Total Num Reads:        %d\n", num_memory_reads);
  printf("Total Num Writes:       %d\n", num_memory_writes);
  printf("Total Num Dirty Swaps:  %d\n\n", num_dirty_swaps);

  /* Close Input File and Return */
	fclose(address_file);
  return 0;

}

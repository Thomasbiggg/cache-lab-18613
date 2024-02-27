#include "cachelab.h"
#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const int parse_buffer = 50;
const unsigned long largest_long = ~0UL;
const unsigned long mask = (1ul << 63);
const int hex_format = 16;

// Holds the arguments of each line in the traces file
typedef struct {
    char opt;
    unsigned long addr;
    unsigned long size;
} FileLine;

// The struct in cache
typedef struct {
    int dirty;
    unsigned long tag;
} Block;

// Hold the index of the 2-D cache, for LRU
// For exmaple, when load, push the index of the ele in the cache to LRU queue
typedef struct {
    unsigned long x;
    unsigned long y;
} LRU;

// The node of the linkedlist that stores LRU index
typedef struct QueueNode {
    LRU data;
    struct QueueNode *next;
} QueueNode;

// The array of linkedlist
typedef struct {
    QueueNode *top;
} LRUQueue;

// Declare a global pointer for the cache, queues and stats
Block **cache;
LRUQueue *queues;
csim_stats_t *res;

// Declare global var for the mask and offset -> for tag and set
unsigned long set_mask;
unsigned long tag_mask;

// Declare global var for the s E b
unsigned long set_size;
unsigned long offset_size;
unsigned long ways;

int process_trace_file(const char *filename);
FileLine parse_line(char *input);
void allocateCache(unsigned long sets, unsigned long lines);
void freeCache(unsigned long sets);
void allocateQueueArray(unsigned long numQueues);
void freeAllQueues(unsigned long numQueues);
void push(unsigned long set, LRU ele);
LRU pop(unsigned long set);
void operation(char opt, unsigned long addr);

int main(int argc, char **argv) {

    int opt;
    int hflag = 0, vflag = 0, sflag = 0, bflag = 0, Eflag = 0, tflag = 0;
    char *endptr, *t_arg = NULL;
    unsigned long s_ul, b_ul, E_ul;

    while ((opt = getopt(argc, argv, "hvs:b:E:t:")) != -1) {
        switch (opt) {
        case 'h':
            hflag = 1;
            break;

        case 'v':
            vflag = 1;
            break;

        case 's':
            sflag = 1;
            if (optarg[0] == '-') {
                fprintf(stderr,
                        "Error: Argument for -s must not be negative: %s\n",
                        optarg);
                exit(EXIT_FAILURE);
            }

            errno = 0; // Reset errno before conversion
            s_ul = strtoul(optarg, &endptr, 0);
            if (*endptr != '\0' ||
                errno ==
                    ERANGE) { // Check for invalid characters or range error
                fprintf(stderr, "Invalid argument for -s: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            set_size = s_ul;
            break;
        case 'b':
            bflag = 1;
            if (optarg[0] == '-') {
                fprintf(stderr,
                        "Error: Argument for -b must not be negative: %s\n",
                        optarg);
                exit(EXIT_FAILURE);
            }

            errno = 0; // Reset errno before conversion
            b_ul = strtoul(optarg, &endptr, 0);
            if (*endptr != '\0' ||
                errno ==
                    ERANGE) { // Check for invalid characters or range error
                fprintf(stderr, "Invalid argument for -b: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            offset_size = b_ul;
            break;

        case 'E':
            Eflag = 1;
            if (optarg[0] == '-') {
                fprintf(stderr,
                        "Error: Argument for -E must not be negative: %s\n",
                        optarg);
                exit(EXIT_FAILURE);
            }

            errno = 0; // Reset errno before conversion
            E_ul = strtoul(optarg, &endptr, 0);
            if (*endptr != '\0' || errno == ERANGE ||
                E_ul == 0) { // Check for invalid characters or range error
                fprintf(stderr, "Invalid argument for -E: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            ways = E_ul;
            break;

        case 't':
            tflag = 1;
            t_arg = optarg;
            break;

        default: /* '?' */
            fprintf(stderr,
                    "All four arguments -s, -b, -E, and -t are required.\n");
            exit(EXIT_FAILURE);
            break;
        }
    }

    // If -h is used
    if (hflag) {
        printf("Usage:\n./csim [-v] -s <s> -E <E> -b <b> -t <trace>\n "
               "./csim -h\n");
        exit(EXIT_SUCCESS);
    }

    // If -v is used
    if (vflag) {
        printf("Performing an action...\n");
    }

    // Check for mandatory arguments
    if (!sflag || !bflag || !Eflag || !tflag) {
        fprintf(stderr, "Error: Missing required arguments.\n");
        fprintf(stderr, "Usage: %s -s <s> -b <b> -E <E> -t <trace>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Check whether larger than cache address
    if (s_ul + b_ul > largest_long) {
        fprintf(stderr, "Error: Arguments too large.\n");
        exit(EXIT_FAILURE);
    }
    // Print the argument
    printf("Values entered: s=%lu, b=%lu, E=%lu, t=%s\n", s_ul, b_ul, E_ul,
           t_arg);

    // Instantiate the variables and mask
    unsigned long sets = (1ul << s_ul);
    unsigned long lines = (1ul << E_ul);
    unsigned long offsets = (1ul << b_ul);
    set_mask = (sets * offsets - 1ul) - (offsets - 1ul);
    tag_mask = (mask - 1ul) ^ ((1ul << (s_ul + b_ul)) - 1ul);

    // Instantiate the cache
    allocateCache(sets, lines);

    // Instantiate queues that keeps record of LRU
    allocateQueueArray(sets);

    // Instantiate the csim_stats_t for print summary
    res = malloc(sizeof(csim_stats_t));
    if (res == NULL) {
        printf("stat init failed");
        exit(EXIT_FAILURE);
    }
    res->hits = 0;
    res->misses = 0;
    res->evictions = 0;
    res->dirty_bytes = 0;
    res->dirty_evictions = 0;

    // Parse file one line by one line
    int exit_code = process_trace_file(t_arg);

    printSummary(res);

    // Free the cache, queues, and the stats
    freeCache(sets);
    freeAllQueues(sets);
    free(res);

    exit(exit_code);
}

/** Process a memory-access trace file. *
 * @param trace Name of the trace file to process.
 * @return 0 if successful, 1 if there were errors. */
int process_trace_file(const char *trace) {
    FILE *tfp = fopen(trace, "rt");
    if (!tfp) {
        fprintf(stderr, "Error opening %s: %s\n", trace, strerror(errno));
        return 1;
    }
    char linebuf[parse_buffer]; // How big should LINELEN be?
    int parse_error = 0;
    while (fgets(linebuf, parse_buffer, tfp)) {
        // Parse the line of text in linebuf.
        FileLine l = parse_line(linebuf);
        char opt = l.opt;
        unsigned long addr = l.addr;
        unsigned long size = l.size;

        printf("Values entered: opt=%c, addr=%lu, size=%lu\n", opt, addr, size);

        operation(opt, addr);
    }
    fclose(tfp);
    // Why do I return
    return parse_error;
}

/** Process each line of the memory-access trace file. *
 * @param input Each line of the trace file.
 * @return FileLine struct that holds the opt, addr, and size. */
FileLine parse_line(char *input) {
    FileLine l;
    // Just to make it as large as possible
    char hexStr[parse_buffer];
    char sizeStr[parse_buffer];
    // Find the space and comma positions
    char *spacePos = strchr(input, ' ');
    char *commaPos = strchr(input, ',');

    char *endptr = NULL;

    if (spacePos != NULL && commaPos != NULL) {
        // Extract the operation character
        l.opt = input[0];
        if (l.opt != 'S' && l.opt != 'L') {
            printf("Invalid operator: %c\n", l.opt);
            exit(EXIT_FAILURE);
        }

        // Extract the hexadecimal address
        size_t hexLen = (size_t)(commaPos - spacePos - 1);
        strncpy(hexStr, spacePos + 1, hexLen);
        hexStr[hexLen] = '\0'; // Null-terminate the string
        // Convert hex string to number
        errno = 0; // Reset errno before conversion
        l.addr = strtoul(hexStr, &endptr, hex_format);
        if (*endptr != '\0' || errno == ERANGE ||
            l.addr > mask) { // Check for invalid characters or range error
            fprintf(stderr, "Invalid address: %s\n", hexStr);
            exit(EXIT_FAILURE);
        }

        // Extract the size
        char *lastEle = input + strlen(input) - 1;
        size_t sizeLen = (size_t)(lastEle - commaPos - 1);
        strncpy(sizeStr, commaPos + 1, sizeLen);
        sizeStr[sizeLen] = '\0';
        errno = 0; // Reset errno before conversion
        l.size = strtoul(sizeStr, &endptr, 0);
        if (*endptr != '\0' ||
            errno == ERANGE) { // Check for invalid characters or range error
            fprintf(stderr, "Invalid size: %s\n", sizeStr);
            exit(EXIT_FAILURE);
        }
    } else {
        printf("Input format error %s\n", input);
        exit(EXIT_FAILURE);
    }

    return l;
}

/** Allocate memory for 2-D array cache. *
 * @param sets Number of sets the cache have.
 * @param lines Number of lines per set. */
void allocateCache(unsigned long sets, unsigned long lines) {
    // Allocate memory for rows
    cache = (Block **)malloc(sets * sizeof(Block *));
    if (cache == NULL) {
        // Handle allocation failure
        printf("Failed to allocate memory for Cache.\n");
        exit(EXIT_FAILURE);
    }
    for (unsigned long i = 0; i < sets; i++) {
        // Allocate memory for columns in each row
        cache[i] = (Block *)malloc(lines * sizeof(Block));
        if (cache[i] != NULL) {
            for (unsigned long j = 0; j < lines; j++) {
                cache[i][j].dirty = 0;        // Initial state, not dirty
                cache[i][j].tag = UINT64_MAX; // Indicating uninitialized
            }
        }
    }
}

/** Free memory for 2-D array cache. *
 * @param sets Number of sets the cache have.*/
void freeCache(unsigned long sets) {
    // Free each row
    for (unsigned long i = 0; i < sets; i++) {
        free(cache[i]);
    }
    // Free the row pointers
    free(cache);
}

/** Allocate memory for an array of linkedlist that keeps record of LRU element*
 * @param numQueues Number of sets the cache have.*/
void allocateQueueArray(unsigned long numQueues) {
    queues = (LRUQueue *)malloc(numQueues * sizeof(LRUQueue));
    if (queues == NULL) {
        printf("Failed to allocate memory for queues.\n");
        exit(EXIT_FAILURE);
    }

    // Initialize each Queue
    for (unsigned long i = 0; i < numQueues; i++) {
        queues[i].top = NULL;
    }
}

/** Free the memory of linkedlists that keeps record of LRU element*
 * @param numQueues Number of sets the cache have.*/
void freeAllQueues(unsigned long numQueues) {
    for (unsigned long i = 0; i < numQueues; i++) {
        QueueNode *node = queues[i].top;
        while (node) {
            QueueNode *temp = node;
            node = node->next;
            free(temp); // Free the current node
        }
    }
    free(queues); // Free the array of queues after all individual queues are
                  // freed
}

/** Push the element to the head of the linkedlist*
 * @param set The current set of the cache
 * @param ele LRU node that keeps record of the index of cache */
void push(unsigned long set, LRU ele) {
    QueueNode *newNode = (QueueNode *)malloc(sizeof(QueueNode));
    if (newNode == NULL) {
        printf("Malloc new node failed.\n");
        exit(EXIT_FAILURE);
    }

    newNode->data = ele;
    newNode->next = queues[set].top;
    queues[set].top = newNode;
}

/** Pop the last element of the queue*
 * @param set The current set of the cache */
LRU pop(unsigned long set) {
    if (queues[set].top == NULL) {
        printf("Queue is empty.\n");
        exit(EXIT_FAILURE);
    }

    // If there is only one item in the list
    if (queues[set].top->next == NULL) {
        LRU poppedEle = queues[set].top->data;
        free(queues[set].top);
        queues[set].top = NULL;
        return poppedEle;
    }

    // Traverse to the second-to-last node
    QueueNode *current = queues[set].top;
    while (current->next->next != NULL) {
        current = current->next;
    }

    // Now, current is the second-to-last node
    LRU poppedEle = current->next->data; // The data to return
    free(current->next);                 // Free the last node
    current->next = NULL;                // Remove the last node from the list

    return poppedEle;
}

/** Deal with the operations of the cache*
 * @param opt 'S' or 'L'
 * @param addr The address passed from the trace file */
void operation(char opt, unsigned long addr) {
    unsigned long offset_byte = (1 << offset_size);
    unsigned long set = (addr & set_mask) >> offset_size;
    unsigned long curTag = (addr & tag_mask) >> (set_size + offset_size);
    unsigned long loopTime = 0;
    // loop through the lines to see if the line is empty or the tag hits
    for (unsigned long ind = 0; ind < ways; ind++) {
        // 1st condition when the line is not full
        if (cache[set][ind].tag == UINT64_MAX) {
            printf("Miss, set: %lu curTag: %lu\n", set, curTag);

            // Push the index of the cache to the Queue
            LRU tempLRU = {set, ind};
            push(set, tempLRU);

            // If the store misses, set dirty bit
            if (opt == 'S') {
                cache[set][ind].dirty = 1;
                res->dirty_bytes += offset_byte;
            }
            cache[set][ind].tag = curTag;

            res->misses += 1;

            break;
        }
        // 2nd condition where the tag hits
        else if (cache[set][ind].tag == curTag) {
            printf("Hit, set: %lu curTag: %lu\n", set, curTag);
            // Pop and push the index of the cache to the Queue

            unsigned long top_y = queues[set].top->data.y;
            if (top_y != ind) {
                LRU popLRU = pop(set);
                push(set, popLRU);
            }

            // If the cahce is load first, but then a store hits, the dirty
            // should set to one
            if (cache[set][ind].dirty == 0) {
                if (opt == 'S') {
                    cache[set][ind].dirty = 1;
                    res->dirty_bytes += offset_byte;
                }
            }

            res->hits += 1;

            break;
        }
        loopTime += 1;
    }
    // 3rd condition evic if didn't find an empty line
    if (loopTime == ways) {
        printf("Miss Eviction, set: %lu curTag: %lu\n", set, curTag);

        // Pop and push the index of the cache to the Queue
        LRU popLRU = pop(set);
        push(set, popLRU);
        unsigned long x_set = popLRU.x;
        unsigned long y_line = popLRU.y;

        // If the operation is store set dirty bit because it is a new value
        // that need to be writen to main memory
        if (cache[x_set][y_line].dirty == 1) {
            if (opt == 'L') {
                // It loads from memory so dirty bit will be set to 0
                cache[x_set][y_line].dirty = 0;
                res->dirty_bytes -= offset_byte;
            }
            res->dirty_evictions += offset_byte;
        }
        // The dirty is 0 but the store causes eviction
        else {
            if (opt == 'S') {
                // It loads from memory so dirty bit will be set to 0
                cache[x_set][y_line].dirty = 1;
                res->dirty_bytes += offset_byte;
            }
        }

        cache[x_set][y_line].tag = curTag;

        // Add evictions and misses
        res->evictions += 1;
        res->misses += 1;
    }
}

// void printSummary(const csim_stats_t *stats) {
//     printf("hits:%lu misses:%lu evictions:%lu dirty_bytes_in_cache:%lu
//     dirty_bytes_evicted:%lu", res->hits, res->misses, res->evictions,
//     res->dirty_bytes, res->dirty_evictions);
// }

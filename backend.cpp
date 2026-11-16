#include <iostream>
#include <string>
#include <cstring>
#include <regex>
#include <vector>
#include <algorithm>
#include <new>

#ifndef DMA_SIMULATOR_TEST
#include "httplib.h"
#endif
using namespace std;
struct Block {
    int id;
    int start;
    int size;
    bool free;
    bool reallocated;
    char* data;
    string type;
    vector<bool> initialized;
    Block* next;
};

class MemorySimulator {

    Block* head;
    int totalSize;
    int nextId;

    // Create a free block. Keeping this setup in one place avoids repeated code.
    Block* createFreeBlock(int start, int size, Block* nextBlock) {
        Block* block = new (nothrow) Block{};

        if (block == nullptr)
            return nullptr;

        block->id = 0;
        block->start = start;
        block->size = size;
        block->free = true;
        block->reallocated = false;
        block->data = nullptr;
        block->type = "free";
        block->next = nextBlock;

        return block;
    }

    Block* findAllocation(int id) {
        Block* current = head;

        while (current != nullptr) {
            if (!current->free && current->id == id)
                return current;

            current = current->next;
        }

        return nullptr;
    }

public:

    // Start with one large free block covering the whole memory pool.
    MemorySimulator(int size) {
        totalSize = size;
        nextId = 1;
        head = createFreeBlock(0, size, nullptr);

        if (head == nullptr)
            throw bad_alloc();

        cout << "Memory Initialized: " << size << " Bytes" << endl;
    }

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
Block* findBlock(int size, const string& strategy) {
        Block* current = head;
        Block* selected = nullptr;

        while (current != nullptr) {
            if (current->free && current->size >= size) {

                if (strategy == "first")
                    return current;

                if (selected == nullptr) {
                    selected = current;
                }
                else if (strategy == "best" && current->size < selected->size) {
                    selected = current;
                }
                else if (strategy == "worst" && current->size > selected->size) {
                    selected = current;
                }
            }

            current = current->next;
        }

        return selected;
    }

    int allocate(int size, const string& strategy, const string& type) {
        if (size < 1 || size > totalSize)
            return -1;

        if (type != "malloc" && type != "calloc")
            return -1;

        if (strategy != "first" && strategy != "best" && strategy != "worst")
            return -1;

        Block* selected = findBlock(size, strategy);

        if (selected == nullptr)
            return -1;


        char* newData = new (nothrow) char[size];
        if (newData == nullptr)
            return -1;

        vector<bool> newInitialization;
        try {
            newInitialization.assign(size, type == "calloc");
        }
        catch (...) {
            delete[] newData;
            return -1;
        }

        Block* remainingBlock = nullptr;
        if (selected->size > size) {
            remainingBlock = createFreeBlock(
                selected->start + size,
                selected->size - size,
                selected->next
            );

            if (remainingBlock == nullptr) {
                delete[] newData;
                return -1;
            }
        }

        if (type == "calloc")
            memset(newData, 0, size);

        if (remainingBlock != nullptr)
            selected->next = remainingBlock;

        selected->size = size;
        selected->free = false;
        selected->reallocated = false;
        selected->id = nextId++;
        selected->type = type;
        selected->data = newData;
        selected->initialized.swap(newInitialization);

        return selected->id;
    }
  int reallocate(int id, int newSize) {
        if (newSize < 1 || newSize > totalSize)
            return -1;

        Block* current = findAllocation(id);
        if (current == nullptr)
            return -1;

        int oldSize = current->size;

        if (newSize == oldSize)
            return id;


        if (newSize < oldSize) {
            char* newData = new (nothrow) char[newSize];
            if (newData == nullptr)
                return -1;

            vector<bool> newInitialization;
            try {
                newInitialization.assign(
                    current->initialized.begin(),
                    current->initialized.begin() + newSize
                );
            }
            catch (...) {
                delete[] newData;
                return -1;
            }

            Block* freePart = createFreeBlock(
                current->start + newSize,
                oldSize - newSize,
                current->next
            );

            if (freePart == nullptr) {
                delete[] newData;
                return -1;
            }

            memcpy(newData, current->data, newSize);
            delete[] current->data;

            current->data = newData;
            current->initialized.swap(newInitialization);
            current->size = newSize;
            current->reallocated = true;
            current->next = freePart;

            mergeBlocks();
            return id;
        }


        int extraSize = newSize - oldSize;
        Block* nextBlock = current->next;

        if (nextBlock != nullptr && nextBlock->free && nextBlock->size >= extraSize) {
            char* newData = new (nothrow) char[newSize];
            if (newData == nullptr)
                return -1;

            vector<bool> newInitialization;
            try {
                newInitialization = current->initialized;
                newInitialization.resize(newSize, false);
            }
            catch (...) {
                delete[] newData;
                return -1;
            }

            memcpy(newData, current->data, oldSize);
            delete[] current->data;
            current->data = newData;
            current->initialized.swap(newInitialization);

            if (nextBlock->size == extraSize) {
                current->next = nextBlock->next;
                delete nextBlock;
            }
            else {
                nextBlock->start += extraSize;
                nextBlock->size -= extraSize;
            }

            current->size = newSize;
            current->reallocated = true;
            return id;
        }



        string originalType = current->type;
        int newId = allocate(newSize, "first", originalType);

        if (newId == -1)
            return -1;

        Block* newBlock = findAllocation(newId);
        if (newBlock == nullptr) {
            deallocate(newId);
            return -1;
        }

        memcpy(newBlock->data, current->data, oldSize);

        try {
            for (int index = 0; index < oldSize; index++)
                newBlock->initialized[index] = current->initialized[index];

            for (int index = oldSize; index < newSize; index++)
                newBlock->initialized[index] = false;
        }
        catch (...) {
            deallocate(newId);
            return -1;
        }

        newBlock->reallocated = true;
        deallocate(id);

        return newId;
    }
bool deallocate(int id) {
        Block* current = findAllocation(id);

        if (current == nullptr)
            return false;

        delete[] current->data;
        current->data = nullptr;
        current->initialized.clear();
        current->free = true;
        current->reallocated = false;
        current->id = 0;
        current->type = "free";

        mergeBlocks();
        return true;
    }

    // Join neighbouring free blocks into one larger free block.
    void mergeBlocks() {
        Block* current = head;

        while (current != nullptr && current->next != nullptr) {
            if (current->free && current->next->free) {
                Block* blockToDelete = current->next;
                current->size += blockToDelete->size;
                current->next = blockToDelete->next;
                delete blockToDelete;
            }
            else {
                current = current->next;
            }
        }
        
    }

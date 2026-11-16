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
string getMemory() const {
        string result = "[";
        Block* current = head;

        while (current != nullptr) {
            result += "{\"id\":" + to_string(current->id);
            result += ",\"start\":" + to_string(current->start);
            result += ",\"size\":" + to_string(current->size);
            result += ",\"end\":" + to_string(current->start + current->size - 1);
            result += ",\"free\":" + string(current->free ? "true" : "false");
            result += ",\"type\":\"" + current->type + "\"";
            result += ",\"reallocated\":" + string(current->reallocated ? "true" : "false");
            result += "}";

            if (current->next != nullptr)
                result += ",";

            current = current->next;
        }

        result += "]";
        return result;
    }
string getInspection(int id) const {
        Block* current = head;

        while (current != nullptr) {
            if (!current->free && current->id == id) {
                const int previewSize = min(current->size, 24);
                string result = "{\"success\":true";
                result += ",\"id\":" + to_string(current->id);
                result += ",\"start\":" + to_string(current->start);
                result += ",\"size\":" + to_string(current->size);
                result += ",\"type\":\"" + current->type + "\"";
                result += ",\"reallocated\":" + string(current->reallocated ? "true" : "false");
                result += ",\"bytes\":[";

                for (int index = 0; index < previewSize; index++) {
                    if (current->initialized[index]) {
                        result += to_string(static_cast<unsigned char>(current->data[index]));
                    }
                    else {
                        result += "null";
                    }

                    if (index + 1 < previewSize)
                        result += ",";
                }

                result += "]}";
                return result;
            }

            current = current->next;
        }

        return "{\"success\":false,\"message\":\"Allocation ID not found\"}";
}
string getStats() const {
        int used = 0;
        int freeMemory = 0;
        Block* current = head;

        while (current != nullptr) {
            if (current->free)
                freeMemory += current->size;
            else
                used += current->size;

            current = current->next;
        }

        return "{\"total\":" + to_string(totalSize) +
               ",\"used\":" + to_string(used) +
               ",\"free\":" + to_string(freeMemory) + "}";
    }

    int getTotalSize() const {
        return totalSize;
    }
~MemorySimulator() {
        Block* current = head;

        while (current != nullptr) {
            Block* blockToDelete = current;
            current = current->next;
            delete[] blockToDelete->data;
            delete blockToDelete;
        }
    }
};
ass MemoryOperation {
public:
    virtual int execute(MemorySimulator& memory, int value,
                        const string& strategy) const = 0;
    virtual ~MemoryOperation() {}
};

class MallocOperation : public MemoryOperation {
public:
    int execute(MemorySimulator& memory, int size,
                const string& strategy) const override {
        return memory.allocate(size, strategy, "malloc");
    }
};

class CallocOperation : public MemoryOperation {
public:
    int execute(MemorySimulator& memory, int size,
                const string& strategy) const override {
        return memory.allocate(size, strategy, "calloc");
    }
};

class FreeOperation : public MemoryOperation {
public:
    int execute(MemorySimulator& memory, int id,
                const string&) const override {
        return memory.deallocate(id) ? id : -1;
    }
int getNumber(const string& body, const string& key) {
    regex pattern("\\\"" + key + "\\\"\\s*:\\s*(-?\\d+)");
    smatch match;

    if (regex_search(body, match, pattern)) {
        try {
            return stoi(match[1]);
        }
        catch (...) {
            return -1;
        }
    }

    return -1;
}


string getString(const string& body, const string& key) {
    regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    smatch match;

    if (regex_search(body, match, pattern))
        return match[1];

    return "";
}

#ifndef DMA_SIMULATOR_TEST
int main() {


    MemorySimulator* memory = new MemorySimulator(2048);
    httplib::Server server;

    MallocOperation mallocOperation;
    CallocOperation callocOperation;
    FreeOperation freeOperation;


    auto addHeaders = [](httplib::Response& response) {
        response.set_header("Access-Control-Allow-Origin", "*");
        response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response.set_header("Access-Control-Allow-Headers", "Content-Type");
    };

    server.Options(R"(/.*)", [&](const httplib::Request&, httplib::Response& response) {
        addHeaders(response);
        response.status = 204;
    });


    server.Get("/health", [&](const httplib::Request&, httplib::Response& response) {
        addHeaders(response);
        response.set_content("{\"success\":true,\"message\":\"C++ server is running\"}", "application/json");
    });


    server.Post("/reset", [&](const httplib::Request& request, httplib::Response& response) {
        addHeaders(response);
        int newSize = getNumber(request.body, "size");

        if (newSize < 1 || newSize > 102400) {
            response.status = 400;
            response.set_content("{\"success\":false,\"message\":\"Pool size must be between 1 and 102400 bytes.\"}", "application/json");
            return;
        }

        try {
            MemorySimulator* newMemory = new MemorySimulator(newSize);
            delete memory;
            memory = newMemory;
        }
        catch (...) {
            response.status = 500;
            response.set_content("{\"success\":false,\"message\":\"Could not create the requested memory pool.\"}", "application/json");
            return;
        }

        response.set_content(
            "{\"success\":true,\"message\":\"Memory pool reset.\",\"total\":" + to_string(newSize) + "}",
            "application/json"
        );
    });


    server.Post("/allocate", [&](const httplib::Request& request, httplib::Response& response) {
        addHeaders(response);

        int size = getNumber(request.body, "size");
        string strategy = getString(request.body, "strategy");
        string type = getString(request.body, "type");

        if (size < 1 || size > memory->getTotalSize()) {
            response.status = 400;
            response.set_content("{\"success\":false,\"message\":\"Allocation size must fit inside the memory pool.\"}", "application/json");
            return;
        }

        if (strategy != "first" && strategy != "best" && strategy != "worst") {
            response.status = 400;
            response.set_content("{\"success\":false,\"message\":\"Choose First Fit, Best Fit or Worst Fit.\"}", "application/json");
            return;
        }

        const MemoryOperation* operation = nullptr;
        if (type == "malloc")
            operation = &mallocOperation;
        else if (type == "calloc")
            operation = &callocOperation;
        else {
            response.status = 400;
            response.set_content("{\"success\":false,\"message\":\"Allocation type must be malloc or calloc.\"}", "application/json");
            return;
        }

        int id = operation->execute(*memory, size, strategy);
        if (id == -1) {
            response.status = 400;
            response.set_content("{\"success\":false,\"message\":\"Allocation failed: no suitable free block is available.\"}", "application/json");
            return;
        }

        response.set_content(
            "{\"success\":true,\"id\":" + to_string(id) + ",\"message\":\"Allocation successful.\"}",
            "application/json"
        );
    });


    server.Post("/realloc", [&](const httplib::Request& request, httplib::Response& response) {
        addHeaders(response);
        int id = getNumber(request.body, "id");
        int newSize = getNumber(request.body, "size");

        if (id < 1 || newSize < 1 || newSize > memory->getTotalSize()) {
            response.status = 400;
            response.set_content("{\"success\":false,\"message\":\"Choose a valid allocation and a new size within the pool.\"}", "application/json");
            return;
        }

        int newId = memory->reallocate(id, newSize);
        if (newId == -1) {
            response.status = 400;
            response.set_content("{\"success\":false,\"message\":\"Reallocation failed. The original allocation was kept unchanged.\"}", "application/json");
            return;
        }

        response.set_content(
            "{\"success\":true,\"id\":" + to_string(newId) + ",\"message\":\"Reallocation successful.\"}",
            "application/json"
        );
    });


    server.Post("/free", [&](const httplib::Request& request, httplib::Response& response) {
        addHeaders(response);
        int id = getNumber(request.body, "id");

        bool success = freeOperation.execute(*memory, id, "") != -1;
        if (!success) {
            response.status = 400;
            response.set_content("{\"success\":false,\"message\":\"That allocation ID does not exist.\"}", "application/json");
            return;
        }

        response.set_content("{\"success\":true,\"message\":\"Memory released and adjacent free blocks merged.\"}", "application/json");
    });


    server.Post("/inspect", [&](const httplib::Request& request, httplib::Response& response) {
        addHeaders(response);
        int id = getNumber(request.body, "id");
        string result = memory->getInspection(id);

        if (result.find("\"success\":false") != string::npos)
            response.status = 404;

        response.set_content(result, "application/json");
    });

    server.Get("/memory", [&](const httplib::Request&, httplib::Response& response) {
        addHeaders(response);
        response.set_content(memory->getMemory(), "application/json");
    });

    server.Get("/stats", [&](const httplib::Request&, httplib::Response& response) {
        addHeaders(response);
        response.set_content(memory->getStats(), "application/json");
    });


    if (!server.set_mount_point("/", "./frontend")) {
        cerr << "Could not find the frontend folder. Run the program from the project folder." << endl;
        delete memory;
        return 1;
    }

    cout << "\nDynamic Memory Allocation Simulator is running." << endl;
    cout << "Open http://localhost:8080 in your browser." << endl;
    cout << "Keep this terminal open while using the simulator." << endl;

    bool started = server.listen("127.0.0.1", 8080);

    if (!started)
        cerr << "Could not start the server. Port 8080 may already be in use." << endl;

    delete memory;
    return started ? 0 : 1;
}
#endif

"use strict";
const $ = id => document.getElementById(id);

const ui = {
    poolSize: $("pool-size"),
    poolSlider: $("pool-slider"),
    malloc: $("malloc-mode"),
    calloc: $("calloc-mode"),
    realloc: $("realloc-mode"),
    allocationForm: $("allocation-form"),
    allocationSize: $("allocation-size"),
    elementCount: $("element-count"),
    elementSize: $("element-size"),
    reallocId: $("realloc-id"),
    reallocSize: $("realloc-size"),
    allocateButton: $("allocate-button"),
    freeForm: $("free-form"),
    freeId: $("free-id"),
    freeButton: $("free-button"),
    strategy: $("strategy"),
    memoryBar: $("memory-bar"),
    addressTicks: $("address-ticks"),
    allocationRows: $("allocation-rows"),
    history: $("operation-history"),
    clearHistory: $("clear-history"),
    status: $("status"),
    inspectionDialog: $("allocation-dialog"),
    bytePreview: $("byte-preview")
};

let memoryBlocks = [];
let totalMemory = 2048;
let selectedId = 0;
let currentMode = "malloc";
let busy = false;
const historyEntries = [];

function showStatus(message, isError = false) {
    ui.status.textContent = message;
    ui.status.classList.toggle("error", isError);
}

async function sendRequest(path, values = null) {
    const options = values === null
        ? { method: "GET", cache: "no-store" }
        : {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(values)
        };

    let response;

    try {
        response = await fetch(path, options);
    }
    catch (error) {
        throw new Error(
            "Cannot connect to the C++ server at http://localhost:8080. " +
            "Run start_windows.bat and open http://localhost:8080. Do not open index.html directly."
        );
    }

    let result;

    try {
        result = await response.json();
    }
    catch (error) {
        throw new Error("The C++ server returned an invalid response. Check the terminal for errors.");
    }

    if (!response.ok || result.success === false) {
        throw new Error(result.message || `The server returned error ${response.status}.`);
    }

    return result;
}

function readPositiveNumber(value, fieldName, maximum = 102400) {
    const number = Number(value);

    if (!Number.isSafeInteger(number) || number < 1 || number > maximum) {
        throw new Error(`${fieldName} must be a whole number from 1 to ${maximum}.`);
    }

    return number;
}

function updateControls() {
    const hasAllocations = memoryBlocks.some(block => !block.free);
    const hasState = memoryBlocks.length > 0;

    ui.malloc.disabled = busy;
    ui.calloc.disabled = busy;
    ui.realloc.disabled = busy;
    ui.poolSize.disabled = busy;
    ui.poolSlider.disabled = busy;
    ui.strategy.disabled = busy;

    ui.allocationSize.disabled = busy || currentMode !== "malloc";
    ui.elementCount.disabled = busy || currentMode !== "calloc";
    ui.elementSize.disabled = busy || currentMode !== "calloc";
    ui.reallocId.disabled = busy || currentMode !== "realloc" || !hasAllocations;
    ui.reallocSize.disabled = busy || currentMode !== "realloc";
    ui.allocateButton.disabled = busy || !hasState;

    ui.freeId.disabled = busy || !hasAllocations;
    ui.freeButton.disabled = busy || !hasAllocations;
    ui.clearHistory.disabled = busy;
}

function setBusy(isBusy) {
    busy = isBusy;
    updateControls();
}

function setMode(nextMode) {
    currentMode = nextMode;

    const modes = [
        { name: "malloc", button: ui.malloc, fields: $("malloc-fields") },
        { name: "calloc", button: ui.calloc, fields: $("calloc-fields") },
        { name: "realloc", button: ui.realloc, fields: $("realloc-fields") }
    ];

    modes.forEach(item => {
        const selected = currentMode === item.name;
        item.button.classList.toggle("active", selected);
        item.button.setAttribute("aria-pressed", String(selected));
        item.fields.hidden = !selected;
    });

    if (currentMode === "malloc") {
        $("allocation-heading").textContent = "Size (bytes)";
        ui.allocateButton.textContent = "Allocate";
    }
    else if (currentMode === "calloc") {
        $("allocation-heading").textContent = "Count × element size";
        ui.allocateButton.textContent = "Allocate";
    }
    else {
        $("allocation-heading").textContent = "Resize an allocation";
        ui.allocateButton.textContent = "Reallocate";
    }

    updateControls();
}

async function loadMemory() {
    const blocks = await sendRequest("/memory");
    const statistics = await sendRequest("/stats");

    if (!Array.isArray(blocks))
        throw new Error("The memory endpoint did not return a block list.");

    memoryBlocks = blocks;
    totalMemory = statistics.total;

    if (!memoryBlocks.some(block => !block.free && block.id === selectedId))
        selectedId = 0;

    ui.poolSize.value = totalMemory;
    updateSlider(totalMemory);
    $("total-value").textContent = `${statistics.total} B`;
    $("used-value").textContent = `${statistics.used} B`;
    $("free-value").textContent = `${statistics.free} B`;

    drawMemoryMap();
    drawAllocationTable();
    updateAllocationSelectors();
    updateControls();
}

function updateSlider(value) {
    ui.poolSlider.value = value;

    const percentage = ((Number(value) - 1) / (102400 - 1)) * 100;
    ui.poolSlider.style.setProperty("--progress", `${percentage}%`);
}

function getAddressInterval() {
    const availableWidth = ui.memoryBar.clientWidth || 800;
    const maximumIntervals = Math.max(2, Math.min(8, Math.floor(availableWidth / 105)));
    const roughInterval = totalMemory / maximumIntervals;
    const unit = totalMemory >= 256 ? 256 : 1;
    return Math.max(unit, Math.ceil(roughInterval / unit) * unit);
}

function drawMemoryMap() {
    ui.memoryBar.replaceChildren();
    ui.addressTicks.replaceChildren();

    memoryBlocks.forEach(block => {
        const element = document.createElement(block.free ? "div" : "button");
        element.className = "memory-block";

        if (block.free) {
            element.classList.add("free");
        }
        else {
            element.classList.add(block.type === "calloc" ? "calloc" : "malloc");
            element.type = "button";
            element.addEventListener("click", () => inspectAllocation(block.id));
            element.classList.toggle("selected", block.id === selectedId);
            element.classList.toggle("reallocated", Boolean(block.reallocated));
        }

        const left = (block.start / totalMemory) * 100;
        const width = (block.size / totalMemory) * 100;
        element.style.left = `${left}%`;
        element.style.width = `${width}%`;

        const idLabel = document.createElement("strong");
        const sizeLabel = document.createElement("span");
        idLabel.textContent = block.free ? "Free" : `ID${block.id}`;
        sizeLabel.textContent = `${block.size} B`;
        element.append(idLabel, sizeLabel);
        const widthInPixels = (block.size / totalMemory) * ui.memoryBar.clientWidth;
        element.classList.toggle("compact", widthInPixels < 78);
        element.classList.toggle("id-only", widthInPixels >= 78 && widthInPixels < 132);

        const blockName = block.free ? "Free memory" : `ID${block.id} ${block.type}`;
        const resizedText = block.reallocated ? ", resized using realloc" : "";
        element.title = `${blockName}${resizedText} — ${block.size} bytes, addresses ${block.start} to ${block.start + block.size - 1}. Select to inspect.`;
        element.setAttribute("aria-label", element.title);

        ui.memoryBar.append(element);
    });
    const interval = getAddressInterval();
    const addresses = [];

    for (let address = 0; address < totalMemory; address += interval) {
        addresses.push(address);
    }

    const lastAddress = addresses[addresses.length - 1];
    if (totalMemory - lastAddress >= interval * 0.55)
        addresses.push(totalMemory);
    else
        addresses[addresses.length - 1] = totalMemory;

    addresses.forEach((address, index) => {
        const tick = document.createElement("span");
        tick.className = "address-tick";

        if (index === 0)
            tick.classList.add("first");
        if (index === addresses.length - 1)
            tick.classList.add("last");

        tick.style.left = `${(address / totalMemory) * 100}%`;
        tick.textContent = address.toLocaleString("en-IN");
        ui.addressTicks.append(tick);
    });
}

function drawAllocationTable() {
    ui.allocationRows.replaceChildren();
    const allocations = memoryBlocks.filter(block => !block.free);

    if (allocations.length === 0) {
        const row = document.createElement("tr");
        row.className = "empty-row";
        const cell = document.createElement("td");
        cell.colSpan = 5;
        cell.textContent = "No active allocations.";
        row.append(cell);
        ui.allocationRows.append(row);
        return;
    }

    allocations.forEach(block => {
        const row = document.createElement("tr");
        row.tabIndex = 0;
        row.classList.toggle("selected", block.id === selectedId);

        const operationName = block.reallocated
            ? `${block.type} · realloc`
            : block.type;
        const values = [
            `ID${block.id}`,
            operationName,
            block.start.toLocaleString("en-IN"),
            `${block.size.toLocaleString("en-IN")} B`,
            (block.start + block.size - 1).toLocaleString("en-IN")
        ];

        values.forEach(value => {
            const cell = document.createElement("td");
            cell.textContent = value;
            row.append(cell);
        });

        row.addEventListener("click", () => inspectAllocation(block.id));
        row.addEventListener("keydown", event => {
            if (event.key === "Enter" || event.key === " ") {
                event.preventDefault();
                inspectAllocation(block.id);
            }
        });

        ui.allocationRows.append(row);
    });
}

function updateAllocationSelectors() {
    const allocations = memoryBlocks.filter(block => !block.free);
    const previousFreeId = Number(ui.freeId.value);
    const previousReallocId = Number(ui.reallocId.value);

    ui.freeId.replaceChildren();
    ui.reallocId.replaceChildren();

    if (allocations.length === 0) {
        ui.freeId.append(new Option("No allocations", ""));
        ui.reallocId.append(new Option("No allocations", ""));
        return;
    }

    allocations.forEach(block => {
        ui.freeId.append(new Option(`ID${block.id}`, block.id));
        ui.reallocId.append(new Option(`ID${block.id} (${block.size} B)`, block.id));
    });

    const preferredId = allocations.some(block => block.id === selectedId)
        ? selectedId
        : allocations[0].id;

    ui.freeId.value = allocations.some(block => block.id === previousFreeId)
        ? previousFreeId
        : preferredId;

    ui.reallocId.value = allocations.some(block => block.id === previousReallocId)
        ? previousReallocId
        : preferredId;
}

function addHistory(operation, description) {
    const time = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
    historyEntries.unshift({ time, operation, description });

    if (historyEntries.length > 8)
        historyEntries.pop();

    drawHistory();
}

function drawHistory() {
    ui.history.replaceChildren();

    if (historyEntries.length === 0) {
        const empty = document.createElement("li");
        empty.className = "history-empty";
        empty.textContent = "Operations performed in this session will appear here.";
        ui.history.append(empty);
        return;
    }

    historyEntries.forEach(entry => {
        const item = document.createElement("li");
        item.className = `history-entry history-${entry.operation}`;

        const badge = document.createElement("strong");
        badge.textContent = entry.operation;
        const detail = document.createElement("span");
        detail.textContent = entry.description;
        const time = document.createElement("time");
        time.textContent = entry.time;

        item.append(badge, detail, time);
        ui.history.append(item);
    });
}

async function inspectAllocation(id) {
    if (busy)
        return;

    try {
        const details = await sendRequest("/inspect", { id });
        selectedId = id;
        drawMemoryMap();
        drawAllocationTable();
        updateAllocationSelectors();

        $("inspection-title").textContent = `ID${details.id} — ${details.type}`;
        $("inspection-summary").textContent =
            `${details.size.toLocaleString("en-IN")} bytes · addresses ${details.start.toLocaleString("en-IN")} to ${(details.start + details.size - 1).toLocaleString("en-IN")}`;

        if (details.type === "calloc") {
            $("inspection-description").textContent =
                "calloc initializes allocated bytes to zero. Bytes added later by realloc are uninitialized.";
        }
        else {
            $("inspection-description").textContent =
                "malloc does not initialize memory. A ?? value means the byte is uninitialized in this simulation.";
        }

        ui.bytePreview.replaceChildren();
        details.bytes.forEach((value, index) => {
            const cell = document.createElement("span");
            cell.className = `byte-cell${value === 0 ? " zero" : ""}`;
            cell.textContent = value === null
                ? "??"
                : value.toString(16).padStart(2, "0").toUpperCase();
            cell.title = value === null
                ? `Address ${details.start + index}: uninitialized`
                : `Address ${details.start + index}: ${value} (${cell.textContent})`;
            ui.bytePreview.append(cell);
        });

        $("inspection-note").textContent = `Showing the first ${details.bytes.length} bytes of this allocation.`;

        if (!ui.inspectionDialog.open)
            ui.inspectionDialog.showModal();
    }
    catch (error) {
        showStatus(error.message, true);
    }
}

async function performAllocation(event) {
    event.preventDefault();

    if (busy)
        return;

    try {
        let result;

        if (currentMode === "malloc") {
            const size = readPositiveNumber(ui.allocationSize.value, "Allocation size", totalMemory);
            setBusy(true);
            result = await sendRequest("/allocate", {
                size,
                strategy: ui.strategy.value,
                type: "malloc"
            });

            selectedId = result.id;
            await loadMemory();
            addHistory("malloc", `ID${result.id} · ${size.toLocaleString("en-IN")} bytes`);
            showStatus(`malloc allocated ${size} bytes. Allocation ID: ${result.id}.`);
        }
        else if (currentMode === "calloc") {
            const count = readPositiveNumber(ui.elementCount.value, "Element count");
            const elementSize = readPositiveNumber(ui.elementSize.value, "Element size");
            const totalSize = count * elementSize;

            if (!Number.isSafeInteger(totalSize) || totalSize > totalMemory)
                throw new Error("The calloc request is larger than the memory pool.");

            setBusy(true);
            result = await sendRequest("/allocate", {
                size: totalSize,
                strategy: ui.strategy.value,
                type: "calloc"
            });

            selectedId = result.id;
            await loadMemory();
            addHistory("calloc", `ID${result.id} · ${count} × ${elementSize} = ${totalSize} bytes`);
            showStatus(`calloc allocated and zero-initialized ${totalSize} bytes. Allocation ID: ${result.id}.`);
        }
        else {
            const id = readPositiveNumber(ui.reallocId.value, "Allocation ID", 2147483647);
            const newSize = readPositiveNumber(ui.reallocSize.value, "New allocation size", totalMemory);
            const oldBlock = memoryBlocks.find(block => !block.free && block.id === id);

            if (!oldBlock)
                throw new Error("Select an allocation that still exists.");

            const oldSize = oldBlock.size;
            setBusy(true);
            result = await sendRequest("/realloc", { id, size: newSize });

            selectedId = result.id;
            await loadMemory();
            addHistory("realloc", `ID${id} → ID${result.id} · ${oldSize} → ${newSize} bytes`);
            showStatus(`realloc changed ${oldSize} bytes to ${newSize} bytes. Current allocation ID: ${result.id}.`);
        }
    }
    catch (error) {
        showStatus(error.message, true);
    }
    finally {
        setBusy(false);
        updateControls();
    }
}

async function freeAllocation(event) {
    event.preventDefault();

    if (busy)
        return;

    try {
        const id = readPositiveNumber(ui.freeId.value, "Allocation ID", 2147483647);
        setBusy(true);
        await sendRequest("/free", { id });

        selectedId = 0;
        if (ui.inspectionDialog.open)
            ui.inspectionDialog.close();

        await loadMemory();
        addHistory("free", `Released ID${id} and merged adjacent free blocks`);
        showStatus(`Allocation ID${id} was freed. Adjacent free blocks were merged where possible.`);
    }
    catch (error) {
        showStatus(error.message, true);
    }
    finally {
        setBusy(false);
        updateControls();
    }
}

async function resetMemoryPool() {
    if (busy)
        return;

    try {
        const newSize = readPositiveNumber(ui.poolSize.value, "Pool size");
        if (newSize === totalMemory) {
            updateSlider(totalMemory);
            return;
        }

        setBusy(true);
        await sendRequest("/reset", { size: newSize });

        selectedId = 0;
        if (ui.inspectionDialog.open)
            ui.inspectionDialog.close();

        await loadMemory();
        addHistory("reset", `New memory pool: ${newSize.toLocaleString("en-IN")} bytes; previous allocations cleared`);
        showStatus(`Simulation reset. The new memory pool is ${newSize.toLocaleString("en-IN")} bytes.`);
    }
    catch (error) {
        showStatus(error.message, true);
        ui.poolSize.value = totalMemory;
        updateSlider(totalMemory);
    }
    finally {
        setBusy(false);
        updateControls();
    }
}

ui.poolSize.addEventListener("input", () => {
    if (ui.poolSize.value !== "") {
        const value = Math.min(102400, Math.max(1, Number(ui.poolSize.value)));
        updateSlider(value);
    }
});
ui.poolSize.addEventListener("change", resetMemoryPool);
ui.poolSize.addEventListener("keydown", event => {
    if (event.key === "Enter") {
        event.preventDefault();
        ui.poolSize.blur();
    }
});

ui.poolSlider.addEventListener("input", () => {
    ui.poolSize.value = ui.poolSlider.value;
    updateSlider(ui.poolSlider.value);
});
ui.poolSlider.addEventListener("change", resetMemoryPool);

ui.malloc.addEventListener("click", () => setMode("malloc"));
ui.calloc.addEventListener("click", () => setMode("calloc"));
ui.realloc.addEventListener("click", () => setMode("realloc"));
ui.allocationForm.addEventListener("submit", performAllocation);
ui.freeForm.addEventListener("submit", freeAllocation);
ui.clearHistory.addEventListener("click", () => {
    historyEntries.length = 0;
    drawHistory();
});

$("close-inspection").addEventListener("click", () => ui.inspectionDialog.close());
ui.inspectionDialog.addEventListener("click", event => {
    if (event.target === ui.inspectionDialog) {
        const bounds = ui.inspectionDialog.getBoundingClientRect();
        const outside = event.clientX < bounds.left || event.clientX > bounds.right ||
            event.clientY < bounds.top || event.clientY > bounds.bottom;
        if (outside)
            ui.inspectionDialog.close();
    }
});

window.addEventListener("resize", () => {
    if (memoryBlocks.length > 0)
        drawMemoryMap();
});

async function startSimulator() {
    setMode("malloc");
    drawHistory();

    try {
        await loadMemory();
        showStatus("Connected to the C++ backend. Memory is ready.");
    }
    catch (error) {
        showStatus(error.message, true);
        ui.allocationRows.innerHTML = '<tr class="empty-row"><td colspan="5">Start the C++ backend to load memory.</td></tr>';
    }

    updateControls();
}

startSimulator();


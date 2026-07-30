#include "rg_system.h"
#include "rg_psram_exec_test.h"

#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32P4) && defined(CONFIG_SPIRAM)

#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_cache.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MARKER_FILE RG_BASE_PATH "/psram_exec_test"
#define RESULT_FILE RG_BASE_PATH "/psram_exec_test.log"

// addi a0, a0, 1  -- imm=1 rs1=a0 rd=a0. A block of these plus a return is a function whose
// answer proves it actually executed: call it with 0 and it gives back the instruction count.
#define RV32_ADDI_A0_A0_1 0x00150513
// jalr x0, 0(ra)
#define RV32_RET 0x00008067

#define ADDI_PER_BLOCK 256
#define BLOCK_BYTES ((ADDI_PER_BLOCK + 1) * 4)

// Enough blocks to overrun the 128KB L2 several times over, so the streaming pass measures
// fetching code that is not already cached -- which is the case a dynarec actually lives in.
#define STREAM_BLOCKS 1024

// The cache maintenance calls below want cache-line-sized everything, and the line is 64B.
#define ALIGN_TO 64
#define ALIGN_UP(n) (((n) + (ALIGN_TO - 1)) & ~(size_t)(ALIGN_TO - 1))

typedef int (*probe_fn_t)(int);

static FILE *result_fp;

static void note(const char *fmt, ...)
{
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    RG_LOGW("%s", buffer);

    if (result_fp)
    {
        fprintf(result_fp, "%s\n", buffer);
        // Flushed line by line on purpose: the interesting run is the one that dies partway
        // through, and what it managed to write is the whole result.
        fflush(result_fp);
        fsync(fileno(result_fp));
    }
}

static void emit_block(uint32_t *dest)
{
    for (int i = 0; i < ADDI_PER_BLOCK; ++i)
        dest[i] = RV32_ADDI_A0_A0_1;
    dest[ADDI_PER_BLOCK] = RV32_RET;
}

// Make what we just wrote as data visible to instruction fetch. On this chip that is two
// separate problems: the stores are sitting dirty in the data cache, and the instruction path
// may hold stale lines for the same addresses.
static void publish_code(void *addr, size_t size)
{
    esp_err_t err = esp_cache_msync(addr, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    if (err != ESP_OK)
        note("  esp_cache_msync(data writeback) returned 0x%x", err);

    err = esp_cache_msync(addr, size, ESP_CACHE_MSYNC_FLAG_INVALIDATE | ESP_CACHE_MSYNC_FLAG_TYPE_INST);
    if (err != ESP_OK)
        note("  esp_cache_msync(instruction invalidate) returned 0x%x", err);

    asm volatile("fence.i" ::: "memory");
}

static void describe(const char *what, void *ptr)
{
    note("%s: %p (external:%d executable:%d)", what, ptr, esp_ptr_external_ram(ptr) ? 1 : 0,
         esp_ptr_executable(ptr) ? 1 : 0);
}

// Calls the block `repeats` times and reports throughput. Returns false if the block gave the
// wrong answer, which would mean it ran but not as written -- worth knowing before trusting
// any timing.
static bool time_block(const char *label, void *code, int repeats)
{
    probe_fn_t fn = (probe_fn_t)code;
    int64_t started = rg_system_timer();
    int accumulator = 0;

    for (int i = 0; i < repeats; ++i)
        accumulator = fn(accumulator);

    int64_t elapsed = rg_system_timer() - started;
    int64_t instructions = (int64_t)repeats * ADDI_PER_BLOCK;

    if (accumulator != (int)instructions)
    {
        note("%s: WRONG RESULT %d, expected %lld -- it ran, but not what we wrote", label, accumulator,
             (long long)instructions);
        return false;
    }

    note("%s: %lld instructions in %lld us = %d MIPS", label, (long long)instructions, (long long)elapsed,
         elapsed > 0 ? (int)(instructions / elapsed) : -1);
    return true;
}

void rg_psram_exec_test(void)
{
    if (!rg_storage_exists(MARKER_FILE))
        return;

    // Before anything that can fault. See the header: the answer we are looking for is a
    // panic, and a panic that repeats every boot is not a result, it is a brick.
    rg_storage_delete(MARKER_FILE);
    rg_storage_mkdir(RG_BASE_PATH);
    result_fp = fopen(RESULT_FILE, "w");

    note("=== PSRAM instruction fetch probe ===");
    note("build: %s", RG_BUILD_INFO);

    // 1. Does the heap even offer executable PSRAM? If it does not, ask for plain PSRAM and
    //    try anyway -- the heap's opinion and the hardware's are two different questions, and
    //    the second one is the one that matters.
    size_t warm_size = ALIGN_UP(BLOCK_BYTES);
    void *psram_code = heap_caps_aligned_alloc(ALIGN_TO, warm_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_EXEC);
    bool heap_offered_exec = psram_code != NULL;

    note("heap_caps_aligned_alloc(SPIRAM|EXEC): %s", heap_offered_exec ? "ok" : "NULL");

    if (!psram_code)
    {
        psram_code = heap_caps_aligned_alloc(ALIGN_TO, warm_size, MALLOC_CAP_SPIRAM);
        note("falling back to plain SPIRAM: %s", psram_code ? "ok" : "NULL");
    }

    if (!psram_code)
    {
        note("VERDICT: cannot allocate PSRAM at all. Probe inconclusive.");
        goto cleanup;
    }

    describe("psram block", psram_code);

    // 2. Write the block and make it fetchable.
    emit_block(psram_code);
    publish_code(psram_code, warm_size);

    // 3. The line before the jump. If the log ends here, the answer is no: the CPU faulted
    //    trying to fetch from PSRAM, and /sd/crash.log has the details.
    note("about to call into PSRAM at %p ...", psram_code);
    note("(if this is the last line in this file, instruction fetch from PSRAM FAULTS)");

    if (!time_block("psram warm (cache resident)", psram_code, 2000))
        goto cleanup;

    note("VERDICT: instruction fetch from PSRAM WORKS%s", heap_offered_exec ? "" : " (without MALLOC_CAP_EXEC)");

    // 4. Internal RAM, same block, for the ratio that actually decides the design.
    void *iram_code = heap_caps_aligned_alloc(ALIGN_TO, warm_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_EXEC);
    if (iram_code)
    {
        describe("internal block", iram_code);
        emit_block(iram_code);
        publish_code(iram_code, warm_size);
        time_block("internal warm", iram_code, 2000);
        heap_caps_free(iram_code);
    }
    else
    {
        note("no internal executable RAM available for comparison");
    }

    // 5. The number that matters most. A warm 1KB block lives in L2 and says nothing about
    //    the dynarec case, which is code touched once and evicted. So: more blocks than the
    //    cache can hold, each called once.
    size_t stream_size = ALIGN_UP((size_t)STREAM_BLOCKS * BLOCK_BYTES);
    void *stream = heap_caps_aligned_alloc(ALIGN_TO, stream_size,
                                           MALLOC_CAP_SPIRAM | (heap_offered_exec ? MALLOC_CAP_EXEC : 0));
    if (stream)
    {
        for (int i = 0; i < STREAM_BLOCKS; ++i)
            emit_block((uint32_t *)((uint8_t *)stream + (size_t)i * BLOCK_BYTES));
        publish_code(stream, stream_size);

        int64_t started = rg_system_timer();
        int accumulator = 0;
        for (int i = 0; i < STREAM_BLOCKS; ++i)
        {
            probe_fn_t fn = (probe_fn_t)((uint8_t *)stream + (size_t)i * BLOCK_BYTES);
            accumulator = fn(accumulator);
        }
        int64_t elapsed = rg_system_timer() - started;
        int64_t instructions = (int64_t)STREAM_BLOCKS * ADDI_PER_BLOCK;

        if (accumulator != (int)instructions)
            note("psram streaming: WRONG RESULT %d, expected %lld", accumulator, (long long)instructions);
        else
            note("psram streaming (%d KB, each block once): %lld instructions in %lld us = %d MIPS",
                 (int)(stream_size / 1024), (long long)instructions, (long long)elapsed,
                 elapsed > 0 ? (int)(instructions / elapsed) : -1);

        heap_caps_free(stream);
    }
    else
    {
        note("could not allocate %d KB for the streaming pass", (int)(stream_size / 1024));
    }

cleanup:
    if (psram_code)
        heap_caps_free(psram_code);
    note("=== probe done ===");
    if (result_fp)
    {
        fclose(result_fp);
        result_fp = NULL;
    }
}

#else

void rg_psram_exec_test(void)
{
    // Nothing to ask on a target without PSRAM, or one that is not an ESP32-P4.
}

#endif

// Inline hook engine for arm64 (libroomfix).
// Patches 16 bytes at the target: "ldr x17, #8; br x17; .quad replacement".
// The trampoline re-executes the 4 stolen instructions (PC-relative ones are relocated)
// and jumps back to target+16. Every patch first compares the stolen bytes with the
// preimage taken from the immutable original libil2cpp.so.
#include "hook.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define TRAMP_POOL (64 * 1024)
#define TRAMP_SLOT 160

static uint8_t* g_pool;
static size_t g_pool_used;

static void seterr(char* err, size_t n, const char* fmt, ...) {
    if (!err || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

static uint32_t* pool_alloc(void) {
    if (!g_pool) {
        void* p = mmap(NULL, TRAMP_POOL, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return NULL;
        g_pool = (uint8_t*)p;
    }
    if (g_pool_used + TRAMP_SLOT > TRAMP_POOL) return NULL;
    uint32_t* s = (uint32_t*)(g_pool + g_pool_used);
    g_pool_used += TRAMP_SLOT;
    return s;
}

static int64_t sext(uint64_t v, int bits) {
    uint64_t m = 1ULL << (bits - 1);
    return (int64_t)((v ^ m) - m);
}

static void put64(uint32_t* w, uint64_t v) {
    w[0] = (uint32_t)v;
    w[1] = (uint32_t)(v >> 32);
}

// Emits a relocated copy of one instruction at out[*n]. Returns 0 or -1 (unsupported).
static int relocate(uint32_t insn, uint64_t pc, uint32_t* out, int* n) {
    uint32_t* w = out + *n;
    if ((insn & 0x9F000000u) == 0x90000000u) {  // adrp
        uint32_t rd = insn & 31;
        int64_t imm = sext(((insn >> 29) & 3) | (((insn >> 5) & 0x7FFFF) << 2), 21) << 12;
        uint64_t v = (pc & ~0xFFFULL) + imm;
        w[0] = 0x58000040u | rd;  // ldr xd, #8
        w[1] = 0x14000003u;       // b #12
        put64(w + 2, v);
        *n += 4;
        return 0;
    }
    if ((insn & 0x9F000000u) == 0x10000000u) {  // adr
        uint32_t rd = insn & 31;
        int64_t imm = sext(((insn >> 29) & 3) | (((insn >> 5) & 0x7FFFF) << 2), 21);
        uint64_t v = pc + imm;
        w[0] = 0x58000040u | rd;
        w[1] = 0x14000003u;
        put64(w + 2, v);
        *n += 4;
        return 0;
    }
    if ((insn & 0xFC000000u) == 0x14000000u) {  // b
        uint64_t t = pc + (sext(insn & 0x3FFFFFF, 26) << 2);
        w[0] = 0x58000051u;  // ldr x17, #8
        w[1] = 0xD61F0220u;  // br x17
        put64(w + 2, t);
        *n += 4;
        return 0;
    }
    if ((insn & 0xFC000000u) == 0x94000000u) {  // bl
        uint64_t t = pc + (sext(insn & 0x3FFFFFF, 26) << 2);
        w[0] = 0x58000071u;  // ldr x17, #12
        w[1] = 0xD63F0220u;  // blr x17
        w[2] = 0x14000003u;  // b #12
        put64(w + 3, t);
        *n += 5;
        return 0;
    }
    uint64_t t = 0;
    uint32_t head = 0;
    if ((insn & 0xFF000010u) == 0x54000000u) {  // b.cond
        t = pc + (sext((insn >> 5) & 0x7FFFF, 19) << 2);
        head = (insn & 0xFF00001Fu) | (2u << 5);
    } else if ((insn & 0x7E000000u) == 0x34000000u) {  // cbz / cbnz
        t = pc + (sext((insn >> 5) & 0x7FFFF, 19) << 2);
        head = (insn & 0xFF00001Fu) | (2u << 5);
    } else if ((insn & 0x7E000000u) == 0x36000000u) {  // tbz / tbnz
        t = pc + (sext((insn >> 5) & 0x3FFF, 14) << 2);
        head = (insn & 0xFFF8001Fu) | (2u << 5);
    }
    if (head) {
        w[0] = head;         // cond -> +8
        w[1] = 0x14000005u;  // b +20 (skip jump block)
        w[2] = 0x58000051u;  // ldr x17, #8
        w[3] = 0xD61F0220u;  // br x17
        put64(w + 4, t);
        *n += 6;
        return 0;
    }
    if ((insn & 0xBF000000u) == 0x18000000u) {  // ldr wt/xt, literal
        uint32_t rt = insn & 31;
        int is64 = (insn >> 30) & 1;
        uint64_t a = pc + (sext((insn >> 5) & 0x7FFFF, 19) << 2);
        w[0] = 0x58000051u;  // ldr x17, #8
        w[1] = 0x14000003u;  // b #12
        put64(w + 2, a);
        w[4] = (is64 ? 0xF9400000u : 0xB9400000u) | (17u << 5) | rt;  // ldr xt/wt, [x17]
        *n += 5;
        return 0;
    }
    // other literal loads (ldrsw/prfm/simd) are not expected in prologues
    if ((insn & 0x3B000000u) == 0x18000000u) return -1;
    if (insn == 0xD65F03C0u) return -1;  // ret inside the stolen window: function too short
    w[0] = insn;
    *n += 1;
    return 0;
}

// page-granular protection change; the runtime page size (4 KB or 16 KB), never a compile-time constant
static int write_code(void* dst, const void* src, size_t len) {
    uintptr_t pg = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t a = (uintptr_t)dst & ~(pg - 1);
    uintptr_t e = ((uintptr_t)dst + len + pg - 1) & ~(pg - 1);
    if (mprotect((void*)a, e - a, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
    memcpy(dst, src, len);
    __builtin___clear_cache((char*)dst, (char*)dst + len);
    mprotect((void*)a, e - a, PROT_READ | PROT_EXEC);
    return 0;
}

int rf_patch_insn(void* target, uint32_t expect, uint32_t repl) {
    if (!target || *(const uint32_t*)target != expect) return -1;
    return write_code(target, &repl, sizeof repl);
}

void* rf_hook_install(void* target, void* repl, const uint8_t* expect, int want_orig, char* err, size_t errlen) {
    if (!target || !repl) {
        seterr(err, errlen, "null target/replacement");
        return NULL;
    }
    if (expect && memcmp(target, expect, 16) != 0) {
        const uint8_t* b = (const uint8_t*)target;
        seterr(err, errlen, "preimage mismatch: %02x%02x%02x%02x %02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
        return NULL;
    }
    void* orig = (void*)1;  // non-NULL success marker when no trampoline is requested
    if (want_orig) {
        uint32_t* tr = pool_alloc();
        if (!tr) {
            seterr(err, errlen, "trampoline pool exhausted/mmap failed");
            return NULL;
        }
        int n = 0;
        const uint32_t* src = (const uint32_t*)target;
        for (int i = 0; i < 4; i++) {
            if (relocate(src[i], (uint64_t)(uintptr_t)(src + i), tr, &n) != 0) {
                seterr(err, errlen, "cannot relocate insn %d (%08x)", i, src[i]);
                return NULL;
            }
        }
        tr[n++] = 0x58000051u;  // ldr x17, #8
        tr[n++] = 0xD61F0220u;  // br x17
        put64(tr + n, (uint64_t)(uintptr_t)target + 16);
        n += 2;
        __builtin___clear_cache((char*)tr, (char*)(tr + n));
        orig = tr;
    }
    uint32_t patch[4] = {0x58000051u, 0xD61F0220u, 0, 0};
    put64(patch + 2, (uint64_t)(uintptr_t)repl);
    if (write_code(target, patch, sizeof patch) != 0) {
        seterr(err, errlen, "mprotect failed");
        return NULL;
    }
    return orig;
}

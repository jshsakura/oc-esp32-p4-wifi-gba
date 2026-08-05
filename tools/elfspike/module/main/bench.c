/*
 * A stand-in for an emulator's interpreter loop, compiled twice: once into the
 * host firmware (so it executes from flash through the instruction cache) and
 * once as a -fPIC shared object that elf_loader relocates into PSRAM. The two
 * timings are the whole point -- they price instruction fetch from PSRAM, which
 * is the one unknown left in the "load cores from SD at launch" design.
 *
 * Shaped to stress the thing an emulator stresses and nothing else:
 *
 *   - a switch over an opcode stream, so the branch is data-dependent and the
 *     I-side sees a scattered jump table rather than a hot straight line
 *   - reads and writes into a small guest memory array, so the D-side is busy
 *     but stays in cache and cannot be confused for the effect being measured
 *   - no libc, no framework calls: freestanding, so the module needs no symbol
 *     binding and the comparison is pure code
 *
 * Deterministic: same opcode stream, same iteration count, same answer both
 * ways. The checksum is returned so neither copy can be optimised away, and so
 * a mismatch between the two is immediately visible.
 */
#include <stdint.h>

#define GUEST_MEM   4096          /* stays inside L1/L2 either way */
#define PROG_LEN    1024
#define ITERATIONS  2000

/* The "guest" state. Kept inside the module so the shared object needs no
 * relocations against the host and no imported symbols. */
uint32_t elfspike_bench(uint32_t seed)
{
    static uint8_t mem[GUEST_MEM];
    static uint8_t prog[PROG_LEN];

    uint32_t a = seed, x = 0, y = 0, pc = 0, sum = 0;

    /* Build the opcode stream from the seed so it is identical in both copies
     * but not a constant the compiler can fold. */
    for (uint32_t i = 0; i < PROG_LEN; i++)
    {
        seed = seed * 1103515245u + 12345u;
        prog[i] = (uint8_t)(seed >> 16);
    }
    for (uint32_t i = 0; i < GUEST_MEM; i++)
        mem[i] = (uint8_t)i;

    for (uint32_t iter = 0; iter < ITERATIONS; iter++)
    {
        pc = 0;
        while (pc < PROG_LEN)
        {
            uint8_t op = prog[pc++];
            switch (op & 0x0f)
            {
            case 0x0: a += mem[(a + x) & (GUEST_MEM - 1)]; break;
            case 0x1: a ^= mem[(a + y) & (GUEST_MEM - 1)]; break;
            case 0x2: mem[(a + x) & (GUEST_MEM - 1)] = (uint8_t)a; break;
            case 0x3: x++; break;
            case 0x4: y++; break;
            case 0x5: a = (a << 1) | (a >> 31); break;
            case 0x6: a -= mem[(x ^ y) & (GUEST_MEM - 1)]; break;
            case 0x7: if ((a & 0x80) == 0) pc++; break;
            case 0x8: x = mem[(a) & (GUEST_MEM - 1)]; break;
            case 0x9: y = mem[(a + 1) & (GUEST_MEM - 1)]; break;
            case 0xa: a &= mem[(y + 3) & (GUEST_MEM - 1)]; break;
            case 0xb: a |= op; break;
            case 0xc: mem[(y + x) & (GUEST_MEM - 1)] ^= (uint8_t)op; break;
            case 0xd: if (a & 1) pc += 2; break;
            case 0xe: sum += a; break;
            default:  a = (a >> 3) ^ (a << 5); break;
            }
        }
        sum += a + x + y;
    }
    return sum;
}

/* Provided by the HOST, not by this module.
 *
 * This is the whole point of the second half of the spike. A module that only
 * runs its own code proves the loader relocates and the CPU executes; it proves
 * nothing about whether a real core can work, because a real core is almost
 * entirely calls back into the framework -- rg_display_submit, rg_audio_submit,
 * rg_alloc. Those have to resolve, at load time, against the running firmware.
 *
 * In the module build this is an undefined symbol that elf_loader must find in
 * the host's registered symbol table. In the host build it is just a call. Both
 * feed the same checksum, so if the binding resolves to the wrong thing the
 * comparison says so instead of the run looking fine. */
extern uint32_t elfspike_host_mix(uint32_t v);

/* elf_loader's esp_elf_request() enters the module at main() -- and DISCARDS the
 * return value (esp_elf.c: it calls elf->entry(argc, argv) and returns 0
 * regardless). So the checksum comes back through argv[0], which the host points
 * at a uint32_t. Without this the guard compares a real checksum against a
 * constant zero and reports a mismatch for a module that ran perfectly. */
int main(int argc, char **argv)
{
    uint32_t r = elfspike_bench(0x12345678u);
    r ^= elfspike_host_mix(r);   /* the call that has to cross back into the host */
    if (argc > 0 && argv && argv[0])
        *(uint32_t *)argv[0] = r;
    return (int)r;
}

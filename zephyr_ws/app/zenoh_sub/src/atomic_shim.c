#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/*
 * Minimal 32-bit __atomic builtins for RV32IM without the RISC-V A extension.
 * This is acceptable for the current single-core VexRiscv/Zephyr setup.
 */

uint32_t __atomic_fetch_add_4(volatile void* ptr, uint32_t val, int memmodel) {
  ARG_UNUSED(memmodel);

  unsigned int key = irq_lock();
  uint32_t old = *(volatile uint32_t*)ptr;
  *(volatile uint32_t*)ptr = old + val;
  irq_unlock(key);

  return old;
}

uint32_t __atomic_fetch_sub_4(volatile void* ptr, uint32_t val, int memmodel) {
  ARG_UNUSED(memmodel);

  unsigned int key = irq_lock();
  uint32_t old = *(volatile uint32_t*)ptr;
  *(volatile uint32_t*)ptr = old - val;
  irq_unlock(key);

  return old;
}

bool __atomic_compare_exchange_4(volatile void* ptr, void* expected,
                                 uint32_t desired, bool weak,
                                 int success_memmodel, int failure_memmodel) {
  ARG_UNUSED(weak);
  ARG_UNUSED(success_memmodel);
  ARG_UNUSED(failure_memmodel);

  unsigned int key = irq_lock();

  uint32_t old = *(volatile uint32_t*)ptr;
  uint32_t exp = *(uint32_t*)expected;

  if (old == exp) {
    *(volatile uint32_t*)ptr = desired;
    irq_unlock(key);
    return true;
  }

  *(uint32_t*)expected = old;
  irq_unlock(key);
  return false;
}
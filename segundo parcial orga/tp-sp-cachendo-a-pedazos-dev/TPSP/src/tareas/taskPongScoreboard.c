#include "task_lib.h"
#include <stdint.h>

#define WIDTH TASK_VIEWPORT_WIDTH
#define HEIGHT TASK_VIEWPORT_HEIGHT

#define SHARED_SCORE_BASE_VADDR (PAGE_ON_DEMAND_BASE_VADDR + 0xF00)
#define CANT_PONGS 3

void task(void) {
  screen pantalla;
  // ¿Una tarea debe terminar en nuestro sistema?

  while (true) {
    // Completar:
    // - Pueden definir funciones auxiliares para imprimir en pantalla
    // - Pueden usar `task_print`, `task_print_dec`, etc.
    uint32_t *shared_memory = (uint32_t *)SHARED_SCORE_BASE_VADDR;
    for (uint8_t i = 0; i < CANT_PONGS; i++) {

      task_print_dec(pantalla, shared_memory[i * 2], 5, WIDTH / 2 - 7,
                     HEIGHT / 2 - 2 + i * 3, C_FG_WHITE | C_BG_BLACK);
      task_print_dec(pantalla, shared_memory[i * 2 + 1], 5, WIDTH / 2 + 6,
                     HEIGHT / 2 - 2 + i * 3, C_FG_WHITE | C_BG_BLACK);
    }

    syscall_draw(pantalla);
  }
}
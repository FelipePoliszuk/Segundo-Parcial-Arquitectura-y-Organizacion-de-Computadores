#include "defines.h"
#include "mmu.c"
#include "sched.c"
#include "task_defines.h"
#include <stdint.h>
#include <sys/types.h>
#include "gdt.h"
#include "tss.h"

extern void buffer_dma(pd_entry_t *pd);
extern void buffer_copy(pd_entry_t *pd, paddr_t phys, vaddr_t virt);
extern uint32_t task_selector_to_CR3(uint16_t selector);
extern paddr_t virt_to_phy(uint32_t cr3, vaddr_t virt);

// Dirección física del buffer original
#define VIDEO_BUFFER_PHY_ADDR 0xF151C000
// Dirección virtual de la tarea para DMA
#define DMA_VIRT_ADDR 0xBABAB000

#define NO_ACCESS 0
#define ACCESS_DMA 1
#define ACCESS_COPY 2


/* ==========================================================
 * Ejercicio 1
 * ==========================================================
 */

// a) Programar la rutina que atenderá la interrupción que el lector de
// cartuchos generará al terminar de llenar el buffer.

// Consejo: programar una función deviceready y llamarla desde esta rutina.

// Entiendo que tengo que agregar en isr.asm (donde estan las definiciones de
// rutinas de atencion de interrupciones) una rutina de atencion para la
// interrupcion IRQ_40 (rutina que nos dice cada vez que el buffer esta lleno y
// listo para su procesamiento)

// El codigo seria asi:
// ;; Rutina de atención de buffer
// ;; --------------------------------------------------------------------------
// ;;
// ...
// extern deviceready
// ...
// global _isr40

//  isr_40:
//   pushad               ; guardo todos los registros generales en la pila
//   pic_finish1          ; aviso al PIC que voy a atender una interrupcion
//   call deviceready     ; llamo a deviceready
//   popad                ; recupero los registros generales, restaurando el contexto previo a la interrupcion 
//   iret                 ; finalizo volviendo del handler a donde estuvo interrumpido
//   

// En idt.c debo agregar esto:

// ...
// IDT_ENTRY0(40);
// IDT_ENTRY3(90)
// ...

// voy a tener que extender la estructura sched_entry y task_state_t en sched.c
/**
 * Estados posibles de una tarea en el scheduler:
 * - `TASK_SLOT_FREE`: No existe esa tarea
 * - `TASK_RUNNABLE`: La tarea se puede ejecutar
 * - `TASK_PAUSED`: La tarea se registró al scheduler pero está pausada
 * - `TASK_BLOCKED`: La tarea esta bloqueada
 * - `TASK_KILLED`: La tarea fue eliminada
 */
//  typedef enum {
//   TASK_SLOT_FREE,
//   TASK_RUNNABLE,
//   TASK_PAUSED
//   TASK_BLOCKED
//   TASK_KILLED
// } task_state_t;

// /**
//  * Estructura usada por el scheduler para guardar la información pertinente
//  de
//  * cada tarea.
//  */
// typedef struct {
//   int16_t selector;
//   task_state_t state;

//   uint8_t mode                  // agrego variable para saber si el acceso es por DMA o por copia 
//   uint32_t copyDir              // agrego variable para saber la direccion de la copia virtual
//   uint32_t dir_copia_fisica     // agrego variable para saber la direccion de la copia fisica

// } sched_entry_t;

// static sched_entry_t sched_tasks[MAX_TASKS] = {0};

// defino la funcion deviceready en (sched.c)

// si se llama a deviceready es porque el buffer se lleno, entonces tengo que:

// Ver qué tareas estaban esperando este evento
// Darles acceso al buffer (con buffer_dma o buffer_copy)
// Despertarlas (ponerlas de nuevo en estado TASK_RUNNABLE).

void deviceready(void) {
  for (int i = 0; i < MAX_TASKS; i++) {
    sched_entry_t *tarea = &sched_tasks[i];
    if (tarea->mode == NO_ACCESS)                          // No solicita acceso al buffer
      continue;
      
    if (tarea->state == TASK_BLOCKED) {
      if (tarea->mode == ACCESS_DMA)                        // Solicita acceso en modo DMA
        buffer_dma((pd_entry_t *)CR3_TO_PAGE_DIR(
            task_selector_to_CR3(tarea->selector)));

      if (tarea->mode == ACCESS_COPY)                         // Solicita acceso en modo por copia
        buffer_copy((pd_entry_t *)CR3_TO_PAGE_DIR(task_selector_to_CR3(tarea->selector)),
                    mmu_next_free_user_page(), tarea->copyDir);
      tarea->state = TASK_RUNNABLE; // Dejamos la tarea lista para correr en una próxima ejecución

    } else {
      if (tarea->mode == ACCESS_COPY) {       // Actualizar copia
        paddr_t destino = virt_to_phy(task_selector_to_CR3(tarea->selector), tarea->copyDir);
        copy_page(destino, (paddr_t)0xF151C000);
      }
    }
  }
}

     

// b) Programar las syscalls opendevice y closedevice.

// Al igual que antes, debo llamar desde isr.asm a estas funciones...

// OPENDEVICE
// Syscall que permite a las tareas solicitar acceso al buffer según el tipo configurado. En el
// caso de acceso por copia, la dirección virtual donde realizar la copia estará dada por el valor
// del registro ECX al momento de llamarla.

// El sistema no debe retornar la ejecución de las tareas que llaman a la syscall hasta que
// se detecte que el buffer está listo y se haya realizado el mapeo DMA o la copia
// correspondiente.


// El codigo seria asi:
// ;; Rutina de atención de opendevice
// ;; --------------------------------------------------------------------------
// ;;

// extern opendevice
// ...
//  global isr_90

//  isr_90:

//  pushad                                ; guardo todos los registros generales en la pila
//  push ecx                              ; guardo ecx
//  call opendevice                       ; llamo a opendevice
//  add esp, 4                            ; limpio el parámetro de la pila
//  call sched_next_task                  ; llamo a sched_next_task
//  mov word [sched_task_selector], ax    ; cargo la tarea en [sched_task_selector]
//  jmp far [sched_task_offset]           ; salto a la nueva tarea
//  popad                                 ; recupero los registros generales, restaurando el contexto previo a la interrupcion iret
//  iret                                  ; finalizo volviendo del handler a donde estuvo interrumpido


void opendevice_handler(vaddr_t copyDir) {
    int8_t task_id = current_task;
    sched_entry_t* tarea = &sched_tasks[task_id];

    // LEER DIRECTAMENTE (Estamos en el contexto de la tarea)
    // El enunciado garantiza que 0xACCE5000 está mapeada.
    tarea->mode = *(uint8_t*)0xACCE5000;
    
    tarea->copyDir = copyDir; 

    // 3. BLOQUEAR
    tarea->state = TASK_BLOCKED; 
}


// ;; Rutina de atención de closedevice
// ;; --------------------------------------------------------------------------
// ;;

// extern closedevice
// ...
//  global isr_91

//  isr_91:

//  pushad
//  call closedevice
//  popad
//  iret

void closedevice(void) {
  int8_t task_id = current_task;
  sched_entry_t* tarea = &sched_tasks[task_id];  
  // En el caso DMA, la dir virtual de la pagina es siempre la misma

  if (tarea->mode == ACCESS_DMA)
    mmu_unmap_page(rcr3(),(vaddr_t)0xBABAB000);
  
  // En el caso por copia, la dir virtual la tenemos en el struct del scheduler
  if (tarea->mode == ACCESS_COPY)
    mmu_unmap_page(rcr3(), tarea->copyDir);

  tarea->mode = NO_ACCESS;
}


// funciones auxiliares:

// funcion para obtener el cr3 de una tarea a partir de su selector
uint32_t task_selector_to_CR3(uint16_t selector) {
  uint16_t index = selector >> 3;            // Sacamos los atributos
  gdt_entry_t *taskDescriptor = &gdt[index]; // Indexamos en la gdt
  tss_t *tss = (tss_t *)((taskDescriptor->base_15_0) |
                         (taskDescriptor->base_23_16 << 16) |
                         (taskDescriptor->base_31_24 << 24));
  return tss->cr3;
}

// funcion para obtener la direccion fisica de una pagina a partir de su
// direccion virtual y su cr3

paddr_t virt_to_phy(uint32_t cr3, vaddr_t virt) {
  pd_entry_t *pd = (pd_entry_t *)CR3_TO_PAGE_DIR(cr3);
  uint32_t pd_index = VIRT_PAGE_DIR(virt);
  uint32_t pt_index = VIRT_PAGE_TABLE(virt);

  // Reconstruyo el puntero a la tabla de páginas del directorio
  pt_entry_t *pt = (pt_entry_t *)((pd[pd_index].pt) << 12);

  // Devuelvo la dirección física de la página correspondiente
  return (paddr_t)(pt[pt_index].page << 12);
}

/* ==========================================================
 * Ejercicio 2
 * ==========================================================
 */

/**
 * a) Programar la función buffer_dma
 * Mapea la página de la tarea en modo DMA (Lectura, Usuario)
 */
void buffer_dma(pd_entry_t *pd) {
  uint32_t cr3 = (uint32_t)pd;

  // Mapeo virtual 0xBABAB000 a físico 0xF151C000
  // Permisos: Presente (P) y Usuario (U).
  // NO se pone Write (W) para que sea solo lectura para la tarea.
  mmu_map_page(cr3, DMA_VIRT_ADDR, VIDEO_BUFFER_PHY_ADDR, MMU_P | MMU_U);
}

/**
 * b) Programar la función buffer_copy
 * Copia el buffer original a una nueva página y la mapea (R/W, Usuario)
 */
void buffer_copy(pd_entry_t *pd, paddr_t phys, vaddr_t virt) {
  uint32_t cr3 = (uint32_t)pd;
  // 1. Copiar el contenido del buffer original (HW) a la
  //    página física 'phys' que nos asignaron para esta tarea.
  //    (Asumimos que copy_page está en mmu.c)
  copy_page(phys, VIDEO_BUFFER_PHY_ADDR);

  // 2. Mapear la dirección virtual 'virt' (que eligió la tarea)
  //    a la página física 'phys' (nuestra copia).
  // Permisos: Presente (P), Usuario (U) y Escritura (W).
  mmu_map_page(cr3, virt, phys, MMU_P | MMU_W | MMU_U);
}
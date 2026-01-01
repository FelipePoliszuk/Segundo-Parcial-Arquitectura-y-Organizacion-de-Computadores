#include "defines.h"
#include "mmu.c"
#include "sched.c"
#include "task_defines.h"
#include <stdint.h>
#include <sys/types.h>
#include "gdt.h"
#include "tss.h"
#include <stdbool.h>
#include "i386.h"


#define VIRT_DIR 0xC001C0DE
// #define 

// extern void swap_handler(uint32_t id_tarea);
// extern


// Primer ejercicio

// a) Programar una syscall swap que permita intercambiar sus registros con la tarea destino de forma bloqueante, esto es:
// • La tarea que llama a swap debe continuar su ejecución solamente cuando los registros ya fueron intercambiados


// Para empezar, en el archivo idt.c debo agregar las entradas para estas syscalls.  (nivel 3)


    // ...
    // IDT_ENTRY3(90); // <-- Syscall para swap
    // IDT_ENTRY3(91); // <-- Syscall para swap_now
    // ...


// Ambas syscalls las voy a implementar en isr.asm, donde donde se definen las rutinas de atencion de interrupciones.


//     extern swap
//     extern swap_now

//     global swap  

//     _isr90:
//         pushad
//         push ebx                             ; Paso id de tarea en ebx
//         call swap
//         add esp, 4

//         // "saltar a la próxima tarea"        
//         call sched_next_task
//         mov word [sched_task_selector], ax
//         jmp far [sched_task_offset]          ; Desalojo la tarea

//         popad                                ;Restaurar los registros originales
//         iret


typedef struct {
  int16_t selector;
  task_state_t state;
  int8_t quiere_swapear_con         // ID con el que quiere swapear (0 si no quiere swapear)
} sched_entry_t;


void swap(int8_t tarea_a_swapear ){
    int8_t task_id = current_task;
    sched_entry_t* tarea = &sched_tasks[task_id];                           // tarea actual
    sched_entry_t* tarea_a_swap = &sched_tasks[tarea_a_swapear];            // tarea a swapear
    uint32_t cr3 = rcr3();     
    uint32_t cr3_swap = task_selector_to_CR3(tarea_a_swap->selector);                                             

    tarea->quiere_swapear_con = tarea_a_swapear;

    if ((tarea->quiere_swapear_con == tarea_a_swapear) && (tarea_a_swap->quiere_swapear_con == task_id)){

        swap_posta(tarea_a_swapear, task_id);
        avisar_intercambio(cr3, 1);
        avisar_intercambio(cr3_swap, 1);

    } else {
        sched_disable_task(tarea);          // deshabilito la tarea en ejecución
        avisar_intercambio(cr3, 0);
    }
    
}

//     global swap_now 

//     _isr90:
//         pushad
//         push ebx                             ; Paso id de tarea en ebx
//         call swap_now
//         add esp, 4

//         // "saltar a la próxima tarea"         ; Porque dice que no se devuelve el control hasta que este listo
//         call sched_next_task
//         mov word [sched_task_selector], ax
//         jmp far [sched_task_offset]          ; Desalojo la tarea

//         popad                                ;Restaurar los registros originales
//         iret

void swap_now(int8_t tarea_a_swapear){
    int8_t task_id = current_task;
    sched_entry_t* tarea = &sched_tasks[task_id];                           // tarea actual
    sched_entry_t* tarea_a_swap = &sched_tasks[tarea_a_swapear];            // tarea a swapear
    uint32_t cr3 = rcr3();
    uint32_t cr3_swap = task_selector_to_CR3(tarea_a_swap->selector);  
    
    if ((tarea->quiere_swapear_con == tarea_a_swapear) && (tarea_a_swap->quiere_swapear_con == task_id)){

        swap_posta(tarea_a_swapear, task_id);
        avisar_intercambio(cr3, 1);
        avisar_intercambio(cr3_swap, 1);
    } else{
        avisar_intercambio(cr3, 0);
    }
    
}


void swap_posta(int8_t tarea1, int8_t tarea2){

    tss_t* tss1 = &tss_tasks[tarea1];       //tss1
    tss_t* tss2 = &tss_tasks[tarea2];       //tss2

    sched_entry_t* tarea1_sched = &sched_tasks[tarea1];                           // tarea actual
    sched_entry_t* tarea2_sched = &sched_tasks[tarea2];                           // tarea a swapear    

    uint32_t temp;

    temp = tss1->eax;
    tss1->eax = tss2->eax;
    tss2->eax = temp;
    
    temp = tss1->ecx;
    tss1->ecx = tss2->ecx;
    tss2->ecx = temp;

    temp = tss1->edx;
    tss1->edx = tss2->edx;
    tss2->edx = temp;

    temp = tss1->ebx;
    tss1->ebx = tss2->ebx;
    tss2->ebx = temp;
    
    temp = tss1->esi;
    tss1->esi = tss2->esi;
    tss2->esi = temp;
    
    temp = tss1->edi;
    tss1->edi = tss2->edi;
    tss2->edi = temp;    

    tarea1_sched->quiere_swapear_con = 0;
    tarea2_sched->quiere_swapear_con = 0;

    sched_enable_task(tarea1);
    sched_enable_task(tarea2);
}


void avisar_intercambio(uint32_t cr3, uint8_t valor){

    garantizar_y_limpiar_pagina(cr3, VIRT_DIR);

    if (rcr3() == cr3){
        *(uint8_t*)(VIRT_DIR) = valor;
    } else {

        paddr_t fisica = virt_to_phy(cr3,VIRT_DIR);

        mmu_map_page(rcr3(),DST_VIRT_PAGE,fisica, MMU_P | MMU_U | MMU_W); 

        uint32_t offset = VIRT_DIR & 0xFFF;                         // Los últimos 12 bits
        uint8_t* puntero_final = (uint8_t*)(DST_VIRT_PAGE + offset);

        *puntero_final = valor;

        mmu_unmap_page(rcr3(), DST_VIRT_PAGE);
    }   

}



// Fuciones auxiliares

// =================================================================
/**
 * Verifica si una dirección virtual está mapeada.
 * Si NO lo está: pide una página física, la limpia (ceros) y la mapea.
 */
void garantizar_y_limpiar_pagina(uint32_t cr3, vaddr_t virt) {
    // Usamos tu función auxiliar
    if (mmu_is_page_mapped(cr3, virt) == false) {
        
        // 1. Pedir página física nueva
        paddr_t nueva_pag = mmu_next_free_user_page();
        
        // 2. Limpiarla (Requisito del enunciado)
        zero_page(nueva_pag); 

        // 3. Mapearla en el CR3 de la tarea correspondiente
        // Permisos: Usuario, Presente y Escritura (RW)
        mmu_map_page(cr3, virt, nueva_pag, MMU_P | MMU_U | MMU_W);
    }
}

/**
 * Verifica si una dirección virtual está actualmente mapeada (presente)
 * en un mapa de memoria (cr3) dado.
 */
 bool mmu_is_page_mapped(uint32_t cr3, vaddr_t virt) {
    
  pd_entry_t *pd = (pd_entry_t *)CR3_TO_PAGE_DIR(cr3);
  uint32_t pd_index = VIRT_PAGE_DIR(virt);

  // 1. ¿Está presente la Tabla de Páginas (Page Table)?
  if ((pd[pd_index].attrs & MMU_P) == 0) { // MMU_P = bit "Presente"
      return false;
  }

  // 2. Obtener la Page Table
  pt_entry_t *pt = (pt_entry_t *)MMU_ENTRY_PADDR(pd[pd_index].pt);
  uint32_t pt_index = VIRT_PAGE_TABLE(virt);

  // 3. ¿Está presente la Página (Page) en sí?
  if ((pt[pt_index].attrs & MMU_P) == 0) {
      return false;
  }

  return true; // ¡La página está mapeada!
}

/**
 * Obtiene el CR3 de una tarea a partir de su selector
 */
uint32_t task_selector_to_CR3(uint16_t selector) {
  uint16_t index = selector >> 3;            // Sacamos los atributos
  gdt_entry_t *taskDescriptor = &gdt[index]; // Indexamos en la gdt
  tss_t *tss = (tss_t *)((taskDescriptor->base_15_0) |
                         (taskDescriptor->base_23_16 << 16) |
                         (taskDescriptor->base_31_24 << 24));
  return tss->cr3;
}

/**
 * Obtiene la dirección física de una página a partir de su dirección virtual y su cr3
 */
paddr_t virt_to_phy(uint32_t cr3, vaddr_t virt) {
  pd_entry_t *pd = (pd_entry_t *)CR3_TO_PAGE_DIR(cr3);
  uint32_t pd_index = VIRT_PAGE_DIR(virt);
  uint32_t pt_index = VIRT_PAGE_TABLE(virt);

  // Reconstruyo el puntero a la tabla de páginas del directorio
  pt_entry_t *pt = (pt_entry_t *)((pd[pd_index].pt) << 12);

  // Devuelvo la dirección física de la página correspondiente
  return (paddr_t)(pt[pt_index].page << 12);
}
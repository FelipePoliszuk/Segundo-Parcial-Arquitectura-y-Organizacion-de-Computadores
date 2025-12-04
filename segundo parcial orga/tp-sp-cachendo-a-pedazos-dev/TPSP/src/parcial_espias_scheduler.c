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


// #define VIRT_DIR 0xC001C0DE
// #define 
// #define 

// extern void swap_handler(uint32_t id_tarea);
// extern
// extern


// Ejercicio 1

// Se desea agregar al sistema una syscall que le permita a la tarea que la llama espiar la memoria 
// de las otras tareas en ejecución. En particular queremos copiar 4 bytes de una dirección de la tarea 
// a espiar en una dirección de la tarea llamadora (tarea espía). La syscall tendrá los siguientes parámetros:

// • El selector de la tarea a espiar.
// • La dirección virtual a leer de la tarea espiada.
// • La dirección virtual a escribir de la tarea espía.

// Si la dirección a espiar no está mapeada en el espacio de direcciones de la tarea correspondiente, la
// syscall deberá devolver 1 en eax, por el contrario, si se pudo hacer correctamente la operación deberá
// devolver 0 en eax.

// ------------------------------------------------------------------------------------------------------------

// Tengo que implementar la syscall espia que le permita a la tarea que la llama espiar la memoria 
// de las otras tareas en ejecución.

// Para empezar, en el archivo idt.c debo agregar las entradas para esta syscall. (nivel 3)


    // ...
    // IDT_ENTRY3(90); // <-- Syscall para espia
    // ...

// La syscall la voy a implementar en isr.asm, donde donde se definen las rutinas de atencion de interrupciones.

//     extern espia

//     global _isr90  

//     _isr90:
//         pushad
//         push ax                              ; Paso selector de tarea por ax
//         push edi                             ; Paso dirección a leer por edi
//         push esi                             ; Paso dirección a escribir por esi
//         call espia_handler
//         add esp, 12
//         
//         mov [ESP+offset_EAX], eax            ; IMPORTANTE no pisar el resultado con el popad

//         popad                                ; Restaurar los registros originales
//         iret



bool espia_handler(uint16_t selector, vaddr_t dir_vir_read, vaddr_t dir_vir_write){

    uint32_t cr3_a_espiar = task_selector_to_CR3(selector);     // cr3_a_espiar
    uint32_t cr3 = rcr3();                                       // cr3
    
    if (mmu_is_page_mapped(cr3_a_espiar,dir_vir_read)){
        
        paddr_t dir_phy_read = virt_to_phy(cr3_a_espiar, dir_vir_read);
        
        
        mmu_map_page(cr3, SRC_VIRT_PAGE, dir_phy_read, MMU_P | MMU_U | MMU_W);
        
        uint32_t* ptr = (uint32_t*)((SRC_VIRT_PAGE & 0xFFFFF000) | (dir_vir_read & 0xFFF));
        uint32_t dato_a_copiar = *ptr; // ¡Aquí se lee la memoria!
        
        
        mmu_unmap_page(cr3, SRC_VIRT_PAGE);
        
        uint32_t* destino = (uint32_t*)dir_vir_write;
        *destino = dato_a_copiar;

        return 0;
    }
    
    return 1;
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
 * mmu_map_page agrega las entradas necesarias a las estructuras de paginación
 * de modo de que la dirección virtual virt se traduzca en la dirección física
 * phy con los atributos definidos en attrs
 * @param cr3 el contenido que se ha de cargar en un registro CR3 al realizar la
 * traducción
 * @param virt la dirección virtual que se ha de traducir en phy
 * @param phy la dirección física que debe ser accedida (dirección de destino)
 * @param attrs los atributos a asignar en la entrada de la tabla de páginas
 */
void mmu_map_page(uint32_t cr3, vaddr_t virt, paddr_t phy, uint32_t attrs) {

  pd_entry_t *pdb = (pd_entry_t *)CR3_TO_PAGE_DIR(cr3);

  uint32_t pdi = VIRT_PAGE_DIR(virt);
  uint32_t pti = VIRT_PAGE_TABLE(virt);

  if ((pdb[pdi].attrs & MMU_P) == 0) {
    paddr_t addr = mmu_next_free_kernel_page();
    zero_page(addr);
    pdb[pdi].pt = (addr >> 12);
  }

  pt_entry_t *ptb = (pt_entry_t *)MMU_ENTRY_PADDR(pdb[pdi].pt);

  pdb[pdi].attrs = pdb[pdi].attrs | attrs | MMU_P;
  ptb[pti].attrs = ptb[pti].attrs | attrs | MMU_P;

  ptb[pti].page = (phy >> 12);

  tlbflush();
}

/**
 * mmu_unmap_page elimina la entrada vinculada a la dirección virt en la tabla
 * de páginas correspondiente
 * @param virt la dirección virtual que se ha de desvincular
 * @return la dirección física de la página desvinculada
 */
paddr_t mmu_unmap_page(uint32_t cr3, vaddr_t virt) {

  pd_entry_t *pdb = (pd_entry_t *)CR3_TO_PAGE_DIR(cr3);

  uint32_t pdi = VIRT_PAGE_DIR(virt);
  uint32_t pti = VIRT_PAGE_TABLE(virt);

  pt_entry_t *ptb = (pt_entry_t *)MMU_ENTRY_PADDR(pdb[pdi].pt);

  paddr_t phy_addr = MMU_ENTRY_PADDR(ptb[pti].page);

  ptb[pti].attrs = 0;

  tlbflush();

  return phy_addr;
}
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


// extern tss_t tss_create_kernel_task(paddr_t code_start);
// extern uint32_t task_selector_to_CR3(uint16_t selector);
// extern paddr_t virt_to_phy(uint32_t cr3, vaddr_t virt);
// extern uint32_t get_cr3_from_task_id(int8_t task_id);
// extern void* traducir_puntero_tarea(vaddr_t virt_tarea, int8_t task_id_tarea);
// extern paddr_t mmu_get_pte(uint32_t cr3, vaddr_t virt);
// extern bool mmu_is_page_mapped(uint32_t cr3, vaddr_t virt);
// extern void garantizar_y_limpiar_pagina(uint32_t cr3, vaddr_t virt);
// extern tss_t* obtener_puntero_tss(uint16_t selector);
// extern void escribir_en_tarea(uint32_t cr3_tarea, vaddr_t virt_destino, uint8_t valor);
// extern void leer_de_tarea(uint32_t cr3_tarea, vaddr_t virt_origen);



/** 
 * Crea una TSS para una tarea de Nivel 0 (Kernel).
 * (Inspirado en tss_create_user_task)
 */
 tss_t tss_create_kernel_task(paddr_t code_start) {
    
  // Las tareas de Kernel usan el Page Directory del Kernel
  uint32_t cr3 = KERNEL_PAGE_DIR;
  
  // Las tareas de Kernel usan una pila del pool de kernel
  vaddr_t stack = mmu_next_free_kernel_page();
  vaddr_t esp = stack + PAGE_SIZE;

  return (tss_t){
      .cr3 = cr3,
      .esp = esp,
      .ebp = esp,
      .eip = (uint32_t)code_start, // EIP apunta a la función C

      // Selectores de Nivel 0 (Kernel)
      .cs = GDT_CODE_0_SEL,
      .ds = GDT_DATA_0_SEL,
      .es = GDT_DATA_0_SEL,
      .fs = GDT_DATA_0_SEL,
      .gs = GDT_DATA_0_SEL,
      .ss = GDT_DATA_0_SEL,
      
      // Pila de Nivel 0 para interrupciones (puede ser la misma)
      .ss0 = GDT_DATA_0_SEL,
      .esp0 = esp,

      .eflags = EFLAGS_IF, // Habilitar interrupciones
  };
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
 * Obtiene el CR3 de una tarea dando su ID (su índice en sched_tasks).
 * Combina la búsqueda del selector y la conversión a CR3.
 */
 uint32_t get_cr3_from_task_id(int8_t task_id) {
  // 1. Obtener el selector desde el array del scheduler
  uint16_t selector = sched_tasks[task_id].selector;
  
  // 2. Usar tu función para obtener el CR3
  return task_selector_to_CR3(selector);
}


/**
 * Traduce una dirección virtual de una TAREA específica a una 
 * dirección física que el KERNEL puede leer.
 * Asume que el kernel tiene identity mapping para toda la RAM. (PELIGROSO)
 */
 void* traducir_puntero_tarea(vaddr_t virt_tarea, int8_t task_id_tarea) {
    
  // 1. Obtener el mapa de memoria (CR3) de la tarea
  uint32_t cr3_tarea = get_cr3_from_task_id(task_id_tarea);

  // 2. Obtener la dirección FÍSICA de la PÁGINA
  paddr_t phy_pagina = virt_to_phy(cr3_tarea, virt_tarea);

  // 3. Calcular el OFFSET dentro de esa página
  uint32_t offset = virt_tarea & 0xFFF; // 0xFFF son los 12 bits del offset

  // 4. Calcular la dirección FÍSICA final del byte
  paddr_t phy_final = phy_pagina + offset;

  // 5. Como el kernel usa identity mapping, la dirección física
  //    es igual a la dirección virtual del kernel.
  return (void*)phy_final;
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
 * Devuelve un puntero a la Page Table Entry (PTE) de una dirección
 * virtual, permitiendo leer o modificar sus bits de atributos
 * (como el bit de Accedido 'A' o Sucio 'D').
 * Devuelve NULL si la Page Table no está presente.
 */
 pt_entry_t* mmu_get_pte(uint32_t cr3, vaddr_t virt) {
    
  pd_entry_t *pd = (pd_entry_t *)CR3_TO_PAGE_DIR(cr3);
  uint32_t pd_index = VIRT_PAGE_DIR(virt);

  // 1. ¿Está presente la Tabla de Páginas (Page Table)?
  if ((pd[pd_index].attrs & MMU_P) == 0) {
      return NULL; // No hay Page Table, no podemos obtener la PTE
  }

  // 2. Obtener la Page Table
  pt_entry_t *pt = (pt_entry_t *)MMU_ENTRY_PADDR(pd[pd_index].pt);
  uint32_t pt_index = VIRT_PAGE_TABLE(virt);

  // 3. Devolver el puntero a la entrada
  return &pt[pt_index];
}

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
 * Obtiene el puntero a la TSS de una tarea a partir de su selector
 */
tss_t* obtener_puntero_tss(uint16_t selector) {
    uint16_t index = selector >> 3;
    gdt_entry_t* desc = &gdt[index];

    // Armamos la dirección base de 32 bits
    uint32_t base = (desc->base_15_0) | 
                    (desc->base_23_16 << 16) | 
                    (desc->base_31_24 << 24);

    return (tss_t*) base; // Devuelve el puntero
}


// Definimos una dirección virtual segura para uso temporal del kernel
// (Debe ser una que sepamos que no se usa para otra cosa, mmu.c usa 0xA00000)
#define TEMP_KERNEL_PAGE_VIRT 0xA00000 

void escribir_en_tarea(uint32_t cr3_tarea, vaddr_t virt_destino, uint8_t valor) {
    
    // 1. Obtener la dirección FÍSICA de la página de la tarea
    // (Usamos tu función que ya verifica si existe)
    garantizar_y_limpiar_pagina(cr3_tarea, virt_destino); 
    paddr_t phy_page_base = virt_to_phy(cr3_tarea, virt_destino);

    // 2. Calcular el offset (desplazamiento) dentro de la página
    // (virt_to_phy te da la base 0x...000, necesitamos los últimos 12 bits)
    uint32_t offset = virt_destino & 0xFFF;

    // 3. MAPEO TEMPORAL
    // Mapeamos esa física ajena a una virtual nuestra (del kernel/actual)
    // Usamos rcr3() porque queremos que sea visible AHORA.
    mmu_map_page(rcr3(), TEMP_KERNEL_PAGE_VIRT, phy_page_base, MMU_P | MMU_W); 
    // Nota: No hace falta MMU_U porque estamos en kernel (Ring 0)

    // 4. Escribir el valor
    // Apuntamos a la base temporal + el offset original
    uint8_t* ptr = (uint8_t*)(TEMP_KERNEL_PAGE_VIRT + offset);
    *ptr = valor;

    // 5. Limpieza
    // Desmapeamos la página temporal para no dejar basura
    mmu_unmap_page(rcr3(), TEMP_KERNEL_PAGE_VIRT);
}

/**
 * Lee un byte de la memoria de otra tarea de forma segura,
 * mapeando temporalmente la página en el espacio del kernel.
 */
uint8_t leer_de_tarea(uint32_t cr3_tarea, vaddr_t virt_origen) {
    
    // 1. Obtener dirección física (asumiendo que la página existe)
    paddr_t phy_page_base = virt_to_phy(cr3_tarea, virt_origen);
    if (phy_page_base == 0) return 0; // Manejo de error simple

    // 2. Mapeo temporal en la ventana del kernel
    // Usamos MMU_P (Presente) y MMU_W (Escritura - opcional para leer, pero estándar)
    mmu_map_page(rcr3(), TEMP_KERNEL_PAGE_VIRT, phy_page_base, MMU_P | MMU_W);

    // 3. Calcular puntero con offset
    uint32_t offset = virt_origen & 0xFFF;
    uint8_t* ptr = (uint8_t*)(TEMP_KERNEL_PAGE_VIRT + offset);

    // 4. Leer valor
    uint8_t valor = *ptr;

    // 5. Limpiar
    mmu_unmap_page(rcr3(), TEMP_KERNEL_PAGE_VIRT);
    tlbflush(); 

    return valor;
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
 * Obtiene la dirección física base de una página a partir de su dirección virtual.
 * Inspirado en las funciones de map_page y unmap_page.
 * Devuelve 0 si la página no está mapeada.
 * NOTA: Devuelve solo la BASE de la página (sin el offset)
 */
uint32_t obtenerDireccionFisica(uint32_t cr3_tarea_a_espiar, uint32_t* direccion_a_espiar) {
    pd_entry_t* pd = (pd_entry_t*)CR3_TO_PAGE_DIR(cr3_tarea_a_espiar);
    int pdi = VIRT_PAGE_DIR(direccion_a_espiar);
    
    // Si no está mapeada devuelvo 0
    if (!(pd[pdi].attrs & MMU_P)) {
        return 0;
    }
    
    pt_entry_t* pt = (pt_entry_t*)MMU_ENTRY_PADDR(pd[pdi].pt);
    int pti = VIRT_PAGE_TABLE(direccion_a_espiar);
    
    // Idem al anterior
    if (!(pt[pti].attrs & MMU_P)) {
        return 0;
    }
    
    // Hasta acá es CASI IGUAL a mmu_unmap_page (solo cambié nombres de variables)
    // Ahora en vez de poner el present en 0, solo devuelvo la dirección física
    paddr_t direccion_fisica = MMU_ENTRY_PADDR(pt[pti].page);
    return direccion_fisica;
}



// 0x400000 = 4mb en hexadecimal

//CHEQUEAR 

bool page_fault_handler(vaddr_t virt) {

    // --- CASO A: BAJO DEMANDA (TP3 / Shared) ---
    // "¿Es la dirección de memoria compartida?"
    if (virt >= ON_DEMAND_MEM_START_VIRTUAL && 
        virt <= ON_DEMAND_MEM_END_VIRTUAL) {
        
        // ESTRATEGIA: Mapear a una FÍSICA FIJA/CONOCIDA
        // Todos los procesos ven la misma física (0x03000000)
        mmu_map_page(rcr3(), virt, ON_DEMAND_MEM_START_PHYSICAL, MMU_P | MMU_U | MMU_W);
        return true;
    }

    // --- CASO B: LAZY ALLOCATION (Malloco) ---
    // "¿Es una dirección que la tarea reservó con malloc?"
    if (esMemoriaReservada(virt)) { // (Tu función auxiliar)
        
        // ESTRATEGIA: Mapear a una FÍSICA NUEVA
        // Cada página es única para esta tarea.
        paddr_t nueva_fisica = mmu_next_free_user_page();
        zero_page(nueva_fisica); // ¡Importante limpiar en Lazy!
        
        mmu_map_page(rcr3(), virt, nueva_fisica, MMU_P | MMU_U | MMU_W);
        return true;
    }

    return false; // Segfault real
}



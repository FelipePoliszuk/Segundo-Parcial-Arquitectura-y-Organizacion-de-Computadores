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


#define VIR_DIR_A 0x0AAAA000
#define VIR_DIR_B 0x0BBBB000


// ## Ejercicio 1 (50 puntos):

// Funciones que puedo utilizar:

task_id_t hay_tarea_disponible_para_recurso(recurso_t recurso);
task_id_t para_quien_produce(task_id_t id_tarea);
void restaurar_tarea(task_id_t id_tarea);
task_id_t hay_consumidora_esperando(recurso_t recurso);


// Tengo que implementar las syscalls solicitar y recurso_listo, ambos tienen estados bloqueantes, es decir, 
// cuando se llaman, se bloquean las tareas.

// solicitar: syscall que permite a la tarea llamadora solicitar un recurso 
// recurso_listo: syscall que permite a la tarea llamadora avisar que su recurso está listo.  

// Para empezar, en el archivo idt.c debo agregar las entradas para estas syscalls.  (nivel 3)


    // ...
    // IDT_ENTRY3(90); // <-- Syscall para solicitar
    // IDT_ENTRY3(91); // <-- Syscall para recurso_listo
    // ...


// Ambas syscalls las voy a implementar en isr.asm, donde donde se definen las rutinas de atencion de interrupciones.
// asm
//     extern solicitar_handler
//     extern recurso_listo_handler

//     global _isr90  

//     _isr90:
//         pushad
//         push ebx                             ; Paso 'recurso_t' (recurso solicitado) por ebx
//         call solicitar_handler
//         add esp, 4

//         // "saltar a la próxima tarea"         ; Porque dice que no se devuelve el control hasta que este listo
//         call sched_next_task
//         mov word [sched_task_selector], ax
//         jmp far [sched_task_offset]          ; Desalojo la tarea

//         popad                                ;Restaurar los registros originales
//         iret

//     Voy a necesitar modificar el struct de sched.c asi puedo tener el control de que recursos
//     quiere cada tarea 


/**
 * Estructura usada por el scheduler para guardar la información pertinente de
 * cada tarea.
 */
typedef struct {
  int16_t selector;
  task_state_t state;
  recurso_t recurso_solicitado;
} sched_entry_t;


void solicitar_handler(recurso_t recurso){
    int8_t task_id = current_task;
    sched_entry_t* tarea = &sched_tasks[task_id];

    // 1. Guardar qué quiero
    tarea->recurso_solicitado = recurso;

    // 2. CORRECCIÓN: Buscar si hay un productor libre
    // (Soluciona: "No maneja caso de activar a una tarea productora libre")
    task_id_t id_productor = hay_tarea_disponible_para_recurso(recurso);

    if (id_productor != 0) {
        // ¡Hay uno libre! Lo despertamos para que empiece a trabajar.
        sched_enable_task(id_productor);
    } 
    // Si id_productor es 0, significa que están todos ocupados.
    // Nos dormimos igual, y quedaremos en una "cola de espera" implícita
    // hasta que alguno se libere.

    // 3. Bloquearse (siempre)
    sched_disable_task(task_id);
}

// Ahora me voy a encargar de la syscall recurso_listo

//      global _isr91  

//      _isr91:
//          pushad
//          call recurso_listo_handler

//           ;"saltar a la próxima tarea"         
//          call sched_next_task
//          mov word [sched_task_selector], ax
//          jmp far [sched_task_offset]          ; Desalojamos la tarea
//          popad                                ;Restaurar los registros originales
//          iret

void recurso_listo_handler(){

    int8_t id_productor = current_task;
    // La función 'para_quien_produce' nos dice quién pidió esto (o 0 si fue manual)
    task_id_t id_consumidor = para_quien_produce(id_productor);

    // --- CASO A: Hay un consumidor real esperando ---
    if (id_consumidor != 0) 
    {
        // 1. Obtener CR3s
        uint32_t cr3_productor = rcr3(); // Soy yo
        uint32_t cr3_consumidor = task_selector_to_CR3(sched_tasks[id_consumidor].selector);

        // 2. CORRECCIÓN MEMORIA: Garantizar que las páginas existan
        // Antes de traducir, nos aseguramos que estén mapeadas y limpias.
        garantizar_y_limpiar_pagina(cr3_productor, VIR_DIR_A);
        garantizar_y_limpiar_pagina(cr3_consumidor, VIR_DIR_B);

        // 3. Ahora es SEGURO traducir
        uint32_t PHY_DIR_A = virt_to_phy(cr3_productor, VIR_DIR_A);
        uint32_t PHY_DIR_B = virt_to_phy(cr3_consumidor, VIR_DIR_B);

        // 4. Copiar
        copy_page(PHY_DIR_B, PHY_DIR_A); // (Destino, Origen)
        
        // 5. Despertar al consumidor (ya tiene su recurso)
        sched_enable_task(id_consumidor); 
        
        // Limpiamos el pedido en el struct del consumidor (buena práctica)
        sched_tasks[id_consumidor].recurso_solicitado = 0;
    }

    // --- CASO B: Siempre (sea manual o para consumidor) ---
    
    // 6. Restaurar al productor a su estado inicial
    restaurar_tarea(id_productor);

    // 7. Pausar al productor (se "desaloja" como pide el enunciado)
    sched_disable_task(id_productor);
    
    // Nota: El ASM se encargará de hacer el salto a otra tarea.
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
 * Obtiene la dirección física de una página a 4)partir de su dirección virtual y su cr3
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



// Función auxiliar para verificar si una dirección virtual está mapeada 
//  en un mapa de memoria (cr3) dado.

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

  return true; 
}


// ## Ejercicio 2 (25 puntos):

// Dado el id de una tarea tengo que restaurarlo, es decir, restaurar los valores de la tss.
// Necesitamos acceder al array de TSS y constantes
extern tss_t tss_tasks[MAX_TASKS]; //

void restaurar_tarea(task_id_t id_tarea) {
    
    // 1. Obtener la TSS existente de la tarea
    //    (No creamos una nueva, modificamos la que ya tiene)
    tss_t* tss = &tss_tasks[id_tarea];

    // 2. "Rebobinar" la ejecución: Resetear EIP al inicio del código
    //    Usamos la constante que define donde arranca el código de usuario
    tss->eip = TASK_CODE_VIRTUAL; // (0x08000000)

    // 3. Resetear la Pila: Poner ESP al tope de la pila
    //    La pila empieza en BASE + TAMAÑO (crece hacia abajo)
    tss->esp = TASK_STACK_BASE + PAGE_SIZE; //
    tss->ebp = TASK_STACK_BASE + PAGE_SIZE;

    // 4. Resetear Flags (Importante para habilitar interrupciones)
    tss->eflags = EFLAGS_IF; // (0x202)

    // 5. Limpiar Registros (Opcional, pero buena práctica para "estado cero")
    tss->eax = 0; tss->ebx = 0; tss->ecx = 0;
    tss->edx = 0; tss->esi = 0; tss->edi = 0;

    // 6. Limpiar Memoria "Sucia"
    //    El enunciado dice que solo usó 0x0AAAA000 y 0x0BBBB000.
    //    Las desmapeamos para que la próxima vez estén limpias (page fault).
    //    IMPORTANTE: Mantenemos el mismo CR3, solo limpiamos lo extra.
    
    uint32_t cr3 = tss->cr3; // Usamos su propio CR3

    // Usamos tu helper 'mmu_is_page_mapped' para no romper nada
    if (mmu_is_page_mapped(cr3, VIR_DIR_A)) {
        mmu_unmap_page(cr3, VIR_DIR_A);
    }
    if (mmu_is_page_mapped(cr3, VIR_DIR_B)) {
        mmu_unmap_page(cr3, VIR_DIR_B);
    }
}



// ## Ejercicio 3 (15 puntos):
// Detallar todas las modificaciones necesarias para que el kernel provea el arranque manual mediante interrupción externa.
// Se pueden asumir implementadas las funciones dadas en el ejercicio 1.

// Primero debería agregar la entrada para esta interrupcion en la idt, en el archivo isr.asm (nivel 0).

    IDT_ENTRY0(41); // <-- Syscall para recurso


// Luego tengo que hacer la rutina de interrupción, pasandole como parametro lo que hay en la direccion 0xFAFAFA.
// Como el enunciado dice "bajo la misma lógica que solicitar" puedo llamar al handler de solicitar (solicitar_handler).

// asm
//  global _isr41 
//  _isr41:

//      pushad
//      call pic_finish1                   ; Le decimos al PIC que vamos a atender la interrupción
//      mov eax, [0xFAFAFA]                    ; Paso 'recurso_t' (recurso solicitado) 
//      push eax
//      call solicitar_handler
//      add esp, 4

//       ;"saltar a la próxima tarea"          Porque dice que no se devuelve el control hasta que este listo
//      call sched_next_task
//      mov word [sched_task_selector], ax
//      jmp far [sched_task_offset]          ; Desalojamos la tarea
//      popad                                ;Restaurar los registros originales
//      iret



// ## Ejercicio 4 (10 puntos):
// ¿Cómo modificarían el sistema para almacenar qué recurso produce cada tarea?

// Para esto agregaria en el struct de sched.c "recurso_t recurso_producido"
// Pregunta: ¿Cómo modificarían el sistema para almacenar qué recurso produce cada tarea?

// Respuesta:
// Para esto agregaría en la estructura 'sched_entry_t' (en sched.c) un nuevo campo:
// 'recurso_t recurso_producido;'

// Esto permite que cada tarea tenga asociado estáticamente qué recurso sabe fabricar.
// Esta información sería inicializada al crear las tareas (en tasks.c) y utilizada 
// por funciones como 'hay_tarea_disponible_para_recurso' para encontrar al productor adecuado.

// Pregunta: ¿Cómo modificarían el sistema para llevar registro de qué tareas están esperando?

// Respuesta:
// El sistema ya lo lleva implícitamente gracias a la modificación del Ejercicio 1.
// Al agregar 'recurso_t recurso_solicitado' en 'sched_entry_t', podemos saber qué tarea espera qué cosa.
// La función 'hay_consumidora_esperando(recurso)' simplemente debe iterar sobre el array 
// 'sched_tasks' y buscar la primera tarea cuyo estado sea TASK_PAUSED y cuyo 'recurso_solicitado' 
// coincida con el recurso buscado.

// En sched.c

typedef struct {
  int16_t selector;
  task_state_t state;
  
  // Ejercicio 1: Qué quiere consumir esta tarea (0 si nada)
  recurso_t recurso_solicitado;
  
  // Ejercicio 4: Qué sabe producir esta tarea (Fijo)
  recurso_t recurso_producido;
  
  // Necesario para 'para_quien_produce': A quién está sirviendo ahora (0 si libre)
  task_id_t cliente_actual; 

} sched_entry_t;

extern sched_entry_t sched_tasks[MAX_TASKS];

/**
 * Dado un recurso, busca una tarea que lo produzca y que esté libre.
 * Retorna el ID de la tarea o 0 si no hay ninguna.
 */
task_id_t hay_tarea_disponible_para_recurso(recurso_t recurso) {
    
    // Iteramos por todas las tareas del sistema
    for (int i = 0; i < MAX_TASKS; i++) {
        
        // 1. ¿Esta tarea produce lo que busco?
        if (sched_tasks[i].recurso_producido == recurso) {
            
            // 2. ¿Está libre? (Asumimos que 'libre' significa que está
            //    dormida esperando trabajo y no tiene cliente asignado)
            if (sched_tasks[i].state == TASK_PAUSED && 
                sched_tasks[i].cliente_actual == 0) {
                
                return (task_id_t)i;
            }
        }
    }
    
    return 0; // No encontré nadie disponible
}

/**
 * Dado un ID de tarea productora, devuelve el ID de su cliente.
 * Retorna 0 si fue un arranque manual o si no está produciendo.
 */
task_id_t para_quien_produce(task_id_t id_tarea) {
    return sched_tasks[id_tarea].cliente_actual;
}
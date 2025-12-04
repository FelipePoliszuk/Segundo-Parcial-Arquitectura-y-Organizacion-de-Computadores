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
// #define 

extern void swap_handler(uint32_t id_tarea);
// extern
// extern


// Primer ejercicio

// a) Programar una syscall swap que permita intercambiar sus registros con la tarea destino de forma bloqueante, esto es:
// • La tarea que llama a swap debe continuar su ejecución solamente cuando los registros ya fueron intercambiados



// Primero en el archivo idt.c debo agregar las entradas para estas syscalls. (nivel usuario, 3)

// ...
// IDT_ENTRY3(90); // <-- Syscall para swap
// IDT_ENTRY3(91); // <-- Syscall para swap_now
// ...


// La syscall la debo implementar en el archivo isr.asm donde se definen las rutinas de atencion de interrupciones.

// extern swap
// global _isr90  

// _isr90:
//     pushad
//     push ebx                             ; Pasamos 'id_tarea'
//     call swap_handler
//     cmp al, 1                            ; ¿Devolvió true?
//     je .fin                              ; Si sí segui corriendo la misma tarea porque hubo swap

// ; --- Si 'al' es 0 ---
// .noSwap:

//      "saltar a la próxima tarea"
//     call sched_next_task
//     mov word [sched_task_selector], ax
//     jmp far [sched_task_offset]          ; Desalojamos la tarea

// .fin:
//     popad
//     iret                      


// En c ahora voy a implementar swap_handler, podría ir en el archivo sched.c


// Voy a tener que agrandar la estructura de sched_entry_t:

/**
 * Estructura usada por el scheduler para guardar la información pertinente de
 * cada tarea.
 */
// typedef struct {
//   int16_t selector;
//   task_state_t state;
//   task_id id_swapeo;

// } sched_entry_t;

// static sched_entry_t sched_tasks[MAX_TASKS] = {0};


bool swap_handler(task_id id_tarea){
    
    int8_t task_id = current_task;
    sched_entry_t* tarea_a_swapear = &sched_tasks[id_tarea];        // obtengo la ficha de la tarea a swapear
    sched_entry_t* tarea = &sched_tasks[task_id];                  // obtengo la ficha de la tarea actual



    if (task_id == tarea_a_swapear->id_swapeo)     //  si la otra tarea queria swapear entonces se swapean las tss
    {
        tss_t* tss_actual = &tss_tasks[task_id];
        tss_t* tss_destino = &tss_tasks[id_tarea];

        swap_registers(tss_actual, tss_destino);        //realizo el swapeo

        tarea->id_swapeo = 0;
        tarea_a_swapear->id_swapeo = 0;

        sched_enable_task(id_tarea);        //despierto la tarea destino

        return true;
        
    } else {
        sched_disable_task(task_id);       //pauso la tarea
        tarea->id_swapeo = id_tarea;
        return false;
    }
    
}


/**
 * Intercambia los 6 registros de propósito general entre dos TSS.
 * NO intercambia EBP, ESP, EIP, EFLAGS, CR3, etc.
 */
void swap_registers(tss_t* tss_a, tss_t* tss_b) {
    uint32_t temp;

    // EAX
    temp = tss_a->eax;
    tss_a->eax = tss_b->eax;
    tss_b->eax = temp;

    // EBX
    temp = tss_a->ebx;
    tss_a->ebx = tss_b->ebx;
    tss_b->ebx = temp;

    // ECX
    temp = tss_a->ecx;
    tss_a->ecx = tss_b->ecx;
    tss_b->ecx = temp;

    // EDX
    temp = tss_a->edx;
    tss_a->edx = tss_b->edx;
    tss_b->edx = temp;

    // ESI
    temp = tss_a->esi;
    tss_a->esi = tss_b->esi;
    tss_b->esi = temp;

    // EDI
    temp = tss_a->edi;
    tss_a->edi = tss_b->edi;
    tss_b->edi = temp;
}


// b) swap_now

// Al igual que swap, tengo que hacer la rutina para luego llamar a swap_now_handler.

// extern swap
// global _isr91  ; 

// _isr91:
//     pushad
//     push ebx                             ; Pasamos 'id_tarea'
//     call swap_now_handler
//     cmp al, 1                            ; ¿Devolvió true?
//     je .fin                              ; Si sí segui corriendo la misma tarea porque hubo swap

// ; --- Si 'al' es 0 ---
// .noSwap:

//      "saltar a la próxima tarea"
//     call sched_next_task
//     mov word [sched_task_selector], ax
//     jmp far [sched_task_offset]          ; Desalojamos la tarea

// .fin:
//     popad
//     iret        

// y luego la implementacino del handler:

bool swap_now_handler(task_id id_tarea) {
    
    int8_t task_id = current_task;
    sched_entry_t* tarea_a_swapear = &sched_tasks[id_tarea];        // obtengo la ficha de la tarea a swapear
    sched_entry_t* tarea = &sched_tasks[task_id];                  // obtengo la ficha de la tarea actual

    if (tarea_a_swapear->id_swapeo == task_id) {
        
        // --- ¡SWAP! ---
        
        // 1. Obtenemos punteros a las TSS
        tss_t* tss_actual = &tss_tasks[task_id];
        tss_t* tss_destino = &tss_tasks[id_tarea];

        // 2. Swapeamos SOLO los registros
        swap_registers(tss_actual, tss_destino);
        /////aca

        // 3. Reseteamos las intenciones de swap
        tarea_actual->id_swapeo = 0; 
        tarea_a_swapear->id_swapeo = 0;

        // 4. Despertamos a la tarea destino (si estaba en 'swap' bloqueante)
        if (tarea_destino->state == TASK_PAUSED) {
            sched_enable_task(id_tarea);
        }

        // 5. Devolvemos 'true'. El ASM hará 'popad' e 'iret'.
        //    La tarea actual "continúa con su ejecución"
        return true; 

    } else {
        return false;
    }
}


// Segundo ejercicio

// La syscall corre en Nivel 0 pero dentro del contexto de memoria (CR3) de la tarea que llamó, podemos escribir 
// directamente en su dirección virtual.

// La única "trampa" del ejercicio es que pide informar a ambas tareas (la actual y la destino). 
// Para la tarea destino (que está "dormida" y tiene otro CR3), sí necesitamos usar la "traducción manual"


void informar_swap_status(int8_t task_id, uint8_t status) {
    
    uint8_t* puntero_destino;

    if (task_id == current_task) {
        // --- CASO 1: Es la Tarea Actual ---
        // Estamos en el CR3 de la tarea y tenemos Nivel 0.
        // Escribimos directamente en la dirección virtual.
        puntero_destino = (uint8_t*)VIRT_DIR;

    } else {
        // --- CASO 2: Es la Tarea Destino (dormida) ---
        // Usamos nuestro traductor para obtener un puntero
        // físico/kernel que SÍ podamos escribir.
        puntero_destino = (uint8_t*)traducir_puntero_tarea(VIRT_DIR, task_id);
    }

    // Escribimos el valor (1 para éxito, 0 para fallo)
    if (puntero_destino != NULL) {
        *puntero_destino = status;
    }
}

/**
 * Traduce una dirección virtual de una TAREA específica a una 
 * dirección física que el KERNEL puede leer.
 * Asume que el kernel tiene identity mapping para toda la RAM.
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

// Modificacion de los handlers de las syscalls:

bool swap_handler(task_id id_tarea_destino) {
    
    int8_t id_tarea_actual = current_task;
    sched_entry_t* tarea_actual = &sched_tasks[id_tarea_actual];
    sched_entry_t* tarea_destino = &sched_tasks[id_tarea_destino];

    if (tarea_destino->id_swapeo == id_tarea_actual) {
        
        // --- ¡SWAP! ---
        tss_t* tss_actual = &tss_tasks[id_tarea_actual];
        tss_t* tss_destino = &tss_tasks[id_tarea_destino];
        swap_registers(tss_actual, tss_destino);
        tarea_actual->id_swapeo = 0;
        tarea_destino->id_swapeo = 0;
        sched_enable_task(id_tarea_destino);

        // --- INICIO EJERCICIO 2 ---
        // Informamos "1" (éxito) a AMBAS tareas
        informar_swap_status(id_tarea_actual, 1);
        informar_swap_status(id_tarea_destino, 1);
        // --- FIN EJERCICIO 2 ---

        return true; 

    } else {
        // --- BLOQUEO ---
        tarea_actual->state = TASK_PAUSED;
        tarea_actual->id_swapeo = id_tarea_destino;

        // --- INICIO EJERCICIO 2 ---
        // Informamos "0" (todavía no) a la tarea actual
        informar_swap_status(id_tarea_actual, 0);
        // --- FIN EJERCICIO 2 ---

        return false;
    }
}


bool swap_now_handler(task_id id_tarea_destino) {
    
    int8_t id_tarea_actual = current_task;
    sched_entry_t* tarea_actual = &sched_tasks[id_tarea_actual];
    sched_entry_t* tarea_destino = &sched_tasks[id_tarea_destino];

    if (tarea_destino->id_swapeo == id_tarea_actual) {
        
        // --- ¡SWAP! ---
        tss_t* tss_actual = &tss_tasks[id_tarea_actual];
        tss_t* tss_destino = &tss_tasks[id_tarea_destino];
        swap_registers(tss_actual, tss_destino);
        tarea_actual->id_swapeo = 0; 
        tarea_destino->id_swapeo = 0;
        if (tarea_destino->state == TASK_PAUSED) {
            sched_enable_task(id_tarea_destino);
        }

        // --- INICIO EJERCICIO 2 ---
        // Informamos "1" (éxito) a AMBAS tareas
        informar_swap_status(id_tarea_actual, 1);
        informar_swap_status(id_tarea_destino, 1);
        // --- FIN EJERCICIO 2 ---

        return true; 

    } else {
        // --- NO HAY SWAP ---
        
        // --- INICIO EJERCICIO 2 ---
        // Informamos "0" (falló esta vez) a la tarea actual
        informar_swap_status(id_tarea_actual, 0);
        // --- FIN EJERCICIO 2 ---

        return false;
    }
}

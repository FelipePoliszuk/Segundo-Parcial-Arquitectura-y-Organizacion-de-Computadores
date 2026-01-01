#include "defines.h"
#include "i386.h"
#include "mmu.c"
#include "mmu.h"
#include "sched.c"
#include "task_defines.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "gdt.h"
#include "tss.h"


// (50 pts) Definir el mecanismo por el cual las syscall crear_pareja, juntarse_con y abandonar_pareja recibirán 
// sus parámetros y retornarán sus resultados según corresponda. Dar una implementación para cada una de las syscalls. 
// Explicitar las modificaciones al kernel que sea necesario realizar, como pueden ser estructuras o funciones auxiliares.

#define VIRT_DIR 0xC0C00000

#define MAX_MEMORY (4 * 1024 * 1024)

// funciones:

task_id pareja_de_actual();                 // si la tarea actual está en pareja devuelve el task_id de su pareja, o devuelve 0 si la tarea actual no está en pareja.
bool es_lider(task_id tarea);               // indica si la tarea pasada por parámetro es lider o no.
bool aceptando_pareja(task_id tarea);       // si la tarea pasada por parámetro está en un estado que le permita formar pareja devuelve 1, si no devuelve 0.
void conformar_pareja(task_id tarea);       // informa al sistema que la tarea actual y la pasada por parámetro deben ser emparejadas. Al pasar 0 por parámetro, se indica al sistema que la tarea actual está disponible para ser emparejada.
void romper_pareja();                       // indica al sistema que la tarea actual ya no pertenece a su pareja actual. Si la tarea actual no estaba en pareja, no tiene efecto.


// Dentro de sched.c deberia modificar el struct de sched_entry_t agregando el task_id ? Vi que 
// en otros ejercicios lo usan sin necesidad de agregarlo al struct asi que asumo que lo puedo usar...


// Tengo que implementar estas tres syscalls:

void crear_pareja();
int juntarse_con(int id_tarea);
void abandonar_pareja();



// En el archivo idt.c debo agregar las entradas para estas syscalls. (nivel usuario, 3)

// ...
// IDT_ENTRY3(90); // <-- Syscall para crear_pareja
// IDT_ENTRY3(91); // <-- Syscall para juntarse_con
// IDT_ENTRY3(92); // <-- Syscall para abandonar_pareja
// ...


// Las llamadas a estas syscalls las debo hacer desde isr.asm donde se definen las rutinas de atencion de interrupciones.

// ; En isr.asm
// extern crear_pareja
// extern juntarse_con
// extern abandonar_pareja
// extern es_lider

// ;; Rutina de atención de closedevice
// ;; --------------------------------------------------------------------------
// ;;


// global _isr90  ; Syscall crear_pareja
// _isr90:

//  pushad
//  call crear_pareja

// ; Ahora saltamos a otra tarea pues la actual se puso en pausa

//     call sched_next_task  
//     mov word [sched_task_selector], ax
//     jmp far [sched_task_offset]

// global _isr91  ; Syscall juntarse_con

// _isr91:
//     pushad
//     push ebx      ; Pasamos 'id_tarea'
//     call juntarse_con
//     add esp, 4
//     mov [esp + offset_EAX], eax ; Ponemos el 0 o 1 de retorno en EAX
//     popad
// 	   iret

// global _isr92  ; Syscall abandonar_pareja
// _isr92:

//  pushad
//  call abandonar_pareja
//  call es_lider 

//     cmp al, 0            	 ; ¿Devolvió false?
//     je .next_task             ; salto a next_task

// .fin 
//  popad
//  iret

// next_task:
//     call sched_next_task  
//     mov word [sched_task_selector], ax
//     jmp far [sched_task_offset]

// Implemento la funcion crear_pareja en el archivo sched.c

void crear_pareja() {

  if (pareja_de_actual() == 0) {        //si la tarea ya tiene pareja retorna de inmediato
    return; 
  }
  conformar_pareja(0);
  int8_t task_id = current_task;
  sched_entry_t *tarea = &sched_tasks[task_id]; //no estoy seguro que el id sea lo mismo que el index en la tabla sched

  tarea->state = TASK_PAUSED;
}

int juntarse_con(int id_tarea) {

  if (!(pareja_de_actual() == 0)) { // si la tarea ya tiene pareja retorna 1
    return 1;
  }

  if (aceptando_pareja(id_tarea) == 0) { // si la tarea no esta aceptando pareja retorna 1
    return 1;
  }

  conformar_pareja(id_tarea); // forma la pareja con la actual y la pasada por parametro
  sched_enable_task(id_tarea); //activa la tarea lider

  return 0;
}

void abandonar_pareja() {

  int8_t task_id = current_task;
  sched_entry_t *tarea =
      &sched_tasks[task_id]; // no estoy seguro que el id sea lo mismo que el
                             // index en la tabla sched

  if (pareja_de_actual() == 0) { // si no esta en pareja retorno inmediatamente
    return;
  }

  if (!(pareja_de_actual() == 0) &&
      !(es_lider(task_id))) { // si no esta en pareja y no es el lider entonces
                              // se rompe la pareja
    romper_pareja();
    for (uint32_t v_addr = VIRT_DIR; v_addr < ON_DEMAND_MEM_END_VIRTUAL;
         v_addr += PAGE_SIZE) {
      mmu_unmap_page(rcr3(), v_addr);
    }

    if (!(pareja_de_actual() == 0) &&
        (es_lider(task_id))) { // si no esta en pareja y es el lider entonces se
                               // pone estado bloqueado
      tarea->state = TASK_BLOCKED;

      // segun el diagrama aca a pesar de estar en pausa la tarea, sigue en
      // pareja, por lo cual en este caso no pierde acceso a memoria
    }
  }
}


// En cuanto a la parte de memoria entiendo que deberia cambiar algo en la funcion page_fault_handler

// En defines deberia cambiar el valor de ON_DEMAND_MEM_START_VIRTUAL y ON_DEMAND_MEM_END_VIRTUAL por lo que nos dice el enunciado


// direccion virtual de memoria compartida on demand
#define ON_DEMAND_MEM_START_VIRTUAL    0xC0C00000
#define ON_DEMAND_MEM_END_VIRTUAL      (0xC0C00000 + MAX_MEMORY)
// #define ON_DEMAND_MEM_START_PHYSICAL   0x03000000


  // Voy a necesitar un nuevo struct que podria ir en sched.c

typedef struct {
    paddr_t paginas_fisicas[1024]; // 1024 páginas = 4MB
    int8_t  id_lider;
    int8_t  id_pareja;
    // ...otros estados...
} pareja_mem_t;

static pareja_mem_t g_parejas_mem_pool[MAX_TASKS / 2];


// Agregaria un chequeo para que ver si la tarea es lider

bool page_fault_handler(vaddr_t virt) {
    // Solo nos interesa manejar el area "on demand"
    if (virt < ON_DEMAND_MEM_START_VIRTUAL || virt >= ON_DEMAND_MEM_END_VIRTUAL)
        return false;

    int8_t task_id = current_task;
    // Conseguir el puntero a la pareja de la tarea actual:
    pareja_mem_t *pareja = &g_parejas_mem_pool[task_id]; 			

    uint32_t indice = (virt - ON_DEMAND_MEM_START_VIRTUAL) / 4096;

    // Si la página física de ese índice de pareja no está asignada, la pedimos y la guardamos
    if (pareja->paginas_fisicas[indice] == 0) {
        paddr_t nueva_pagina = mmu_next_free_user_page();
        zero_page(nueva_pagina);
        pareja->paginas_fisicas[indice] = nueva_pagina;
    }

    // Usamos la página física correspondiente para mapear la virtual de esta tarea
    uint32_t attrs = MMU_P | MMU_U;
    if (es_lider(task_id)) {
        attrs |= MMU_W; // El líder puede escribir
    }
    mmu_map_page(rcr3(), virt, pareja->paginas_fisicas[indice], attrs);

    return true;
}

//   habria que hacer un cambio en mmu_map_page ya que en el tp estaba hecha para pedir pagina nivel kernel 
//   y aca necesitamos que sea usuario, entonces quedaria asi:

void mmu_map_page(uint32_t cr3, vaddr_t virt, paddr_t phy, uint32_t attrs) {

	pd_entry_t *pdb = (pd_entry_t *)CR3_TO_PAGE_DIR(cr3); 
  
	uint32_t pdi = VIRT_PAGE_DIR(virt);
	uint32_t pti = VIRT_PAGE_TABLE(virt);
  
	if ((pdb[pdi].attrs & MMU_P) == 0) {
	  paddr_t addr = mmu_next_free_user_page();
	  zero_page(addr);
	  pdb[pdi].pt = (addr >> 12);
	}
  
	pt_entry_t *ptb = (pt_entry_t *)MMU_ENTRY_PADDR(pdb[pdi].pt);
  
	pdb[pdi].attrs = pdb[pdi].attrs | attrs | MMU_P;
	ptb[pti].attrs = ptb[pti].attrs | attrs | MMU_P;
  
	ptb[pti].page = (phy >> 12);
  
	tlbflush();
  }


// Tengo que ampliar el struct de sched.c

// Estados de la tarea (los que ya tenías)
typedef enum {
  TASK_SLOT_FREE,
  TASK_RUNNABLE,
  TASK_PAUSED // Usaremos PAUSED para "bloqueado" o "esperando"
} task_state_t;

// NUEVO: Estados de pareja de una tarea (basado en el diagrama)
typedef enum {
  PAREJA_NULL,        // 0: No tengo pareja
  PAREJA_ESPERANDO,   // 1: Soy líder, esperando (y estoy TASK_PAUSED)
  PAREJA_LIDER,       // 2: En pareja, soy el líder (R/W)
  PAREJA_COMPANERO,   // 3: En pareja, soy el compañero (R-Only)
  PAREJA_LIDER_SOLO   // 4: Mi compañero abandonó, estoy bloqueado (TASK_PAUSED)
} pareja_state_t;

// Tu struct de tarea, ahora modificada
typedef struct {
  int16_t selector;
  task_state_t state; // Estado del scheduler (RUNNABLE, PAUSED)
  
  // --- NUEVO PARA PARCIAL PAREJAS ---
  pareja_state_t pareja_estado; // Mi estado de pareja (de la enum de arriba)
  int8_t           id_companero;  // El task_id de mi líder o compañero
} sched_entry_t;

// Tu array de tareas (¡ahora tiene toda la info que necesitamos!)
static sched_entry_t sched_tasks[MAX_TASKS] = {0};

// (En tu `sched_init()`, deberías inicializar 
//  `sched_tasks[i].pareja_estado = PAREJA_NULL;` para todas las tareas)


/**
 * si la tarea actual está en pareja devuelve el task_id de su pareja,
 * o devuelve 0 si la tarea actual no está en pareja.
 */
 task_id pareja_de_actual() {
  // 1. Obtener la "ficha" de la tarea actual
  sched_entry_t* tarea = &sched_tasks[current_task];
  
  // 2. Revisar su estado de pareja
  //    (Solo devolvemos un ID si la pareja está conformada)
  if (tarea->pareja_estado == PAREJA_LIDER || 
      tarea->pareja_estado == PAREJA_COMPANERO) {
      
      return tarea->id_companero;
  }
  
  // Si está en NULL, ESPERANDO, o LIDER_SOLO, no tiene una pareja *activa*.
  return 0;
}


/**
 * indica si la tarea pasada por parámetro es lider o no.
 */
 bool es_lider(task_id tarea) {
  // 1. Obtener la "ficha" de la tarea que nos piden (¡usamos el parámetro!)
  sched_entry_t* tarea = &sched_tasks[tarea];
  
  // 2. Es líder si está en cualquiera de los 3 estados de líder
  return (tarea->pareja_estado == PAREJA_ESPERANDO ||
          tarea->pareja_estado == PAREJA_LIDER ||
          tarea->pareja_estado == PAREJA_LIDER_SOLO);
}



/**
 * si la tarea pasada por parámetro está en un estado que le
 * permita formar pareja devuelve 1, si no devuelve 0.
 */
 bool aceptando_pareja(task_id tarea) {
  // 1. Obtener la "ficha" de la tarea que nos piden (el líder)
  sched_entry_t* lider = &sched_tasks[tarea];
  
  // 2. Una tarea "acepta pareja" si y solo si está en el estado
  //    "esperando" (que es el estado en el que entra 
  //    después de llamar a crear_pareja())
  return (lider->pareja_estado == PAREJA_ESPERANDO);
}

/**
 * informa al sistema que la tarea actual y la pasada por parámetro
 * deben ser emparejadas. Al pasar 0 por parámetro, se indica
 * al sistema que la tarea actual está disponible para ser emparejada.
 */
 void conformar_pareja(task_id tarea) {
    
  int8_t task_id_actual = current_task;
  sched_entry_t* actual = &sched_tasks[task_id_actual];

  if (tarea == 0) {
      // --- Caso: Tarea actual llama a crear_pareja() ---
      // 1. Ponemos su estado en "esperando"
      actual->pareja_estado = PAREJA_ESPERANDO;
      actual->id_companero = 0; // Aún no tiene compañero
      
  } else {
      // --- Caso: Tarea actual llama a juntarse_con(id_lider) ---
      int8_t id_lider = tarea;
      sched_entry_t* lider = &sched_tasks[id_lider];
      
      // 1. Actualizamos el estado de la tarea actual (el compañero)
      actual->pareja_estado = PAREJA_COMPANERO;
      actual->id_companero = id_lider; // Guardamos el ID del líder

      // 2. Actualizamos el estado del líder
      lider->pareja_estado = PAREJA_LIDER;
      lider->id_companero = task_id_actual; // Guardamos el ID del compañero
  }
}


/**
 * indica al sistema que la tarea actual ya no pertenece a su pareja actual.
 * Si la tarea actual no estaba en pareja, no tiene efecto.
 */
 void romper_pareja() {
  int8_t task_id_actual = current_task;
  sched_entry_t* actual = &sched_tasks[task_id_actual];

  // 1. Verificamos si realmente tenía pareja
  if (actual->pareja_estado == PAREJA_NULL || 
      actual->pareja_estado == PAREJA_ESPERANDO) {
      return; // No tiene efecto
  }

  // 2. Obtenemos el ID y la "ficha" del compañero
  int8_t id_companero = actual->id_companero;
  sched_entry_t* companero = &sched_tasks[id_companero];

  bool yo_era_lider = (actual->pareja_estado == PAREJA_LIDER);

  // 3. Rompemos la pareja para la tarea actual
  actual->pareja_estado = PAREJA_NULL;
  actual->id_companero = 0;

  // 4. Actualizamos el estado del compañero
  if (yo_era_lider) {
      // Yo era el líder, el compañero queda libre
      companero->pareja_estado = PAREJA_NULL;
      companero->id_companero = 0;
  } else {
      // Yo era el compañero, el líder pasa a "LÍDER_SOLO"
      // (La syscall 'abandonar_pareja' se encargará de bloquearlo)
      companero->pareja_estado = PAREJA_LIDER_SOLO;
      // Mantenemos su 'id_companero' para que sepa quién era
  }
}


// Escribir la función uso_de_memoria_de_las_parejas, la cual permite averiguar cuánta memoria está siendo 
// utilizada por el sistema de parejas. Ésta función debe ser implementada por medio de código ó pseudocódigo 
// que corra en nivel 0.


/* * Asumimos que estas estructuras (que definimos en el ejercicio anterior)
 * existen en el kernel, por ejemplo en sched.c
 */
 extern sched_entry_t sched_tasks[MAX_TASKS];     //
 extern pareja_mem_t g_parejas_mem_pool[MAX_TASKS / 2];
 
 
 /**
  * 4. Escribir la función uso_de_memoria_de_las_parejas
  * (Implementación Correcta)
  */
 uint32_t uso_de_memoria_de_las_parejas() {
     
     uint32_t paginas_en_uso = 0;
     
     // 1. Iteramos por el "pool" de ESTRUCTURAS DE MEMORIA de pareja.
     //    (NO por las tareas, para evitar el conteo doble).
     for (int i = 0; i < (MAX_TASKS / 2); i++) {
         
         pareja_mem_t* mem_pareja = &g_parejas_mem_pool[i];
 
         // 2. ¿Esta estructura de pareja está "activa"?
         //    (Es decir, tiene un líder o un compañero. Esto incluye
         //    a los líderes solitarios).
         if (mem_pareja->id_lider != 0 || mem_pareja->id_pareja != 0) {
             
             // 3. Sí. Ahora contamos cuántas páginas FÍSICAS ha usado
             //    esta pareja (contamos las páginas a las que se intentó acceder).
             for (int j = 0; j < 1024; j++) { // 1024 páginas = 4MB
                 
                 if (mem_pareja->paginas_fisicas[j] != 0) {
                     // Si la dirección física no es 0, significa que 
                     // el page_fault_handler la asignó en algún momento.
                     paginas_en_uso++;
                 }
             }
         }
         // Si id_lider y id_pareja son 0, la pareja se disolvió
         // y sus recursos cuentan como liberados.
     }
     
     // 4. Devolvemos el total en bytes
     return paginas_en_uso * PAGE_SIZE; // (PAGE_SIZE es 4096)
 }
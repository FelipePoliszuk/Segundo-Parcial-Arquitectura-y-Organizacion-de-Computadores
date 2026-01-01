#include "defines.h"
#include "mmu.c"
#include "sched.c"
#include "task_defines.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "gdt.h"
#include "tss.h"

extern uint8_t esMemoriaReservada(vaddr_t virt);


// Ejercicio 1: (50 puntos)

// Detallar todos los cambios que es necesario realizar sobre el kernel para que una tarea de nivel usuario 
// pueda pedir memoria y liberarla, asumiendo como ya implementadas las funciones mencionadas. 
// Para este ejercicio se puede asumir que garbage_collector está implementada y funcionando.


// funciones que puedo utilizar:

// void* malloco(size_t size);
// uint8_t esMemoriaReservada(virtaddr_t virt)
// void chau(virtaddr_t virt);
// reservas* dameReservas(int task_id);



// typedef struct {
  // uint32_t task_id;           // identifica a qué tarea corresponden las reservas de este item
  // reserva_t* array_reservas;  // array de reservas en donde una reserva es un struct reserva_t
  // uint32_t reservas_size;     // cantidad de elementos de array_reservas
// } reservas_por_tarea; 


// typedef struct {
//   uint32_t virt;              // direccion virtual donde comienza el bloque reservado
//   uint32_t tamanio;           // tamaño del bloque en bytes
//   uint8_t estado;             // 0 si la casilla está libre, 1 si la reserva está activa, 2 si la reserva está marcada para liberar, 3 si la reserva ya fue liberada
// } reserva_t; 


// Como es un sistema de lazy allocation, el kernel no asignará memoria física hasta que 
// la tarea intente acceder (por escritura o lectura) a la memoria reservada.

// Una tarea de nivel usuario tiene que poder pedir memoria y liberarla





bool page_fault_handler2(vaddr_t virt) {

    // 1. Chequeamos la lógica vieja (la página on-demand del TP3)
    if (virt >= ON_DEMAND_MEM_START_VIRTUAL &&
        virt <= ON_DEMAND_MEM_END_VIRTUAL) {

        mmu_map_page(rcr3(), virt, ON_DEMAND_MEM_START_PHYSICAL,
                     MMU_P | MMU_U | MMU_W);
        return true;
    }

    // 2. NUEVA LÓGICA (Ejercicio 1 de Malloco)
    //    Chequeamos si la dirección fue "reservada" con malloco
    if (esMemoriaReservada(virt) == true) {


        // Pedimos una página física limpia (como calloc)
        paddr_t nueva_pag_fisica = mmu_next_free_user_page();
        zero_page(nueva_pag_fisica); // zero_page usa kmemset (de mmu.c)

        // 2b. Mapeamos la página VIRTUAL que falló a la FÍSICA nueva
        //     Alineamos la dirección virtual al inicio de su página
        vaddr_t virt_alineada = virt & 0xFFFFF000; 
        mmu_map_page(rcr3(), virt_alineada, nueva_pag_fisica,
                     MMU_P | MMU_U | MMU_W); // Permisos R/W de Usuario

        // 2c. Avisamos que lo resolvimos
        return true;
    }

    // 3. Si no era ninguna de las dos, es un error real.
    return false; // Esto hará que la tarea muera
}


// ; En isr.asm debo modificar _isr14

// extern page_fault_handler2               ; Tu handler C que devuelve true/false
// extern kernel_exception_handler_malloco  ; Tu handler C que mata la tarea
// extern sched_next_task                 ; La función del scheduler

// global _isr14
// _isr14:
//     pushad
//     mov EAX, CR2                         ; Obtenemos la dirección que falló
//     push EAX                             
//     call page_fault_handler2             ; Llamamos a tu función C
//     add esp, 4
    
//     cmp al, 1                            ; ¿Devolvió true?
//     je .fin_pf_ok                        ; Si sí (fue lazy alloc), todo bien.

// ; --- Si 'al' es 0 (fallo real) ---
// .error_pf:
//     ; 1. "marcar la memoria" y "desalojar tarea"
//     call kernel_exception_handler_malloco
    
//     ; 2. "saltar a la próxima tarea"
//     ;    (Igual que en _isr32)
//     call sched_next_task
//     mov word [sched_task_selector], ax
//     jmp far [sched_task_offset]          ; Desalojamos la tarea

// .fin_pf_ok:
//     popad
//     add esp, 4                           ; Limpiamos el error code
//     iret


// debo implementar una funcion kernel_exception_handler_malloco para que mate la tarea 
// cuando page_fault_handler2 da false



void kernel_exception_handler_malloco() {
    
    // 1. Obtener la tarea actual (la que falló)
    int8_t task_id = current_task;

    // 2. "marcar la memoria reservada... para que sea liberada"
    reservas_por_tarea* res = dameReservas(task_id);
    if (res != NULL) {
        // Recorremos todas sus reservas
        for (int i = 0; i < res->reservas_size; i++) {
            // Si estaba activa (1), la marcamos (2)
            if (res->array_reservas[i].estado == 1) {
                res->array_reservas[i].estado = 2; 
            }
        }
    }

    // 3. "desalojar tarea... asegurarse de que no vuelva a correr"
    //    Usamos la función de tu scheduler para "pausarla" o "matarla".
    sched_disable_task(task_id); // (Pone el estado en TASK_PAUSED)
}


// Ejercicio 2: (25 puntos)

// Detallar todos los cambios que es necesario realizar sobre el kernel para incorporar la tarea garbage_collector 
// si queremos que se ejecute una vez cada 100 ticks del reloj. Incluir una posible implementación del código de la tarea.

// Para implementar nuestra tarea garbage_collector, necesitamos hacer tres cosas:

//     Crearla: Definir qué es esta tarea y crearla (su "Nacimiento").

//     Despertarla: Modificar el reloj del kernel para que la active cada 100 ticks (su "Despertador").

//     Implementarla: Escribir el código que ejecutará la tarea cuando esté despierta (su "Trabajo").


// Primero, debemos enseñarle al kernel cómo "construir" esta nueva tarea. A diferencia de las tareas de 
// usuario (Nivel 3), el garbage collector (GC) debe ser de Nivel 0 (Kernel) para tener permiso de hurgar 
// en la memoria de otras tareas y desmapear sus páginas.

// En el archivo tss.c se agregan las funciones para crear la tarea de Nivel 0


// ID de la nueva tarea. Asumimos 3 (0=idle, 1=A, 2=B)
#define GC_TASK_ID 3
#define GC_GDT_IDX (GDT_IDX_TASK_INITIAL + 3) 

// El código de la tarea
void task_garbage_collector_main(void);

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
 * Inicializa la tarea Garbage Collector.
 * Esta función debe ser llamada desde kernel.asm al arrancar.
 */
void garbage_collector_init() {
    
    // 1. Crear la TSS de Nivel 0
    tss_tasks[GC_TASK_ID] = tss_create_kernel_task((paddr_t)&task_garbage_collector_main);

    // 2. Crear la entrada en la GDT
    gdt[GC_GDT_IDX] = tss_gdt_entry_for_task(&tss_tasks[GC_TASK_ID]);

    // 3. Registrar en el scheduler.
    // sched_add_task la deja en estado TASK_PAUSED,
    // lo cual es perfecto.
    sched_add_task(GC_GDT_IDX << 3); 
}

// luego en kernel.asm hay que hacer la llamada de inicilización

// extern garbage_collector_init
    
//     ; ===================================
//     ; ... (después de sched_init y tss_init)
//     call garbage_collector_init
//     ; ===================================


// Debo modificar el scheduler para que active al garbage collector cada 100 ticks

// en el archivo sched.c:

// ... (al inicio del archivo, con las otras variables estáticas)

// --- INICIO CAMBIOS PARCIAL MALLOCO ---
// Contador para el Garbage Collector
static uint32_t gc_tick_counter = 0;
// ID de la tarea GC (debe coincidir con tss.c)
static const int8_t GC_TASK_ID2 = 3; 
// --- FIN CAMBIOS PARCIAL MALLOCO ---


/**
 * Obtiene la siguiente tarea disponible con una política round-robin.
 * MODIFICADO: Cada 100 ticks, despierta y ejecuta el Garbage Collector.
 * (Esta es la lógica que pide la corrección del PDF)
 *
 * @return uint16_t el selector de segmento de la tarea a saltar
 */
uint16_t sched_next_task2(void) {
  
  // --- INICIO CAMBIOS PARCIAL MALLOCO ---
  gc_tick_counter++;
  if (gc_tick_counter >= 100) {
      gc_tick_counter = 0;
      
      // Despertamos al GC (lo ponemos en TASK_RUNNABLE)
      sched_enable_task(GC_TASK_ID2);
      
      // Forzamos su ejecución AHORA
      current_task = GC_TASK_ID2;
      return sched_tasks[GC_TASK_ID2].selector;
  }
  // --- FIN CAMBIOS PARCIAL MALLOCO ---


  // Buscamos la próxima tarea viva (comenzando en la actual)
  int8_t i;
  for (i = (current_task + 1); (i % MAX_TASKS) != current_task; i++) {
    // Si esta tarea está disponible la ejecutamos
    if (sched_tasks[i % MAX_TASKS].state == TASK_RUNNABLE) {
      break;
    }
  }

  // Ajustamos i para que esté entre 0 y MAX_TASKS-1
  i = i % MAX_TASKS;

  // Si la tarea que encontramos es ejecutable entonces vamos a correrla.
  if (sched_tasks[i].state == TASK_RUNNABLE) {
    current_task = i;
    return sched_tasks[i].selector;
  }

  // En el peor de los casos no hay ninguna tarea viva. Usemos la idle como
  // selector.
  current_task = 0; // (Asumiendo que 0 es la Idle)
  return GDT_IDX_TASK_IDLE << 3;
}


/**
 * Loop principal del Garbage Collector.
 * Se ejecuta cada 100 ticks, limpia la memoria y se vuelve a dormir.
 */
 void task_garbage_collector_main() {
    
    while (true) {
        
        // 1. Iterar por TODAS las tareas (menos nosotros mismos e idle)
        for (int i = 1; i < MAX_TASKS; i++) {
            
            // No nos limpiamos a nosotros mismos
            if (i == GC_TASK_ID) continue; 
            
            reservas_por_tarea* res = dameReservas(i);
            if (res == NULL) continue;

            // 2. Iterar por todas las reservas de ESA tarea
            for (int j = 0; j < res->reservas_size; j++) {
                
                reserva_t* reserva_actual = &res->array_reservas[j];
                
                // 3. ¿Está marcada con estado 2 (lista para liberar)?
                if (reserva_actual->estado == 2) {
                    
                    // 4. Sí. Liberar todas sus páginas mapeadas
                    //    Obtenemos el CR3 de la tarea 'i' desde su TSS
                    uint32_t cr3_tarea = tss_tasks[i].cr3;
                    vaddr_t v_start = reserva_actual->virt;
                    vaddr_t v_end = v_start + reserva_actual->tamanio;

                    // Iteramos por CADA PÁGINA de la reserva
                    for (vaddr_t v = v_start; v < v_end; v += PAGE_SIZE) {
                        
                        // (Aquí usamos la corrección del PDF -
                        // desmapeamos la página. Asumimos que `mmu_unmap_page`
                        // también libera la página física, o al menos la
                        // saca del mapa de la tarea).
                        mmu_unmap_page(cr3_tarea, v);
                    }
                    
                    // 5. Marcar como "liberada"
                    reserva_actual->estado = 3; 
                    // (Opcional: poner virt=0, size=0, etc. como en el PDF)
                }
            }
        }
        
        // 6. Tarea terminada. Volver a "dormir".
        // Nos ponemos en PAUSED y cedemos la CPU.
        // El scheduler nos despertará en 100 ticks.
        sched_disable_task(current_task); // `current_task` es el ID del GC
        sched_yield();
    }
}


// Ejercicio 3:

// a)Indicar dónde podría estár definida la estructura que lleva registro de las reservas (5 puntos)


// Tanto sched.c como mmu.c son respuestas correctas y bien justificadas. sched.c es una buena elección si 
// lo ves como "más información de la tarea", y mmu.c es una buena elección si lo ves como "una nueva forma 
// de gestionar memoria".


// b)Dar una implementación para malloco (10 puntos)

// Considerando:

//     Como máximo, una tarea puede tener asignados hasta 4 MB de memoria total. Si intenta reservar más memoria, 
//     la syscall deberá devolver NULL.
//     El área de memoria virtual reservable empieza en la dirección 0xA10C0000
//     Cada tarea puede pedir varias veces memoria, pero no puede reservar más de 4 MB en total.




// IDT_ENTRY3(88);
// IDT_ENTRY3(98);
// IDT_ENTRY3(90); // <-- Syscall para malloco
// IDT_ENTRY3(91); // <-- Syscall para chau

// ; En isr.asm
// extern malloco
// extern chau

// global _isr90  ; Syscall malloco
// _isr90:
//     pushad
//     push ebx      ; Pasamos 'size' como parámetro a la función C
//     call malloco
//     add esp, 4
//     mov [esp + offset_EAX], eax ; Ponemos el valor de retorno (puntero)
//                                 ; en el EAX guardado en la pila
//     popad
//     iret

// global _isr91  ; Syscall chau
// _isr91:
//     pushad
//     push ebx      ; Pasamos 'virt' como parámetro a la función C
//     call chau
//     add esp, 4
//     popad
//     iret



#define MAX_MEMORY (4*1024*1024)

// --- En el mismo archivo .c ---

#define MAX_MEMORY (4*1024*1024) // 4MB

void* malloco(size_t size) {
    // 1. Obtener el contenedor de la tarea actual
    reservas_por_tarea* res = dameReservas(current_task);

    // 2. Chequear límite de 4MB
    if (res->memory_used + size > MAX_MEMORY) {
        return NULL;
    }

    // 3. Buscar un slot libre en el array de reservas
    for (int i = 0; i < MAX_RESERVAS_POR_TAREA; i++) {
        if (res->array_reservas[i].estado == 0) { // 0 = libre
            
            // 4. Encontramos un slot. Lo llenamos.
            vaddr_t virt_addr = res->next_free_virt;
            
            res->array_reservas[i].virt = virt_addr;
            res->array_reservas[i].tamanio = size;
            res->array_reservas[i].estado = 1; // 1 = activa

            // 5. Actualizar los contadores de la tarea
            res->next_free_virt += size; // Avanzamos la dir. virtual
            res->memory_used += size;
            res->reservas_activas++;

            // 6. Devolver el puntero
            return (void*)virt_addr;
        }
    }

    // No se encontraron slots libres (array_reservas lleno)
    return NULL;
}

// c)Dar una implementación para chau (10 puntos)

// Considerando:

//     Si se pasa un puntero que no fue asignado por la syscall malloco, el comportamiento de la syscall chau es indefinido.
//     Si se pasa un puntero que ya fue liberado, la syscall chau no hará nada.
//     Si se pasa un puntero que pertenece a un bloque reservado pero no es la dirección más baja, el comportamiento 
//     de la syscall chau es indefinido.
//     Si la tarea continúa usando la memoria una vez liberada, el comportamiento del sistema es indefinido.
//     No nos preocuparemos por reciclar la memoria liberada, bastará con liberarla

// Contamos con la función chau que recibe una dirección virtual y, si corresponde al comienzo de un bloque 
// reservado con malloco, marca esa reserva para que sea liberada más adelante por una tarea de nivel 0 
// (desarrollado más adelante). No nos preocuparemos por reciclar la memoria liberada, bastará con liberarla.


void chau(virtaddr_t virt) {
    // 1. Obtener el contenedor de la tarea actual
    reservas_por_tarea* res = dameReservas(current_task);

    // 2. Iterar por todas las reservas
    for (int i = 0; i < MAX_RESERVAS_POR_TAREA; i++) {
        
        // 3. Buscar la reserva activa que coincida
        if (res->array_reservas[i].virt == virt && 
            res->array_reservas[i].estado == 1) { // Solo si está activa
            
            // 4. Marcar para liberar
            res->array_reservas[i].estado = 2; // 2 = marcada para liberar

            // 5. Actualizar contadores
            res->memory_used -= res->array_reservas[i].tamanio;
            res->reservas_activas--;
            
            return; // Listo
        }
    }
    // Si no la encontró, o si ya estaba liberada (estado != 1),
    // no hace nada, cumpliendo la consigna.
}
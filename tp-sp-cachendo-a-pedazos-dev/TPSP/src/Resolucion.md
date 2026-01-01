
```
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
```

```
#define VIR_DIR_A 0x0AAAA000
#define VIR_DIR_B 0x0BBBB000
```


## Ejercicio 1 (50 puntos):

Funciones que puedo utilizar
```
task_id_t hay_tarea_disponible_para_recurso(recurso_t recurso);
task_id_t para_quien_produce(task_id_t id_tarea);
void restaurar_tarea(task_id_t id_tarea);
task_id_t hay_consumidora_esperando(recurso_t recurso);
```

Tengo que implementar las syscalls solicitar y recurso_listo, ambos tienen estados bloqueantes, es decir, 
cuando se llaman, se bloquean las tareas.

solicitar: syscall que permite a la tarea llamadora solicitar un recurso 
recurso_listo: syscall que permite a la tarea llamadora avisar que su recurso está listo.  

Para empezar, en el archivo idt.c debo agregar las entradas para estas syscalls.  (nivel 3)

```
    ...
    IDT_ENTRY3(90); // <-- Syscall para solicitar
    IDT_ENTRY3(91); // <-- Syscall para recurso_listo
    ...
```

Ambas syscalls las voy a implementar en isr.asm, donde donde se definen las rutinas de atencion de interrupciones.
```asm
    extern solicitar_handler
    extern recurso_listo_handler

    global _isr90  

    _isr90:
        pushad
        push ebx                             ; Paso 'recurso_t' (recurso solicitado) por ebx
        call solicitar_handler

        // "saltar a la próxima tarea"         ; Porque dice que no se devuelve el control hasta que este listo
        call sched_next_task
        mov word [sched_task_selector], ax
        jmp far [sched_task_offset]          ; Desalojo la tarea

```
    Voy a necesitar modificar el struct de sched.c asi puedo tener el control de que recursos
    quiere cada tarea 


```c
/**
 * Estructura usada por el scheduler para guardar la información pertinente de
 * cada tarea.
 */
typedef struct {
  int16_t selector;
  task_state_t state;
  recurso_t recurso_solicitado;
} sched_entry_t;
```

```c
void solicitar_handler(recurso_t recurso){
    // "No se devolverá el control a la tarea solicitante hasta que el recurso esté listo"

    int8_t task_id = current_task;
    sched_entry_t* tarea = &sched_tasks[task_id];               // obtengo la ficha de la tarea actual
 
    tarea.recurso_solicitado = recurso;     //guardo el recurso solicitado

    sched_disable_task(task_id);        // pongo en pausa la tarea
    
}
```
Ahora me voy a encargar de la syscall recurso_listo

```asm
     global _isr91  

     _isr91:
         pushad
         call recurso_listo_handler

          ;"saltar a la próxima tarea"         
         call sched_next_task
         mov word [sched_task_selector], ax
         jmp far [sched_task_offset]          ; Desalojamos la tarea
```

```c
void recurso_listo_handler(){

    int8_t task_id = current_task;
    sched_entry_t* tarea = &sched_tasks[task_id];                   // obtengo la ficha de la tarea actual(productora)
    task_id_t tarea_consumidora = para_quien_produce(task_id);      // obtengo la ficha de la tarea consumidora


    if (tarea_consumidora == 0)        // si la produccion se solicito mediante arranque manual no debo hacer la copia de la informacion
    {
        sched_enable_task(tarea_consumidora);        // como ya esta listo el recurso de la tarea consumidora, puede volver a ser ejectutada
        restaurar_tarea(task_id);                   // restauro la tarea
    }

    // if (!(mmu_is_page_mapped(rcr3, VIR_DIR_A))){       // si no esta mapeada limpio antes de acceder
    //     zero_page(VIR_DIR_A);
    // }

    // no hace falta hacer zero_page porque estoy pisando el contenido de la VIR_DIR_A a VIR_DIR_B 

    // para hacer la copia de los 4kb de VIR_DIR_A a VIR_DIR_B necesito ambas direcciones fisicas.

    
    uint32_t cr3_consumidora = task_selector_to_CR3(tarea_consumidora->selector); // obtengo el cr3 de la tarea consumidora

    uint32_t PHY_DIR_A = virt_to_phy(rcr3(),VIR_DIR_A);
    uint32_t PHY_DIR_B = virt_to_phy(cr3_consumidora,VIR_DIR_B);

    //una vez que tengo las direcciones fisicas puedo hacer la copia usando copy_page

    copy_page(PHY_DIR_B, PHY_DIR_A);              // copio los 4kb
    
    sched_enable_task(tarea_consumidora);        // como ya esta listo el recurso de la tarea consumidora, puede volver a ser ejectutada

    restaurar_tarea(task_id);                   // restauro la tarea
}   
```

```c
// Fuciones auxiliares

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
```

## Ejercicio 2 (25 puntos):

```c
// Dado el id de una tarea tengo que restaurarlo, es decir, restaurar los valores de la tss.
 void restaurar_tarea(task_id_t id_tarea){

    uint32_t cr3 = task_selector_to_CR3(id_tarea->selector); // obtengo el cr3 de la tarea 

    uint32_t PHY_DIR_B = virt_to_phy(cr3,VIR_DIR_B);    // obtengo la direccion fisica 

    tss_t tss = tss_create_user_task(PHY_DIR_B);        // creo la tss

    tss_set(tss, id_tarea);         // seteo la tss

 }
```


## Ejercicio 3 (15 puntos):
Detallar todas las modificaciones necesarias para que el kernel provea el arranque manual mediante interrupción externa.
Se pueden asumir implementadas las funciones dadas en el ejercicio 1.

Primero debería agregar la entrada para esta interrupcion en la idt, en el archivo isr.asm (nivel 0).

```c
    IDT_ENTRY0(41); // <-- Syscall para recurso
```

Luego tengo que hacer la rutina de interrupción, pasandole como parametro lo que hay en la direccion 0xFAFAFA.
Como el enunciado dice "bajo la misma lógica que solicitar" puedo llamar al handler de solicitar (solicitar_handler).

```asm
 global _isr41 
 _isr41:

     pushad
     call pic_finish1                   ; Le decimos al PIC que vamos a atender la interrupción
     mov ebx, 0xFAFAFA                    ; Paso 'recurso_t' (recurso solicitado) por ebx    
     call solicitar_handler

      ;"saltar a la próxima tarea"          Porque dice que no se devuelve el control hasta que este listo
     call sched_next_task
     mov word [sched_task_selector], ax
     jmp far [sched_task_offset]          ; Desalojamos la tarea
```


## Ejercicio 4 (10 puntos):
¿Cómo modificarían el sistema para almacenar qué recurso produce cada tarea?
Para esto agregaria en el struct de sched.c "recurso_t recurso_producido"

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


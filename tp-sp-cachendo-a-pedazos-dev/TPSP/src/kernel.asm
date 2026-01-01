; ** por compatibilidad se omiten tildes **
; ==============================================================================
; TALLER System Programming - Arquitectura y Organizacion de Computadoras - FCEN
; ==============================================================================

%include "print.mac"


; Definiciones locales 
%define C_FG_WHITE 0xF
%define C_BG_BLUE  (1 << 4)

global start


; COMPLETAR - Agreguen declaraciones extern según vayan necesitando
extern GDT_DESC
extern IDT_DESC
extern screen_draw_layout
extern idt_init
extern pic_reset
extern pic_enable
extern mmu_init_kernel_dir
extern mmu_init_task_dir
extern tss_init
extern tasks_screen_draw
extern sched_init 
extern tasks_init

; COMPLETAR - Definan correctamente estas constantes cuando las necesiten
; Selectores segun indices de la GDT (code_0=1, data_0=3)
%define CS_RING_0_SEL (1 * 8)
%define DS_RING_0_SEL (3 * 8)

%define TAREA_INICIAL (11 * 8)
%define TAREA_IDLE (12 * 8)

BITS 16
;; Saltear seccion de datos
jmp start

;;
;; Seccion de datos.
;; -------------------------------------------------------------------------- ;;
start_rm_msg db     'Iniciando kernel en Modo Real'
start_rm_len equ    $ - start_rm_msg

start_pm_msg db     'Iniciando kernel en Modo Protegido'
start_pm_len equ    $ - start_pm_msg

;;
;; Seccion de código.
;; -------------------------------------------------------------------------- ;;

;; Punto de entrada del kernel.
BITS 16
start:
    ; ==============================
    ; ||  Salto a modo protegido  ||
    ; ==============================

    ; COMPLETAR - Deshabilitar interrupciones (Parte 1: Pasake a modo protegido)
    cli

    ; Cambiar modo de video a 80 X 50
    mov ax, 0003h
    int 10h ; set mode 03h
    xor bx, bx
    mov ax, 1112h
    int 10h ; load 8x8 font

    ; COMPLETAR - Imprimir mensaje de bienvenida - MODO REAL (Parte 1: Pasake a modo protegido)
    ; (revisar las funciones definidas en print.mac y los mensajes se encuentran en la
    ; sección de datos)
    print_text_rm start_rm_msg, start_rm_len, C_FG_WHITE+C_BG_BLUE, 10, 10

    ; COMPLETAR - Habilitar A20 (Parte 1: Pasake a modo protegido)
    ; (revisar las funciones definidas en a20.asm)

    call A20_enable

    ; COMPLETAR - los defines para la GDT en defines.h y las entradas de la GDT en gdt.c
    ; COMPLETAR - Cargar la GDT (Parte 1: Pasake a modo protegido)

    lgdt [GDT_DESC]

    ; COMPLETAR - Setear el bit PE del registro CR0 (Parte 1: Pasake a modo protegido)

    mov eax, cr0
    or eax, 1
    mov cr0, eax


    ; COMPLETAR - Saltar a modo protegido (far jump) (Parte 1: Pasake a modo protegido)
    ; (recuerden que un far jmp se especifica como jmp CS_selector:address)
    ; Pueden usar la constante CS_RING_0_SEL definida en este archivo

    jmp CS_RING_0_SEL:modo_protegido

BITS 32
modo_protegido:
    ; COMPLETAR (Parte 1: Pasake a modo protegido) - A partir de aca, todo el codigo se va a ejectutar en modo protegido
    ; Establecer selectores de segmentos DS, ES, GS, FS y SS en el segmento de datos de nivel 0
    ; Pueden usar la constante DS_RING_0_SEL definida en este archivo

    mov di, DS_RING_0_SEL
    mov ds, di
    mov es, di
    mov gs, di
    mov fs, di
    mov ss, di
    
    ; COMPLETAR - Establecer el tope y la base de la pila (Parte 1: Pasake a modo protegido)

    mov ebp, 0x25000
    mov esp, 0x25000

    ; COMPLETAR - Imprimir mensaje de bienvenida - MODO PROTEGIDO (Parte 1: Pasake a modo protegido)

    print_text_pm start_pm_msg, start_pm_len, C_FG_WHITE+C_BG_BLUE, 10, 10

    ; COMPLETAR - Inicializar pantalla (Parte 1: Pasake a modo protegido)
    
    call screen_draw_layout
    
    ; ===================================
    ; ||     (Parte 3: Paginación)     ||
    ; ===================================

    ; COMPLETAR - los defines para la MMU en defines.h
    ; COMPLETAR - las funciones en mmu.c
    ; COMPLETAR - reemplazar la implementacion de la interrupcion 88 (ver comentarios en isr.asm)
    ; COMPLETAR - La rutina de atención del page fault en isr.asm

    ; COMPLETAR - Inicializar el directorio de paginas

    call mmu_init_kernel_dir

    ; COMPLETAR - Cargar directorio de paginas 

    mov cr3, eax

    ; COMPLETAR - Habilitar paginacion 

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax    

    ; ========================
    ; ||  (Parte 4: Tareas) ||
    ; ========================

    ; COMPLETAR - reemplazar la implementacion de la interrupcion 88 (ver comentarios en isr.asm)
    ; COMPLETAR - las funciones en tss.c
    ; COMPLETAR - Inicializar tss

    call tss_init 

    ; COMPLETAR - Inicializar el scheduler

    call sched_init

    ; COMPLETAR - Inicializar las tareas

    call tasks_screen_draw

    ; ===================================
    ; ||   (Parte 2: Interrupciones)   ||
    ; ===================================

    ; COMPLETAR - las funciones en idt.c

    ; COMPLETAR - Inicializar y cargar la IDT

    call idt_init
    lidt [IDT_DESC]    

    ; COMPLETAR - Reiniciar y habilitar el controlador de interrupciones (ver pic.c)

    call pic_reset      ; remapear PIC
    call pic_enable     ; habilitar PIC
    sti                 ; habilitar interrupciones

    ; El PIT (Programmable Interrupt Timer) corre a 1193182Hz.

    ; Cada iteracion del clock decrementa un contador interno, cuando
    ; éste llega

    ; a cero se emite la interrupción. El valor inicial es 0x0 que
    ; indica 65536,

    ; es decir 18.206 Hz

    ; mov ax, DIVISOR

    ; out 0x40, al

    ; rol ax, 8

    ; out 0x40, al    

    ; COMPLETAR - Rutinas de atención de reloj, teclado, e interrupciones 88 y 89 (en isr.asm)

    ; COMPLETAR (Parte 4: Tareas)- Cargar tarea inicial

    mov ax, TAREA_INICIAL 
    ltr ax

    ; COMPLETAR - Habilitar interrupciones (!! en etapas posteriores, evaluar si se debe comentar este código !!)
    
    ; NOTA: Pueden chequear que las interrupciones funcionen forzando a que se
    ;       dispare alguna excepción (lo más sencillo es usar la instrucción
    ;       `int3`)
    ;int3

    ; COMPLETAR - Probar Sys_call (para etapas posteriores, comentar este código)

    ; COMPLETAR - Probar generar una excepción (para etapas posteriores, comentar este código)
    
    ; ========================
    ; ||  (Parte 4: Tareas)  ||
    ; ========================
    
    ; COMPLETAR - Inicializar el directorio de paginas de la tarea de prueba

    call tasks_init

    ; COMPLETAR - Cargar directorio de paginas de la tarea

    ; COMPLETAR - Restaurar directorio de paginas del kernel

    ; COMPLETAR - Saltar a la primera tarea: Idle

    jmp TAREA_IDLE:0

    ; Escritura de prueba de page fault

    ; push 0x18000
    ; call mmu_init_task_dir
    ; mov cr3, eax
    ; mov word[0x07000010], 2
    ; mov [0x07000002], ecx

    ; Ciclar infinitamente 
    mov eax, 0xFFFF
    mov ebx, 0xFFFF
    mov ecx, 0xFFFF
    mov edx, 0xFFFF
    jmp $
;; -------------------------------------------------------------------------- ;;

%include "a20.asm"

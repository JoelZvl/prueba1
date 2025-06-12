#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>
#include <unistd.h>
#include <time.h>
#include <math.h> // For ceil

#ifndef LISTA_H
#define LISTA_H

#define MAXQUANTUM 5
#define HISTORIAL_SIZE 10
#define Max_Programas 100 // Maximo de programas diferentes
#define MAX_USUARIOS 10

// SWAP and Memory Management Defines
#define INSTRUCTION_SIZE_CHARS 32
#define PAGE_SIZE_INSTRUCTIONS 16
#define SWAP_SIZE_INSTRUCTIONS 65536                                       // 2^16
#define SWAP_SIZE_FRAMES (SWAP_SIZE_INSTRUCTIONS / PAGE_SIZE_INSTRUCTIONS) // 4096 frames
#define SWAP_FILE_NAME "SWAP.bin"
#define TMS_FREE_FRAME 0 // PID 0 indicates a free frame in TMS

// variables globales
char programas_cargados[Max_Programas][100]; // almacena nombres de archivos
int total_programas = 0;
int IncCPU = 60 / MAXQUANTUM; // Quantum por proceso
int PBase = 60;               // Prioridad base
int NumUs = 0;                // Número de usuarios
float W = 0.0;                // Peso de usuarios
int Users[MAX_USUARIOS];      // Arreglo de IDs de usuarios
int DELAY = 5000000;
int CMD_DELAY = 10000; // 50ms para comandos (más responsivo)

int swap_display_start_frame = 0;          // Frame inicial para mostrar
#define SWAP_DISPLAY_COLUMNS 6             // Columnas visibles en pantalla
#define SWAP_DISPLAY_ROWS 16               // Filas por columna (una por instrucción por frame)
#define SWAP_CONTENT_DISPLAY_ROWS 16       // Coincide con PAGE_SIZE_INSTRUCTIONS
#define SWAP_CONTENT_DISPLAY_COLS 6        // Número de columnas visibles
#define SWAP_CONTENT_INSTR_TRUNCATE_LEN 12 // Longitud máxima de instrucción mostrada

int tms_display_start = 0;    // Posición inicial de visualización del TMS
#define TMS_DISPLAY_ENTRIES 6 // Número de entradas visibles por página

// PCB Structure Modification
typedef struct PCB
{
    int PID;
    char fileName[100];
    FILE *program; // Will be NULL after loading to SWAP for non-siblings
    int AX, BX, CX, DX;
    int PC;       // Virtual Program Counter
    char IR[100]; // Instruction Register (holds 32 chars from SWAP + null terminator)
    struct PCB *sig;
    int UID;    // Identificador de usuario
    int P;      // Prioridad del proceso
    int KCPU;   // Contador de uso de CPU por proceso
    int KCPUxU; // Contador de uso de CPU por usuario

    // SWAP related fields
    int *TMP;                  // Tabla de Mapa de Páginas del Proceso (array of frame numbers in SWAP)
    int TmpSize;               // Tamaño de la TMP (cantidad de marcos/páginas del proceso)
    char real_address_str[40]; // For displaying "MarcoReal(Hex):Offset(Hex) | DRS(Hex)"

} PCB;

// Listas globales
PCB *Ejecucion = NULL;
PCB *Listos = NULL;
PCB *Terminados = NULL;
PCB *Nuevos = NULL; // New list for processes waiting for SWAP space

// SWAP global variables
FILE *swap_file_ptr = NULL;
int tms[SWAP_SIZE_FRAMES]; // Table of Map Swap; stores PID or TMS_FREE_FRAME

// prototipos nuevos
void actualizarContadorProgramas(const char *nombre_archivo);
int esProgramaNuevo(const char *nombre_archivo);
void mostrarContadorProgramas();

// Prototipos de funciones
void listaInsertarFinal(PCB **lista, PCB *nuevo);
PCB *listaExtraeInicio(PCB **lista);
PCB *listaExtraePID(PCB **lista, int pid);
void cargarProceso(char *fileName, int uid);
void matarProceso(int pid);
void ejecutarInstruccion(PCB *pcb);
void imprimirListas();
void manejarLineaComandos(char *comando, int *comandoIndex, char historial[HISTORIAL_SIZE][200], int *histIndex, int *histCursor);
int isNumeric(char *str);
void strUpper(char *str);
void actualizarPesoUsuarios();
int encontrarMenorPrioridad();
PCB *extraerPorPrioridad(int prioridad);

// SWAP related function prototypes
void initialize_swap_system();
void shutdown_swap_system();
int count_lines_in_file(const char *filename);
void handle_process_termination(PCB *pcb_to_terminate);
void check_nuevos_list_and_load_if_space();
void display_swap_info_minimal(int start_frame_tms, int num_frames_tms, int start_frame_swap, int num_instr_swap);

#endif
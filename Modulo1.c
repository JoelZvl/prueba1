#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <ncurses.h>
#include "nc_kbh.h"

#define DELAY 1000000  // 100ms de delay
#define HISTORIAL_SIZE 2  // Número de comandos guardados en el historial

// Estructura PCB
typedef struct {
    int AX;
    int BX;
    int CX;
    int DX;
    int PC;
    char IR[100];  // Instrucción registrada
} PCB;

void strUpper(char *str);
int isNumeric(char *str);
void manejarLineaComandos(char *comando, int *comandoIndex, FILE **archivo, int *leyendoArchivo, char historial[HISTORIAL_SIZE][200], int *histIndex,int *histCursor,PCB *pcb);
void evaluarComando(char *comando, FILE **archivo, int *leyendoArchivo, char historial[HISTORIAL_SIZE][200], int *histIndex, PCB *pcb);
int leerLineaArchivo(FILE *archivo,PCB *pcb, char *comando, int *comandoIndex);

int main() {
    initscr();
    keypad(stdscr,true);
    char comando[200] = "";  // Buffer para el comando
    int comandoIndex = 0;
    FILE *archivo = NULL;
    int leyendoArchivo = 0;
    PCB pcb={0,0,0,0,0,""};
    int histCursor =-1;
    nodelay(stdscr, TRUE);

    // Historial de comandos
    char historial[HISTORIAL_SIZE][200] = {""};
    int histIndex = 0;

    
    mvprintw(6, 1, "-------------------------------- PROCESADOR --------------------------------");
    mvprintw(7, 1, "- AX:[0]");
    mvprintw(8, 1, "- BX:[0]");
    mvprintw(9, 1, "- CX:[0]");
    mvprintw(10, 1, "- DX:[0]");
    mvprintw(7, 45, "PC:[0]");
    mvprintw(8, 45, "IR:[0]");
    mvprintw(11,1,"------------------------------------------------------------------------------");
    mvprintw(12, 1, "-------------------------------- MENSAJES --------------------------------");    
    mvprintw(18,1,"---------------------------------------------------------------------------");
    mvprintw(1,1,"#>");  // Imprimir el prompt antes de entrar al bucle
    refresh();


    // Variables para controlar el tiempo de ejecución del archivo
    time_t ultimaEjecucion = 0;
    const int INTERVALO_ARCHIVO = 1; // 1 segundo

    do {
        // --- Parte 1: Verificar comandos del usuario ---
        int tecla = getch();
        if (tecla != ERR) {  // Si hay una tecla disponible
            ungetch(tecla);  // Devolver la tecla para que manejarLineaComandos la procese
            manejarLineaComandos(comando, &comandoIndex, &archivo, &leyendoArchivo, historial, &histIndex, &histCursor, &pcb);
        }

        // --- Parte 2: Leer archivo automáticamente cada segundo ---
        if (leyendoArchivo && archivo) {
            time_t tiempoActual = time(NULL);
            
            // Si es la primera vez o ha pasado 1 segundo
            if (ultimaEjecucion == 0 || difftime(tiempoActual, ultimaEjecucion) >= INTERVALO_ARCHIVO) {
                int terminado = leerLineaArchivo(archivo, &pcb, comando, &comandoIndex);
                if (terminado) {
                    fclose(archivo);
                    archivo = NULL;
                    leyendoArchivo = 0;
                    ultimaEjecucion = 0; // Reset para el próximo archivo
                } else {
                    ultimaEjecucion = tiempoActual;
                }
            }
        }
        
        // Pausa pequeña para no saturar la CPU
        napms(50); // 50ms
        
    } while (1);
    endwin();
    return 0;
}

void manejarLineaComandos(char *comando, int *comandoIndex, FILE **archivo, int *leyendoArchivo, char historial[HISTORIAL_SIZE][200], int *histIndex, int *histCursor,PCB *pcb) {
    int tecla = getch();  

    if (tecla == '\n') {  
        comando[*comandoIndex] = '\0';  

        //evitar guardar comandos vacios en el historial
        if(strlen(comando)>0){
            for (int i = 0; comando[i]; i++) {
                comando[i] = toupper((unsigned char)comando[i]);
            }
        }

        // Mueve los comandos en el historial
        for (int i = HISTORIAL_SIZE - 1; i > 0; i--) {
            strcpy(historial[i], historial[i - 1]);
        }
        strcpy(historial[0], comando);  
        *histIndex = (*histIndex + 1) % HISTORIAL_SIZE;
        *histCursor = -1;  

        // Limpiar historial antes de imprimirlo
        for (int i = 0; i < HISTORIAL_SIZE; i++) {
            move(i + 2, 1);
            clrtoeol();
            mvprintw(i + 2, 1, "#> %s", historial[i]);  
        }

        move(14, 1);
        clrtoeol();
        mvprintw(14, 1, "Comando: %s", comando);
        refresh();
        usleep(DELAY);

        evaluarComando(comando, archivo, leyendoArchivo, historial, histIndex,pcb);

        *comandoIndex = 0;
        comando[0] = '\0';

        move(1, 1);
        clrtoeol();
        mvprintw(1, 1, "#> ");
        refresh();
    } 
    else if (tecla == 127 || tecla == 8 || tecla == KEY_BACKSPACE) {  
        if (*comandoIndex > 0) {
            (*comandoIndex)--;
            comando[*comandoIndex] = '\0';
        }
    }
    
    else if (tecla == KEY_UP) {  
        if (*histCursor < HISTORIAL_SIZE - 1 && strlen(historial[*histCursor + 1]) > 0) {
            (*histCursor)++;
            strcpy(comando, historial[*histCursor]);
            *comandoIndex = strlen(comando);
        }
    } 
    else if (tecla == KEY_DOWN) {  
        if (*histCursor > 0) {
            (*histCursor)--;
            strcpy(comando, historial[*histCursor]);
            *comandoIndex = strlen(comando);
        } else {
            *histCursor = -1;
            comando[0] = '\0';
            *comandoIndex = 0;
        }
    } 
    else {  
        comando[(*comandoIndex)++] = tecla;
        comando[*comandoIndex] = '\0';
    }

    move(1, 1);
    clrtoeol();
    mvprintw(1, 1, "#> %s", comando);
    refresh();
    fflush(stdout);
}

// Función para reiniciar el PCB
void reiniciarPCB(PCB *pcb) {
    pcb->AX = 0;
    pcb->BX = 0;
    pcb->CX = 0;
    pcb->DX = 0;
    pcb->PC = 0;
    strcpy(pcb->IR, "");
    
    // Actualizar la pantalla para mostrar los valores reiniciados
    mvprintw(7, 1, "- AX:[0]     ");
    mvprintw(8, 1, "- BX:[0]     ");
    mvprintw(9, 1, "- CX:[0]     ");
    mvprintw(10, 1,"- DX:[0]     ");
    mvprintw(7, 45, "PC:[0]     ");
    mvprintw(8, 45, "IR:[         ]");
    
    // Limpiar mensajes anteriores
    move(14, 1);
    clrtoeol();
    move(15, 1);
    clrtoeol();
    move(16, 1);
    clrtoeol();
    
    refresh();
}

void evaluarComando(char *comando, FILE **archivo, int *leyendoArchivo, char historial[HISTORIAL_SIZE][200], int *histIndex, PCB *pcb) {
    char operacion[20], parametro[180] = "";
    if (sscanf(comando, "%s %s", operacion, parametro) >= 1) {
        
        if (strcmp(operacion, "LOAD") == 0 || strcmp(operacion, "CARGAR") == 0) {
            if (strlen(parametro) == 0) {
                move(16, 1);
                clrtoeol();
                mvprintw(14,1,"Uso: LOAD <nombre_archivo>\n");
                refresh();
                return;
            }

            // Si ya hay un archivo en ejecución, cerrarlo primero
            if (*archivo && *leyendoArchivo) {
                fclose(*archivo);
                *archivo = NULL;
                *leyendoArchivo = 0;
                move(15, 1);
                clrtoeol();
                mvprintw(15,1,"Archivo anterior cerrado. Cargando nuevo archivo...");
                refresh();
                usleep(500000); // Pausa de medio segundo para mostrar el mensaje
            }

            // Reiniciar el PCB antes de cargar el nuevo archivo
            reiniciarPCB(pcb);

            *archivo = fopen(parametro, "r");
            if (!*archivo) {
                move(16, 1);
                clrtoeol();
                mvprintw(14,1,"Error: no se pudo abrir el archivo: %s.\n", parametro);
                refresh();
                return;
            }
            *leyendoArchivo = 1;
            move(16, 1);
            clrtoeol();
            mvprintw(14,1,"Leyendo archivo [ %s ]",parametro);
                refresh();
        } 
        else if (strcmp(operacion, "EXIT") == 0 || strcmp(operacion, "SALIR") == 0) {
            move(16, 1);
            clrtoeol();
            mvprintw(14, 17, "Estas seguro que deseas abandonar la ejecucion (Y/n): ");
            refresh();
            int respuesta = getch();
            if (respuesta == 'Y' || respuesta == 'y') {
                endwin();
                exit(0);
            }
        }  
        else {
            move(16, 1);
            clrtoeol();
            mvprintw(14,15,"Comando incorrecto...");
            refresh();
        }
    }
}
int leerLineaArchivo(FILE *archivo, PCB *pcb,char *comando,int *comandoIndex) {
    char linea[256], instruccion[20], p1[20], p2[20];
    
    if (fgets(linea, sizeof(linea), archivo)) {
        strUpper(linea);
        linea[strcspn(linea, "\n")] = '\0';
        sscanf(linea, "%s %s %s", instruccion, p1, p2);
        strcpy(pcb->IR,linea);
        pcb->PC++;

        mvprintw(6, 1, "-------------------------------- PROCESADOR --------------------------------");
        mvprintw(7, 1, "- AX:[%d]", pcb->AX);
        mvprintw(8, 1, "- BX:[%d]", pcb->BX);
        mvprintw(9, 1, "- CX:[%d]", pcb->CX);
        mvprintw(10, 1,"- DX:[%d]", pcb->DX);
        mvprintw(7, 45, "PC:[%-5d]", pcb->PC - 1);  // -1 para que empiece en desde 0 y no desde 1
        mvprintw(8, 45, "IR:[%-20s]", pcb->IR); 
        mvprintw(11, 1, "------------------------------------------------------------------------------");
        refresh();
        //usleep(1000000);
        


        // Procesamiento de instrucciones
        if (strcmp(instruccion, "MOV") == 0) {
            if (strcmp(p1, "AX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0)
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                    ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb->AX = atoi(p2);}
                else if (strcmp(p2,"BX")==0){pcb ->AX = pcb->AX;}    
                else if (strcmp(p2,"BX")==0){pcb ->AX = pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->AX = pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->AX = pcb->DX;}
            }
            else if (strcmp(p1, "BX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                    ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb -> BX = atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->BX = pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->BX = pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->BX = pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->BX = pcb->DX;}
                
            }
            else if (strcmp(p1, "CX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                {mvprintw(14,1,"IR[%s] no es valido ",linea);refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->CX = atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->CX = pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->CX = pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->CX = pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->CX = pcb->DX;}
            }
            else if (strcmp(p1, "DX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}    
                else if(isNumeric(p2)){pcb ->DX = atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->DX = pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->DX = pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->DX = pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->DX = pcb->DX;}
            }
            else {
                mvprintw(14,1,"Error de Registro IR[%s]",pcb->IR); 
                refresh();
                move(1,1);  
                return 1;
                }
        } 
        else if (strcmp(instruccion, "ADD") == 0) {
            if (strcmp(p1, "AX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->AX += atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->AX += pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->AX += pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->AX += pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->AX += pcb->DX;}
            }
            else if (strcmp(p1, "BX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->BX += atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->BX += pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->BX += pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->BX += pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->BX += pcb->DX;}
            }
            else if (strcmp(p1, "CX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->CX += atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->CX += pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->CX += pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->CX += pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->CX += pcb->DX;}
            }
            else if (strcmp(p1, "DX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->DX += atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->DX += pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->DX += pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->DX += pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->DX += pcb->DX;}
            }
            else {
                mvprintw(14,1,"Error de Registro IR[%s]",pcb->IR); 
                refresh();
                move(1,1);  
                return 1;
                }
        } 
        else if (strcmp(instruccion, "SUB") == 0) {
            if (strcmp(p1, "AX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->AX -= atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->AX -= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->AX -= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->AX -= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->AX -= pcb->DX;}
            }
            else if (strcmp(p1, "BX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0; move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->BX -= atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->BX -= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->BX -= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->BX -= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->BX -= pcb->DX;}
            }
            else if (strcmp(p1, "CX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->CX -= atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->CX -= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->CX -= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->CX -= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->CX -= pcb->DX;}
            }
            else if (strcmp(p1, "DX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->DX -= atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->DX -= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->DX -= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->DX -= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->DX -= pcb->DX;}
            }
                else {
                mvprintw(14,1,"Error de Registro IR[%s]",pcb->IR); 
                refresh();
                move(1,1);
                pcb -> PC =0;  
                return 1;
                }
        } 
        else if (strcmp(instruccion, "MUL") == 0) {
            if (strcmp(p1, "AX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->AX *= atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->AX *= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->AX *= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->AX *= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->AX *= pcb->DX;}
            }
            else if (strcmp(p1, "BX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->BX *= atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->BX *= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->BX *= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->BX *= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->BX *= pcb->DX;}
            }
            else if (strcmp(p1, "CX") == 0){                
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0; move(1,1);return 1;}
                else if(isNumeric(p2)){pcb ->CX *= atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->CX *= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->CX *= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->CX *= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->CX *= pcb->DX;}
            }
            else if (strcmp(p1, "DX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){pcb ->DX *= atoi(p2);}
                else if (strcmp(p2,"AX")==0){pcb ->DX *= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->DX *= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->DX *= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->DX *= pcb->DX;}
            }
            else {
                mvprintw(14,1,"Error de Registro IR[%s]",pcb->IR); 
                refresh();
                move(1,1); 
                pcb -> PC =0; 
                return 1;
            }
        } 
        else if (strcmp(instruccion, "DIV") == 0) {
            if (strcmp(p1, "AX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){
                    if(atoi(p2)==0){
                        mvprintw(16,1,"divicion sobre 0");
                        

                    }else{pcb ->DX /= atoi(p2);}
                }
                else if (strcmp(p2,"AX")==0){pcb ->AX /= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->AX /= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->AX /= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->AX /= pcb->DX;}
            }
            if (strcmp(p1, "BX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){
                    if(atoi(p2)==0){
                        mvprintw(16,1,"divicion sobre 0");
                        return 1;

                    }else{pcb ->DX /= atoi(p2);}
                }
                else if (strcmp(p2,"AX")==0){pcb ->BX /= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->BX /= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->BX /= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->BX /= pcb->DX;}
            }
            if (strcmp(p1, "CX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0; move(1,1);return 1;}
                else if(isNumeric(p2)){
                    if(atoi(p2)==0){
                        mvprintw(16,1,"divicion sobre 0");

                    }else{pcb ->DX /= atoi(p2);}
                }
                else if (strcmp(p2,"AX")==0){pcb ->CX /= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->CX /= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->CX /= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->CX /= pcb->DX;}
            }
            if (strcmp(p1, "DX") == 0){
                if(!isNumeric(p2) && strcmp(p2,"AX")!=0 && strcmp(p2,"BX")!=0 && strcmp(p2,"CX")!=0&& strcmp(p2,"DX")!=0) 
                    {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
                else if(isNumeric(p2)){
                    if(atoi(p2)==0){
                        mvprintw(14,1,"No se puede ejecutar divicion sobre 0");

                    }else{pcb ->DX /= atoi(p2);}
                }
                else if (strcmp(p2,"AX")==0){pcb ->DX /= pcb->AX;}
                else if (strcmp(p2,"BX")==0){pcb ->DX /= pcb->BX;}
                else if (strcmp(p2,"CX")==0){pcb ->DX /= pcb->CX;}
                else if (strcmp(p2,"DX")==0){pcb ->DX /= pcb->DX;}
            }
            else {
                mvprintw(14,1,"Error de Registro IR[%s]",pcb->IR); 
                refresh();
                move(1,1);
                pcb -> PC =0;  
                return 1;
                }
        } 
        else if (strcmp(instruccion, "INC") == 0) {
            if(strcmp(p1,"AX")!=0 && strcmp(p1,"BX")!=0 && strcmp(p1,"CX")!=0 && strcmp(p1,"DX")!=0) 
                {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0;move(1,1); return 1;}
            else if (strcmp(p1, "AX") == 0) pcb->AX++;
            else if (strcmp(p1, "BX") == 0) pcb->BX++;
            else if (strcmp(p1, "CX") == 0) pcb->CX++;
            else if (strcmp(p1, "DX") == 0) pcb->DX++;
        } 
        else if (strcmp(instruccion, "DEC") == 0) {
            if(strcmp(p1,"AX")!=0 && strcmp(p1,"BX")!=0 && strcmp(p1,"CX")!=0&& strcmp(p1,"DX")!=0) 
                {mvprintw(14,1,"IR[%s] no es valido ",linea);mvprintw(14,30,"                                ");refresh();pcb -> PC =0; move(1,1);return 1;}
            else if (strcmp(p1, "AX") == 0) pcb->AX--;
            else if (strcmp(p1, "BX") == 0) pcb->BX--;
            else if (strcmp(p1, "CX") == 0) pcb->CX--;
            else if (strcmp(p1, "DX") == 0) pcb->DX--;

        } 
        else if (strcmp(instruccion, "END") == 0) {mvprintw(14,1,"Se Encontro fin del Archivo");mvprintw(16,1,"Ejecucion terminada");refresh();move(1,1);
            pcb -> PC =0;
            return 1;
        } 
        else {mvprintw(15, 30, "Error: Instrucción inválida -> %s", linea);mvprintw(14,30,"                                ");
            refresh();
            pcb -> PC =0;
            move(1,1);
            return 1;
        }
        
        refresh();
        int tecla;
        time_t inicio = time(NULL);
        int pausa_ms = 10;  // 1 segundo de pausa base
        
        
        
        return 0;  // Continuar con siguiente línea
    }else{
                mvprintw(14,1,"Fin del archivo");
        refresh();
        return 1;  // Terminar lectura
    }
    return 0;    
        
    }

    

void strUpper(char *str){
    for (int i = 0; str[i] !='\0'; i++) {
            str[i] = toupper(str[i]);
        }

}
// Función para verificar si una cadena es numérica
int isNumeric(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (!isdigit(str[i])) {
            return 0;  // No es un número
        }
    }
    return 1;  // Es un número
}

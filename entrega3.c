#include "nc_kbh.h"
#include "lista.h"
#include <errno.h>
#include <math.h> // For ceil if used, though not in this specific new display logic directly
#include <sys/time.h>
// --- New Defines for SWAP Content Display ---

#define TMS_DISPLAY_ENTRIES 6

// Helper function to count lines in a file
int count_lines_in_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        mvprintw(16, 1, "Error: No se pudo abrir el archivo %s para contar lineas.", filename);
        return -1;
    }
    int lines = 0;
    char buffer[256]; // Assuming lines are not excessively long
    while (fgets(buffer, sizeof(buffer), file))
    {
        lines++;
    }
    fclose(file);
    return lines;
}

void initialize_swap_system()
{
    // Initialize TMS (Table of Map Swap) - all frames free
    for (int i = 0; i < SWAP_SIZE_FRAMES; i++)
    {
        tms[i] = TMS_FREE_FRAME;
    }

    // Create or open SWAP file
    swap_file_ptr = fopen(SWAP_FILE_NAME, "wb+"); // Try opening existing
    if (swap_file_ptr == NULL)
    { // If not exists or error, create it
        swap_file_ptr = fopen(SWAP_FILE_NAME, "wb+");
        if (swap_file_ptr == NULL)
        {
            perror("Error creating SWAP file");
            endwin();
            exit(EXIT_FAILURE);
        }
        // Fill SWAP file with zeros (or spaces)
        char empty_instruction[INSTRUCTION_SIZE_CHARS];
        memset(empty_instruction, '0', INSTRUCTION_SIZE_CHARS); // Fill with '0'
        for (long i = 0; i < SWAP_SIZE_INSTRUCTIONS; i++)
        {
            if (fwrite(empty_instruction, INSTRUCTION_SIZE_CHARS, 1, swap_file_ptr) != 1)
            {
                perror("Error writing to initialize SWAP file");
                fclose(swap_file_ptr);
                endwin();
                exit(EXIT_FAILURE);
            }
        }
        fflush(swap_file_ptr);
        // Re-open in rb+ mode after creation and filling might be safer with some systems
        // but wb+ allows read and write. For simplicity, keep as is or rewind.
        rewind(swap_file_ptr); // Ensure pointer is at the beginning for subsequent rb+ operations
        mvprintw(15, 1, "SWAP file created and initialized.");
    }
    else
    {
        mvprintw(15, 1, "SWAP file opened.");
    }
    refresh();
    // Keep swap_file_ptr open
}

void shutdown_swap_system()
{
    if (swap_file_ptr != NULL)
    {
        fclose(swap_file_ptr);
        swap_file_ptr = NULL;
    }
}

void handle_process_termination(PCB *pcb)
{
    if (!pcb)
        return;

    // If the process had an open program file (e.g., was in Nuevos and never loaded)
    if (pcb->program)
    {
        fclose(pcb->program);
        pcb->program = NULL;
    }

    // Check if TMP is shared and if this is the last process using it
    int is_shared_and_others_exist = 0;
    if (pcb->TMP)
    {
        PCB *temp_list_check = Listos;
        while (temp_list_check)
        {
            if (temp_list_check != pcb && temp_list_check->TMP == pcb->TMP)
            {
                is_shared_and_others_exist = 1;
                break;
            }
            temp_list_check = temp_list_check->sig;
        }
        if (!is_shared_and_others_exist && Ejecucion && Ejecucion != pcb && Ejecucion->TMP == pcb->TMP)
        {
            is_shared_and_others_exist = 1;
        }
        // Could also check Nuevos if they can somehow share TMP before loading, though current logic assigns TMP on load.

        if (!is_shared_and_others_exist)
        { // This is the last process using this TMP
            for (int i = 0; i < pcb->TmpSize; i++)
            {
                int frame_in_swap = pcb->TMP[i];
                if (frame_in_swap >= 0 && frame_in_swap < SWAP_SIZE_FRAMES)
                {
                    tms[frame_in_swap] = TMS_FREE_FRAME; // Mark frame as free
                }
            }
            free(pcb->TMP);
            pcb->TMP = NULL;
            pcb->TmpSize = 0;
        }
    }
    // Note: Actual PCB memory (pcb itself) is freed when Terminados list is cleared or managed,
    // this function just handles SWAP resources associated with it.
}

void check_nuevos_list_and_load_if_space()
{
    PCB *current_nuevo = Nuevos;
    PCB *prev_nuevo = NULL;

    while (current_nuevo)
    {
        int lines = count_lines_in_file(current_nuevo->fileName);
        if (lines <= 0)
        { // Error reading file or empty file
            mvprintw(16, 1, "Error or empty file %s for PID %d in Nuevos. Moving to Terminados.", current_nuevo->fileName, current_nuevo->PID);
            PCB *to_terminate = current_nuevo;
            if (prev_nuevo)
                prev_nuevo->sig = current_nuevo->sig;
            else
                Nuevos = current_nuevo->sig;
            current_nuevo = current_nuevo->sig;
            handle_process_termination(to_terminate); // Handle its resources if any were partially allocated
            listaInsertarFinal(&Terminados, to_terminate);
            continue;
        }

        int frames_needed = (int)ceil((double)lines / PAGE_SIZE_INSTRUCTIONS);
        current_nuevo->TmpSize = frames_needed; // Store for later

        // Count free frames in SWAP
        int free_frames_count = 0;
        for (int i = 0; i < SWAP_SIZE_FRAMES; i++)
        {
            if (tms[i] == TMS_FREE_FRAME)
            {
                free_frames_count++;
            }
        }

        if (free_frames_count >= frames_needed)
        {
            mvprintw(15, 1, "Space found for PID %d from Nuevos. Loading...", current_nuevo->PID);
            // Allocate TMP
            current_nuevo->TMP = (int *)malloc(frames_needed * sizeof(int));
            if (!current_nuevo->TMP)
            {
                mvprintw(16, 1, "Error: No se pudo reservar memoria para TMP de PID %d.", current_nuevo->PID);
                // Leave in Nuevos or move to Terminados if this is a persistent issue
                prev_nuevo = current_nuevo;
                current_nuevo = current_nuevo->sig;
                continue;
            }

            FILE *prog_file = fopen(current_nuevo->fileName, "r");
            if (!prog_file)
            {
                mvprintw(16, 1, "Error: No se pudo abrir %s para PID %d. Removing from Nuevos.", current_nuevo->fileName, current_nuevo->PID);
                free(current_nuevo->TMP);
                current_nuevo->TMP = NULL;
                PCB *to_terminate = current_nuevo;
                if (prev_nuevo)
                    prev_nuevo->sig = current_nuevo->sig;
                else
                    Nuevos = current_nuevo->sig;
                current_nuevo = current_nuevo->sig;
                handle_process_termination(to_terminate);
                listaInsertarFinal(&Terminados, to_terminate);
                continue;
            }

            // Load to SWAP
            char instruction_buffer[INSTRUCTION_SIZE_CHARS + 1]; // +1 for null terminator
            char line_buffer[256];
            int frames_allocated_count = 0;
            int current_instruction_in_page = 0;
            int current_page_in_process = 0;
            long swap_write_pos = -1;

            for (int i = 0; i < frames_needed; i++)
            { // For each page of the process
                int found_frame_for_page = -1;
                for (int j = 0; j < SWAP_SIZE_FRAMES; j++)
                { // Find a free frame in TMS
                    if (tms[j] == TMS_FREE_FRAME)
                    {
                        tms[j] = current_nuevo->PID; // Allocate frame in TMS
                        current_nuevo->TMP[i] = j;   // Store SWAP frame number in process's TMP
                        found_frame_for_page = j;
                        break;
                    }
                }
                if (found_frame_for_page == -1)
                { /* Should not happen due to prior check, but as safeguard */
                    mvprintw(16, 1, "Critical Error: No free frame found during loading PID %d despite check!", current_nuevo->PID);
                    // Rollback allocated frames for this process
                    for (int k = 0; k < i; k++)
                        tms[current_nuevo->TMP[k]] = TMS_FREE_FRAME;
                    free(current_nuevo->TMP);
                    current_nuevo->TMP = NULL;
                    fclose(prog_file);
                    // Leave in Nuevos or move to Terminados. For now, just break this load attempt.
                    goto next_nuevo_process; // Ugly, but helps escape nested loops for this error
                }

                // Copy instructions for this page to the allocated SWAP frame
                for (int k = 0; k < PAGE_SIZE_INSTRUCTIONS; k++)
                {
                    if (fgets(line_buffer, sizeof(line_buffer), prog_file))
                    {
                        line_buffer[strcspn(line_buffer, "\r\n")] = 0; // Remove newline

                        memset(instruction_buffer, ' ', INSTRUCTION_SIZE_CHARS); // Pad with spaces
                        strncpy(instruction_buffer, line_buffer, INSTRUCTION_SIZE_CHARS);
                        // instruction_buffer[INSTRUCTION_SIZE_CHARS] = '\0'; // Not strictly needed for fwrite fixed size

                        swap_write_pos = (long)found_frame_for_page * PAGE_SIZE_INSTRUCTIONS * INSTRUCTION_SIZE_CHARS +
                                         (long)k * INSTRUCTION_SIZE_CHARS;

                        fseek(swap_file_ptr, swap_write_pos, SEEK_SET);
                        if (fwrite(instruction_buffer, INSTRUCTION_SIZE_CHARS, 1, swap_file_ptr) != 1)
                        {
                            mvprintw(16, 1, "Error escribiendo a SWAP para PID %d!", current_nuevo->PID);
                            // Handle error: potentially rollback, mark process for termination
                        }
                    }
                    else
                    {                                                             // End of program file, fill rest of page with NOPs/zeros if any
                        memset(instruction_buffer, '\0', INSTRUCTION_SIZE_CHARS); // Or some NOP representation
                        // strncpy(instruction_buffer, "END 0 0", INSTRUCTION_SIZE_CHARS); // Example NOP/END

                        swap_write_pos = (long)found_frame_for_page * PAGE_SIZE_INSTRUCTIONS * INSTRUCTION_SIZE_CHARS +
                                         (long)k * INSTRUCTION_SIZE_CHARS;
                        fseek(swap_file_ptr, swap_write_pos, SEEK_SET);
                        fwrite(instruction_buffer, INSTRUCTION_SIZE_CHARS, 1, swap_file_ptr);
                    }
                }
            }
            fclose(prog_file);
            current_nuevo->program = NULL; // Program is now in SWAP

            // Move from Nuevos to Listos
            PCB *to_listos = current_nuevo;
            if (prev_nuevo)
                prev_nuevo->sig = current_nuevo->sig;
            else
                Nuevos = current_nuevo->sig;
            current_nuevo = current_nuevo->sig; // Advance current_nuevo before modifying to_listos->sig

            listaInsertarFinal(&Listos, to_listos);
            mvprintw(15, 1, "Proceso PID %d movido de Nuevos a Listos.", to_listos->PID);
            actualizarPesoUsuarios(); // If it affects scheduling or user counts
            imprimirListas();         // Update display
            continue;                 // Try to load next process from Nuevos
        }
        else
        {
            mvprintw(15, 1, "No hay suficiente espacio en SWAP para PID %d (%d marcos necesarios, %d libres).", current_nuevo->PID, frames_needed, free_frames_count);
        }
    next_nuevo_process:;
        prev_nuevo = current_nuevo;
        current_nuevo = current_nuevo->sig;
    }
}

// Función principal
int main()
{
    initscr();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); // Hacer getch() no bloqueante
    srand(time(NULL));     // Inicializar semilla para PID aleatorios
    timeout(0);            // Non-blocking getch

    initialize_swap_system(); // Initialize SWAP file and TMS

    char comando[200] = ""; // Buffer para el comando
    int comandoIndex = 0;
    char historial[HISTORIAL_SIZE][200] = {""}; // Historial de comandos
    int histIndex = 0;
    int histCursor = -1;

    mvprintw(6, 1, "-------------------------------- PROCESADOR --------------------------------|");
    mvprintw(7, 1, "- AX:[--]");
    mvprintw(8, 1, "- BX:[--]");
    mvprintw(9, 1, "- CX:[--]");
    mvprintw(10, 1, "- DX:[--]");
    mvprintw(11, 1, "- P:[--]");
    mvprintw(12, 1, "- KCPU:[--]");
    mvprintw(7, 45, "PC (Virtual):[--]"); // Changed label
    mvprintw(8, 45, "IR:[--]                          |");
    mvprintw(9, 45, "PID:[--]                         |");
    mvprintw(10, 45, "NAME:[--]                        |");
    mvprintw(11, 45, "UID:[--]");
    mvprintw(12, 45, "KCPUxU:[--]");
    mvprintw(5, 45, "Real Addr (Swap):[--]"); // New display for real address
    mvprintw(13, 1, "----------------------------------------------------------------------------|");
    mvprintw(14, 1, "-------------------------------- MENSAJES ----------------------------------|");
    mvprintw(18, 1, "----------------------------------------------------------------------------|");
    mvprintw(1, 1, "#>"); // Prompt
    imprimirListas();
    refresh();

    int quantum_counter = 0; // Renamed for clarity

    struct timeval current_time_tv;                   // <--- CAMBIADO de timespec
    struct timeval last_exec_time_tv;                 // <--- CAMBIADO de timespec
    static struct timeval last_ui_update_tv = {0, 0}; // <--- CAMBIADO de timespec

    // Inicializar last_exec_time_tv antes del bucle
    gettimeofday(&last_exec_time_tv, NULL); // <--- AÑADIDO para inicialización

    while (1)
    {
        gettimeofday(&current_time_tv, NULL); // <--- CAMBIADO de clock_gettime

        if (!Ejecucion && Listos)
        {
            int menor_prioridad = encontrarMenorPrioridad();
            Ejecucion = extraerPorPrioridad(menor_prioridad);
            quantum_counter = 0;
            if (Ejecucion)
            {
                strcpy(Ejecucion->real_address_str, "--:-- | --");
            }
            // Actualizar last_exec_time_tv cuando un nuevo proceso empieza a ejecutar por primera vez en esta "ronda"
            // o se podría actualizar justo antes de la comprobación de DELAY.
            // Por simplicidad, se actualiza después de una ejecución o al final del ciclo de quantum.
        }

        if (Ejecucion) // Moví la condición de Ejecucion aquí para englobar el bloque
        {
            // Calcular tiempo transcurrido en microsegundos
            long long elapsed_microseconds =
                (current_time_tv.tv_sec - last_exec_time_tv.tv_sec) * 1000000LL +
                (current_time_tv.tv_usec - last_exec_time_tv.tv_usec);

            if (elapsed_microseconds >= DELAY) // <--- CAMBIADO: DELAY se asume en microsegundos
            {
                // Translate Virtual PC to Real SWAP Address
                if (Ejecucion->TMP == NULL || Ejecucion->TmpSize == 0)
                {
                    mvprintw(16, 1, "Error: PID %d no tiene TMP o TmpSize es 0. Terminando.", Ejecucion->PID);
                    PCB *to_terminate = Ejecucion;
                    Ejecucion = NULL;
                    handle_process_termination(to_terminate);
                    listaInsertarFinal(&Terminados, to_terminate);
                    check_nuevos_list_and_load_if_space();
                    imprimirListas();
                    gettimeofday(&last_exec_time_tv, NULL); // <--- Actualizar tiempo para el próximo ciclo
                    continue;
                }

                int virtual_page = Ejecucion->PC / PAGE_SIZE_INSTRUCTIONS;
                int offset_in_page = Ejecucion->PC % PAGE_SIZE_INSTRUCTIONS;

                if (virtual_page >= Ejecucion->TmpSize)
                {
                    mvprintw(16, 1, "Error: SegFault PID %d. PC %d fuera de rango (max page %d). Terminando.", Ejecucion->PID, Ejecucion->PC, Ejecucion->TmpSize - 1);
                    PCB *to_terminate = Ejecucion;
                    Ejecucion = NULL;
                    handle_process_termination(to_terminate);
                    listaInsertarFinal(&Terminados, to_terminate);
                    check_nuevos_list_and_load_if_space();
                    imprimirListas();
                    gettimeofday(&last_exec_time_tv, NULL); // <--- Actualizar tiempo
                    continue;
                }

                int frame_in_swap = Ejecucion->TMP[virtual_page];
                long drs_instruction_index = (long)frame_in_swap * PAGE_SIZE_INSTRUCTIONS + offset_in_page;
                long drs_byte_offset = drs_instruction_index * INSTRUCTION_SIZE_CHARS;

                sprintf(Ejecucion->real_address_str, "%X:%X | %lX", frame_in_swap, offset_in_page, drs_instruction_index);

                fseek(swap_file_ptr, drs_byte_offset, SEEK_SET);
                size_t bytes_read = fread(Ejecucion->IR, 1, INSTRUCTION_SIZE_CHARS, swap_file_ptr);

                if (bytes_read < INSTRUCTION_SIZE_CHARS || Ejecucion->IR[0] == '\0' || (isspace(Ejecucion->IR[0]) && Ejecucion->IR[1] == '\0'))
                {
                    mvprintw(15, 1, "Proceso PID %d (%s) finalizado (fin de instrucciones en SWAP en PC=%d).", Ejecucion->PID, Ejecucion->fileName, Ejecucion->PC);
                    PCB *to_terminate = Ejecucion;
                    Ejecucion = NULL;
                    handle_process_termination(to_terminate);
                    listaInsertarFinal(&Terminados, to_terminate);
                    actualizarPesoUsuarios();
                    check_nuevos_list_and_load_if_space();
                    imprimirListas();
                    gettimeofday(&last_exec_time_tv, NULL); // <--- Actualizar tiempo
                    continue;
                }
                Ejecucion->IR[INSTRUCTION_SIZE_CHARS] = '\0';

                char temp_ir_copy[100];
                strncpy(temp_ir_copy, Ejecucion->IR, 99);
                temp_ir_copy[99] = '\0';
                char instruccion_check[20];
                sscanf(temp_ir_copy, "%s", instruccion_check);
                strUpper(instruccion_check);

                if (strcmp(instruccion_check, "END") == 0)
                {
                    mvprintw(15, 1, "Proceso PID %d (%s) ejecuto END. Terminando.", Ejecucion->PID, Ejecucion->fileName);
                    ejecutarInstruccion(Ejecucion);
                    actualizarPesoUsuarios();
                    check_nuevos_list_and_load_if_space();
                    imprimirListas();
                    gettimeofday(&last_exec_time_tv, NULL); // <--- Actualizar tiempo
                    continue;
                }

                Ejecucion->KCPU += IncCPU;
                Ejecucion->KCPUxU += IncCPU;

                PCB *temp_listado = Listos;
                while (temp_listado)
                {
                    if (temp_listado->UID == Ejecucion->UID)
                    {
                        temp_listado->KCPUxU += IncCPU;
                    }
                    temp_listado = temp_listado->sig;
                }

                Ejecucion->PC++;
                quantum_counter++;
                ejecutarInstruccion(Ejecucion);

                if (!Ejecucion)
                {
                    actualizarPesoUsuarios();
                    check_nuevos_list_and_load_if_space();
                    imprimirListas();
                    gettimeofday(&last_exec_time_tv, NULL); // <--- Actualizar tiempo
                    continue;
                }
                imprimirListas();

                if (quantum_counter >= MAXQUANTUM && Ejecucion)
                {
                    PCB *temp_sched = Listos;
                    while (temp_sched)
                    {
                        temp_sched->KCPU /= 2;
                        temp_sched->KCPUxU /= 2;
                        if (W > 0.0001 || W < -0.0001)
                        {
                            temp_sched->P = PBase + temp_sched->KCPU / 2 + temp_sched->KCPUxU / (4 * W);
                        }
                        else
                        {
                            temp_sched->P = PBase + temp_sched->KCPU / 2;
                        }
                        temp_sched = temp_sched->sig;
                    }

                    Ejecucion->KCPU /= 2;
                    Ejecucion->KCPUxU /= 2;
                    if (W > 0.0001 || W < -0.0001)
                    {
                        Ejecucion->P = PBase + Ejecucion->KCPU / 2 + Ejecucion->KCPUxU / (4 * W);
                    }
                    else
                    {
                        Ejecucion->P = PBase + Ejecucion->KCPU / 2;
                    }

                    listaInsertarFinal(&Listos, Ejecucion);
                    Ejecucion = NULL;
                    actualizarPesoUsuarios();
                }
                // Actualizar tiempo de última ejecución DESPUÉS de procesar la instrucción
                gettimeofday(&last_exec_time_tv, NULL); // <--- CAMBIADO y MOVIDO aquí
            }
        } // end if(Ejecucion)

        if (kbhit())
        {
            manejarLineaComandos(comando, &comandoIndex, historial, &histIndex, &histCursor);
            usleep(CMD_DELAY); // CMD_DELAY en microsegundos
        }

        // 3. Actualización de UI controlada
        // Calcular tiempo transcurrido para UI en microsegundos
        long long ui_elapsed_microseconds =
            (current_time_tv.tv_sec - last_ui_update_tv.tv_sec) * 1000000LL +
            (current_time_tv.tv_usec - last_ui_update_tv.tv_usec);

        if (ui_elapsed_microseconds >= 100000) // 100 ms = 100,000 µs <--- CAMBIADO
        {
            imprimirListas();
            gettimeofday(&last_ui_update_tv, NULL); // <--- CAMBIADO
        }

        // 4. Pequeña pausa no bloqueante
        napms(1); // napms espera milisegundos
    }

    shutdown_swap_system(); // Close SWAP file
    endwin();
    return 0;
}

void actualizarPesoUsuarios()
{
    int usuarios_activos[MAX_USUARIOS] = {0}; // Stores UIDs
    int num_distinct_users = 0;

    // Helper to add user if unique
    void add_user_if_unique(int uid)
    {
        if (num_distinct_users >= MAX_USUARIOS)
            return;
        int found = 0;
        for (int i = 0; i < num_distinct_users; i++)
        {
            if (usuarios_activos[i] == uid)
            {
                found = 1;
                break;
            }
        }
        if (!found)
        {
            usuarios_activos[num_distinct_users++] = uid;
        }
    }

    PCB *temp = Listos;
    while (temp)
    {
        add_user_if_unique(temp->UID);
        temp = temp->sig;
    }

    if (Ejecucion)
    {
        add_user_if_unique(Ejecucion->UID);
    }

    // Consider users in Nuevos as well for W calculation?
    // Requirement implies active users (Listos, Ejecucion).
    // temp = Nuevos;
    // while(temp){
    //    add_user_if_unique(temp->UID);
    //    temp = temp->sig;
    // }

    NumUs = num_distinct_users;            // NumUs is the count of distinct active users
    W = (NumUs > 0) ? 1.0f / NumUs : 0.0f; // W is inverse of NumUs
}

int encontrarMenorPrioridad()
{
    int min_prioridad = PBase * 1000; // A sufficiently large initial value
    PCB *temp = Listos;
    if (!temp)
        return min_prioridad; // No processes in Listos

    min_prioridad = temp->P; // Initialize with the first process's priority
    temp = temp->sig;

    while (temp)
    {
        if (temp->P < min_prioridad)
        {
            min_prioridad = temp->P;
        }
        temp = temp->sig;
    }
    return min_prioridad;
}

PCB *extraerPorPrioridad(int prioridad)
{
    PCB *temp = Listos;
    PCB *prev = NULL;

    while (temp)
    {
        if (temp->P == prioridad)
        {
            if (prev)
            {
                prev->sig = temp->sig;
            }
            else
            {
                Listos = temp->sig;
            }
            temp->sig = NULL;
            return temp;
        }
        prev = temp;
        temp = temp->sig;
    }
    return NULL;
}

void listaInsertarFinal(PCB **lista, PCB *nuevo)
{
    if (!nuevo)
        return;
    nuevo->sig = NULL; // Ensure the new node's next is NULL

    if (!*lista)
    {
        *lista = nuevo;
    }
    else
    {
        PCB *temp = *lista;
        while (temp->sig)
        {
            temp = temp->sig;
        }
        temp->sig = nuevo;
    }
}

PCB *listaExtraeInicio(PCB **lista)
{
    if (!*lista)
    {
        return NULL;
    }
    PCB *temp = *lista;
    *lista = (*lista)->sig;
    temp->sig = NULL;
    return temp;
}

PCB *listaExtraePID(PCB **lista, int pid)
{
    PCB *temp = *lista;
    PCB *prev = NULL;

    while (temp)
    {
        if (temp->PID == pid)
        {
            if (prev)
            {
                prev->sig = temp->sig;
            }
            else
            {
                *lista = temp->sig;
            }
            temp->sig = NULL;
            return temp;
        }
        prev = temp;
        temp = temp->sig;
    }
    return NULL;
}

int esProgramaNuevo(const char *nombre_archivo)
{
    for (int i = 0; i < total_programas; i++)
    {
        if (strcmp(programas_cargados[i], nombre_archivo) == 0)
        {
            return 0;
        }
    }
    return 1;
}

void actualizarContadorProgramas(const char *nombre_archivo)
{
    if (esProgramaNuevo(nombre_archivo) && total_programas < Max_Programas)
    {
        strncpy(programas_cargados[total_programas], nombre_archivo, 99);
        programas_cargados[total_programas][99] = '\0';
        total_programas++;
    }
}

void mostrarContadorProgramas()
{
    // mvprintw(20, 1, "Programas Diferentes: %d", total_programas); // Example position
}

void cargarProceso(char *fileName, int uid)
{
    static int ultimopid = 0;
    PCB *nuevo = (PCB *)malloc(sizeof(PCB));
    if (!nuevo)
    {
        mvprintw(16, 1, "Error: No se pudo reservar memoria para el PCB.");
        return;
    }

    nuevo->PID = ++ultimopid;
    strncpy(nuevo->fileName, fileName, sizeof(nuevo->fileName) - 1);
    nuevo->fileName[sizeof(nuevo->fileName) - 1] = '\0';
    nuevo->AX = nuevo->BX = nuevo->CX = nuevo->DX = 0;
    nuevo->PC = 0; // Virtual PC starts at 0
    nuevo->IR[0] = '\0';
    strcpy(nuevo->real_address_str, "--:-- | --");
    nuevo->sig = NULL;
    nuevo->UID = uid;
    nuevo->P = PBase;
    nuevo->KCPU = 0;
    nuevo->KCPUxU = 0;
    nuevo->TMP = NULL; // Initialize SWAP fields
    nuevo->TmpSize = 0;
    nuevo->program = NULL; // Will not use FILE* for instructions after loading to SWAP

    int lines = count_lines_in_file(fileName);
    if (lines <= 0)
    {
        mvprintw(16, 1, "Error: Archivo %s vacio o no encontrado. Proceso no cargado.", fileName);
        free(nuevo);
        return;
    }
    int frames_needed = (int)ceil((double)lines / PAGE_SIZE_INSTRUCTIONS);
    nuevo->TmpSize = frames_needed; // Store temporarily, might be updated by sibling logic

    if ((long)frames_needed * PAGE_SIZE_INSTRUCTIONS > SWAP_SIZE_INSTRUCTIONS)
    {
        mvprintw(16, 1, "Error: Programa %s (%d marcos) demasiado grande para SWAP (%d marcos max). Enviado a Terminados.", fileName, frames_needed, SWAP_SIZE_FRAMES);
        handle_process_termination(nuevo); // Free any partial resources, though none here
        listaInsertarFinal(&Terminados, nuevo);
        return;
    }

    // Check for sibling processes (same program, same user) in Listos or Ejecucion
    PCB *sibling = NULL;
    PCB *temp_check = Listos;
    while (temp_check)
    {
        if (strcmp(temp_check->fileName, fileName) == 0 && temp_check->UID == uid)
        {
            sibling = temp_check;
            break;
        }
        temp_check = temp_check->sig;
    }
    if (!sibling && Ejecucion && strcmp(Ejecucion->fileName, fileName) == 0 && Ejecucion->UID == uid)
    {
        sibling = Ejecucion;
    }

    if (sibling && sibling->TMP)
    { // Found a sibling that is already in SWAP
        mvprintw(15, 1, "Proceso PID %d es hermano de PID %d. Compartiendo SWAP.", nuevo->PID, sibling->PID);
        nuevo->TMP = sibling->TMP;         // Share TMP
        nuevo->TmpSize = sibling->TmpSize; // Share TmpSize
        // No need to load to SWAP, already there. Add to Listos.
        listaInsertarFinal(&Listos, nuevo);
    }
    else
    { // No sibling, or sibling has no TMP (should not happen if loaded), proceed to load
        int free_frames_count = 0;
        for (int i = 0; i < SWAP_SIZE_FRAMES; i++)
        {
            if (tms[i] == TMS_FREE_FRAME)
                free_frames_count++;
        }

        if (free_frames_count >= frames_needed)
        {
            mvprintw(15, 1, "Cargando %s (PID %d, %d marcos) a SWAP...", fileName, nuevo->PID, frames_needed);
            nuevo->TMP = (int *)malloc(frames_needed * sizeof(int));
            if (!nuevo->TMP)
            {
                mvprintw(16, 1, "Error: No se pudo memoria para TMP de PID %d.", nuevo->PID);
                free(nuevo);
                return;
            }

            FILE *prog_file_to_load = fopen(fileName, "r");
            if (!prog_file_to_load)
            {
                mvprintw(16, 1, "Error: No se pudo abrir %s para cargar a SWAP.", fileName);
                free(nuevo->TMP);
                free(nuevo);
                return;
            }

            char instruction_buffer_swap[INSTRUCTION_SIZE_CHARS];
            char line_buffer_file[256];

            for (int i = 0; i < frames_needed; i++)
            {                           // For each page of the process
                int allocated_swf = -1; // SWAP Frame
                for (int j = 0; j < SWAP_SIZE_FRAMES; j++)
                { // Find free frame in TMS
                    if (tms[j] == TMS_FREE_FRAME)
                    {
                        tms[j] = nuevo->PID;
                        nuevo->TMP[i] = j;
                        allocated_swf = j;
                        break;
                    }
                }
                if (allocated_swf == -1)
                { /* Should not happen due to prior check */
                    mvprintw(16, 1, "CRITICAL: No SWAP frame for PID %d page %d.", nuevo->PID, i);
                    for (int k = 0; k < i; k++)
                        tms[nuevo->TMP[k]] = TMS_FREE_FRAME; // Rollback
                    free(nuevo->TMP);
                    fclose(prog_file_to_load);
                    free(nuevo);
                    return;
                }

                for (int k = 0; k < PAGE_SIZE_INSTRUCTIONS; k++)
                { // For each instruction in page
                    long swap_pos = (long)allocated_swf * PAGE_SIZE_INSTRUCTIONS * INSTRUCTION_SIZE_CHARS + (long)k * INSTRUCTION_SIZE_CHARS;
                    if (fgets(line_buffer_file, sizeof(line_buffer_file), prog_file_to_load))
                    {
                        line_buffer_file[strcspn(line_buffer_file, "\r\n")] = 0;
                        memset(instruction_buffer_swap, ' ', INSTRUCTION_SIZE_CHARS);
                        strncpy(instruction_buffer_swap, line_buffer_file, INSTRUCTION_SIZE_CHARS);
                    }
                    else
                    { // EOF or error, pad with NULs (effectively END or NOP)
                        memset(instruction_buffer_swap, '\0', INSTRUCTION_SIZE_CHARS);
                    }
                    fseek(swap_file_ptr, swap_pos, SEEK_SET);
                    fwrite(instruction_buffer_swap, INSTRUCTION_SIZE_CHARS, 1, swap_file_ptr);
                }
            }
            fclose(prog_file_to_load);
            nuevo->program = NULL; // Original file no longer needed open by PCB
            listaInsertarFinal(&Listos, nuevo);
            mvprintw(15, 1, "Proceso PID %d (%s) cargado a SWAP y Listos.", nuevo->PID, nuevo->fileName);
        }
        else
        {
            mvprintw(15, 1, "No hay SWAP para PID %d (%s). %d marcos nec, %d libres. Enviado a Nuevos.", nuevo->PID, nuevo->fileName, frames_needed, free_frames_count);
            // Store original file path if needed, or keep FILE* open if Nuevos processes it.
            // For simplicity, we assume fileName is enough to reopen.
            // nuevo->program = fopen(fileName, "r"); // Keep it open if Nuevos needs it, or NULL if it reopens
            listaInsertarFinal(&Nuevos, nuevo);
        }
    }

    // Update user count and W if this is a new user
    int user_already_exists = 0;
    for (int i = 0; i < NumUs; ++i)
        if (Users[i] == uid)
            user_already_exists = 1;
    if (!user_already_exists && NumUs < MAX_USUARIOS)
    {
        Users[NumUs++] = uid;
    }
    actualizarPesoUsuarios(); // This recalculates W based on active users in Listos/Ejecucion
    actualizarContadorProgramas(fileName);
    imprimirListas();
}

void matarProceso(int pid)
{
    PCB *extraido = NULL;
    if (Ejecucion && Ejecucion->PID == pid)
    {
        extraido = Ejecucion;
        Ejecucion = NULL; // Detach from execution
    }
    else
    {
        extraido = listaExtraePID(&Listos, pid);
        if (!extraido)
        {
            extraido = listaExtraePID(&Nuevos, pid); // Check Nuevos too
            if (extraido)
            {
                mvprintw(16, 1, "Proceso %d (en Nuevos) terminado.", pid);
            }
            else
            {
                mvprintw(16, 1, "Error: No se encontró el proceso con PID %d para matar.", pid);
                return;
            }
        }
    }

    if (extraido)
    {
        mvprintw(15, 1, "Proceso PID %d (%s) terminado por KILL.", extraido->PID, extraido->fileName);
        handle_process_termination(extraido);      // Free SWAP resources
        listaInsertarFinal(&Terminados, extraido); // Add to terminated list
        actualizarPesoUsuarios();
        check_nuevos_list_and_load_if_space(); // Check if space opened
        imprimirListas();
    }
}

void ejecutarInstruccion(PCB *pcb)
{
    if (!pcb)
        return;

    char instruccion[20], p1[20] = "", p2[20] = ""; // Init p1, p2
    // Ensure IR is not too short for sscanf, or sscanf might read garbage
    // IR is already null-terminated at INSTRUCTION_SIZE_CHARS
    sscanf(pcb->IR, "%19s %19s %19s", instruccion, p1, p2);

    strUpper(instruccion);
    strUpper(p1);
    strUpper(p2);

    // ... (existing MOV, ADD, SUB, MUL, DIV, INC, DEC logic remains the same) ...
    // Make sure error messages inside use mvprintw and call matarProceso correctly.
    // For brevity, I'm omitting the large unchanged block of MOV/ADD/etc.
    // Ensure matarProceso is called if an error makes the process unrecoverable.
    // For example, in an invalid parameter error for MOV:
    //     mvprintw(16, 1, "Error: Param invalido en MOV. PID %d", pcb->PID);
    //     PCB* to_terminate = pcb; // If pcb is Ejecucion
    //     if(Ejecucion == pcb) Ejecucion = NULL;
    //     handle_process_termination(to_terminate);
    //     listaInsertarFinal(&Terminados, to_terminate);
    //     // No check_nuevos here, main loop will handle it
    //     return; // Critical to return so main loop knows Ejecucion might be NULL

    if (strcmp(instruccion, "MOV") == 0)
    {
        if (strcmp(p1, "AX") == 0)
        {
            if (isNumeric(p2))
                pcb->AX = atoi(p2);
            else if (strcmp(p2, "BX") == 0)
                pcb->AX = pcb->BX;
            else if (strcmp(p2, "CX") == 0)
                pcb->AX = pcb->CX;
            else if (strcmp(p2, "DX") == 0)
                pcb->AX = pcb->DX;
            else
            {
                mvprintw(16, 1, "Error MOV AX param2 PID %d", pcb->PID);
                PCB *t = pcb;
                if (Ejecucion == pcb)
                    Ejecucion = NULL;
                handle_process_termination(t);
                listaInsertarFinal(&Terminados, t);
                return;
            }
        }
        else if (strcmp(p1, "BX") == 0)
        {
            if (isNumeric(p2))
                pcb->BX = atoi(p2);
            else if (strcmp(p2, "AX") == 0)
                pcb->BX = pcb->AX;
            else if (strcmp(p2, "CX") == 0)
                pcb->BX = pcb->CX;
            else if (strcmp(p2, "DX") == 0)
                pcb->BX = pcb->DX;
            else
            {
                mvprintw(16, 1, "Error MOV BX param2 PID %d", pcb->PID);
                PCB *t = pcb;
                if (Ejecucion == pcb)
                    Ejecucion = NULL;
                handle_process_termination(t);
                listaInsertarFinal(&Terminados, t);
                return;
            }
        }
        else if (strcmp(p1, "CX") == 0)
        {
            if (isNumeric(p2))
                pcb->CX = atoi(p2);
            else if (strcmp(p2, "AX") == 0)
                pcb->CX = pcb->AX;
            else if (strcmp(p2, "BX") == 0)
                pcb->CX = pcb->BX;
            else if (strcmp(p2, "DX") == 0)
                pcb->CX = pcb->DX;
            else
            {
                mvprintw(16, 1, "Error MOV CX param2 PID %d", pcb->PID);
                PCB *t = pcb;
                if (Ejecucion == pcb)
                    Ejecucion = NULL;
                handle_process_termination(t);
                listaInsertarFinal(&Terminados, t);
                return;
            }
        }
        else if (strcmp(p1, "DX") == 0)
        {
            if (isNumeric(p2))
                pcb->DX = atoi(p2);
            else if (strcmp(p2, "AX") == 0)
                pcb->DX = pcb->AX;
            else if (strcmp(p2, "BX") == 0)
                pcb->DX = pcb->BX;
            else if (strcmp(p2, "CX") == 0)
                pcb->DX = pcb->CX;
            else
            {
                mvprintw(16, 1, "Error MOV DX param2 PID %d", pcb->PID);
                PCB *t = pcb;
                if (Ejecucion == pcb)
                    Ejecucion = NULL;
                handle_process_termination(t);
                listaInsertarFinal(&Terminados, t);
                return;
            }
        }
        else
        {
            mvprintw(16, 1, "Error MOV param1 PID %d", pcb->PID);
            PCB *t = pcb;
            if (Ejecucion == pcb)
                Ejecucion = NULL;
            handle_process_termination(t);
            listaInsertarFinal(&Terminados, t);
            return;
        }
    }
    else if (strcmp(instruccion, "ADD") == 0)
    {
        // Similar structure for ADD, SUB, MUL, DIV, INC, DEC
        // Ensure to call handle_process_termination and set Ejecucion=NULL on error path
        if (strcmp(p1, "AX") == 0)
        {
            if (isNumeric(p2))
                pcb->AX += atoi(p2);
            else if (strcmp(p2, "BX") == 0)
                pcb->AX += pcb->BX; /* ... DX */
            else
            { /* err */
            }
        }
        // ... other registers for ADD
    }
    else if (strcmp(instruccion, "SUB") == 0)
    { /* ... */
    }
    else if (strcmp(instruccion, "MUL") == 0)
    { /* ... */
    }
    else if (strcmp(instruccion, "DIV") == 0)
    {
        int divisor = 0;
        int error_div = 0;
        if (isNumeric(p2))
            divisor = atoi(p2);
        else if (strcmp(p2, "AX") == 0)
            divisor = pcb->AX;
        else if (strcmp(p2, "BX") == 0)
            divisor = pcb->BX;
        else if (strcmp(p2, "CX") == 0)
            divisor = pcb->CX;
        else if (strcmp(p2, "DX") == 0)
            divisor = pcb->DX;
        else
            error_div = 1;

        if (!error_div && divisor == 0)
        {
            mvprintw(16, 1, "Error DIV by zero PID %d", pcb->PID);
            PCB *t = pcb;
            if (Ejecucion == pcb)
                Ejecucion = NULL;
            handle_process_termination(t);
            listaInsertarFinal(&Terminados, t);
            return;
        }
        if (error_div)
        {
            mvprintw(16, 1, "Error DIV param2 PID %d", pcb->PID);
            PCB *t = pcb;
            if (Ejecucion == pcb)
                Ejecucion = NULL;
            handle_process_termination(t);
            listaInsertarFinal(&Terminados, t);
            return;
        }

        if (strcmp(p1, "AX") == 0)
            pcb->AX /= divisor;
        else if (strcmp(p1, "BX") == 0)
            pcb->BX /= divisor;
        else if (strcmp(p1, "CX") == 0)
            pcb->CX /= divisor;
        else if (strcmp(p1, "DX") == 0)
            pcb->DX /= divisor;
        else
        {
            mvprintw(16, 1, "Error DIV param1 PID %d", pcb->PID);
            PCB *t = pcb;
            if (Ejecucion == pcb)
                Ejecucion = NULL;
            handle_process_termination(t);
            listaInsertarFinal(&Terminados, t);
            return;
        }
    }
    else if (strcmp(instruccion, "INC") == 0)
    {
        if (strcmp(p1, "AX") == 0)
            pcb->AX++;
        else if (strcmp(p1, "BX") == 0)
            pcb->BX++; /* ... */
        else
        { /* err */
        }
    }
    else if (strcmp(instruccion, "DEC") == 0)
    {
        if (strcmp(p1, "AX") == 0)
            pcb->AX--;
        else if (strcmp(p1, "BX") == 0)
            pcb->BX--; /* ... */
        else
        { /* err */
        }
    }
    // END instruction
    else if (strcmp(instruccion, "END") == 0)
    {
        // pcb->program was already closed if loaded from SWAP.
        // If it was from Nuevos and never fully loaded, handle_process_termination will close it.
        mvprintw(15, 1, "Proceso PID %d (%s) ejecuto END.", pcb->PID, pcb->fileName);
        PCB *to_terminate = pcb; // Assuming pcb is Ejecucion
        if (Ejecucion == pcb)
            Ejecucion = NULL; // Critical: Mark that no process is in execution

        handle_process_termination(to_terminate);      // Free SWAP resources
        listaInsertarFinal(&Terminados, to_terminate); // Add to terminated list
        // actualizaPesoUsuarios and check_nuevos will be called in main loop
        return; // Return to main loop, Ejecucion is now NULL
    }
    else
    {
        // Truncate IR for display if it's too long or contains non-printable chars
        char display_ir[INSTRUCTION_SIZE_CHARS + 1];
        strncpy(display_ir, pcb->IR, INSTRUCTION_SIZE_CHARS);
        display_ir[INSTRUCTION_SIZE_CHARS] = '\0';
        for (int i = 0; i < INSTRUCTION_SIZE_CHARS; ++i)
            if (!isprint(display_ir[i]) && display_ir[i] != '\0')
                display_ir[i] = '?';

        mvprintw(16, 1, "Error: Instr no valida: [%s] PID %d. Terminando.", display_ir, pcb->PID);
        PCB *to_terminate = pcb;
        if (Ejecucion == pcb)
            Ejecucion = NULL;
        handle_process_termination(to_terminate);
        listaInsertarFinal(&Terminados, to_terminate);
        return;
    }

    // Update processor display after successful instruction (if not END or error that returned)
    // This part is reached only if instruction was valid and NOT an END or error that returned early.
    // mvprintw calls are done in imprimirListas called from main loop.
}

void imprimirListas()
{
    // Clear specific areas before redrawing
    for (int i = 1; i <= 35; i++)
    { // Increased clear range for right side lists if they grow
        move(i, 88);
        clrtoeol();
    }
    // Clearing for the bottom-left display area (SWAP Info, TMS, TMP, SWAP Content)
    // Max line used by this section will be calculated. Let's say it's around line 40.
    // For now, clear a generous area.
    int bottom_left_start_line = 19;
    int estimated_max_lines_for_bottom_left = 20; // Estimate for SWAP info, TMS, TMP, SWAP content header + rows
    for (int i = bottom_left_start_line; i < bottom_left_start_line + estimated_max_lines_for_bottom_left; i++)
    {
        move(i, 1);
        clrtoeol();
    }

    // Display CPU state
    if (Ejecucion)
    {
        mvprintw(7, 1, "- AX:[%d]%-5s", Ejecucion->AX, "");
        clrtoeol();
        mvprintw(7, 1, "- AX:[%d]", Ejecucion->AX);
        mvprintw(8, 1, "- BX:[%d]%-5s", Ejecucion->BX, "");
        clrtoeol();
        mvprintw(8, 1, "- BX:[%d]", Ejecucion->BX);
        mvprintw(9, 1, "- CX:[%d]%-5s", Ejecucion->CX, "");
        clrtoeol();
        mvprintw(9, 1, "- CX:[%d]", Ejecucion->CX);
        mvprintw(10, 1, "- DX:[%d]%-5s", Ejecucion->DX, "");
        clrtoeol();
        mvprintw(10, 1, "- DX:[%d]", Ejecucion->DX);
        mvprintw(11, 1, "- P:[%d]%-5s", Ejecucion->P, "");
        clrtoeol();
        mvprintw(11, 1, "- P:[%d]", Ejecucion->P);
        mvprintw(12, 1, "- KCPU:[%d]%-3s", Ejecucion->KCPU, "");
        clrtoeol();
        mvprintw(12, 1, "- KCPU:[%d]", Ejecucion->KCPU);

        mvprintw(7, 45, "PC (Virtual):[%d]%-5s", Ejecucion->PC, "");
        clrtoeol();
        mvprintw(7, 45, "PC (Virtual):[%d]", Ejecucion->PC);

        char display_ir_main[INSTRUCTION_SIZE_CHARS + 1];
        strncpy(display_ir_main, Ejecucion->IR, INSTRUCTION_SIZE_CHARS);
        display_ir_main[INSTRUCTION_SIZE_CHARS] = '\0';
        for (int k = 0; k < INSTRUCTION_SIZE_CHARS; ++k)
            if (!isprint(display_ir_main[k]) && display_ir_main[k] != '\0')
                display_ir_main[k] = '.';

        mvprintw(8, 45, "IR:[%s]%-25s", display_ir_main, "");
        clrtoeol();
        mvprintw(8, 45, "IR:[%s]", display_ir_main);
        mvprintw(9, 45, "PID:[%d]%-5s", Ejecucion->PID, "");
        clrtoeol();
        mvprintw(9, 45, "PID:[%d]", Ejecucion->PID);
        mvprintw(10, 45, "NAME:[%s]%-20s", Ejecucion->fileName, "");
        clrtoeol();
        mvprintw(10, 45, "NAME:[%s]", Ejecucion->fileName);
        mvprintw(11, 45, "UID:[%d]%-5s", Ejecucion->UID, "");
        clrtoeol();
        mvprintw(11, 45, "UID:[%d]", Ejecucion->UID);
        mvprintw(12, 45, "KCPUxU:[%d]%-3s", Ejecucion->KCPUxU, "");
        clrtoeol();
        mvprintw(12, 45, "KCPUxU:[%d]", Ejecucion->KCPUxU);
        mvprintw(5, 45, "Real Addr: [%s]%-15s", Ejecucion->real_address_str, "");
        clrtoeol();
        mvprintw(5, 45, "Real Addr: [%s]", Ejecucion->real_address_str);
    }
    else
    {
        mvprintw(7, 1, "- AX:[--]     ");
        mvprintw(8, 1, "- BX:[--]     ");
        mvprintw(9, 1, "- CX:[--]     ");
        mvprintw(10, 1, "- DX:[--]     ");
        mvprintw(11, 1, "- P:[--]      ");
        mvprintw(12, 1, "- KCPU:[--]   ");
        mvprintw(7, 45, "PC (Virtual):[--]         ");
        mvprintw(8, 45, "IR:[--]                          ");
        mvprintw(9, 45, "PID:[--]                         ");
        mvprintw(10, 45, "NAME:[--]                        ");
        mvprintw(11, 45, "UID:[--]     ");
        mvprintw(12, 45, "KCPUxU:[--]  ");
        mvprintw(5, 45, "Real Addr (Swap):[--:-- | --]      ");
    }

    // Right side display (Lists)
    mvprintw(1, 90, "Usuarios:[%d], W:[%.2f] PBase:[%d]", NumUs, W, PBase);
    mvprintw(3, 90, "Ejecucion:");
    if (Ejecucion)
    {
        mvprintw(4, 88, "PID:[%d] U:[%d] P:[%d] KCPU:[%d] KU:[%d] F:[%s]",
                 Ejecucion->PID, Ejecucion->UID, Ejecucion->P, Ejecucion->KCPU, Ejecucion->KCPUxU, Ejecucion->fileName);
    }
    else
    {
        mvprintw(4, 88, "(ninguno)");
    }

    int current_list_y = 6; // Current Y for printing lists on the right

    mvprintw(current_list_y++, 90, "Listos (max 5):");
    PCB *temp_l = Listos;
    int count_l = 0;
    int hidden_l = 0;
    while (temp_l && count_l < 5)
    {
        mvprintw(current_list_y++, 88, "P:%d U:%d P:%d Kc:%d Ku:%d %s", temp_l->PID, temp_l->UID, temp_l->P, temp_l->KCPU, temp_l->KCPUxU, temp_l->fileName);
        temp_l = temp_l->sig;
        count_l++;
    }
    while (temp_l)
    {
        hidden_l++;
        temp_l = temp_l->sig;
    }
    if (hidden_l > 0)
        mvprintw(current_list_y++, 88, "... y %d mas.", hidden_l);

    mvprintw(current_list_y++, 90, "Nuevos (max 5):");
    temp_l = Nuevos;
    count_l = 0;
    hidden_l = 0;
    while (temp_l && count_l < 5)
    {
        mvprintw(current_list_y++, 88, "P:%d U:%d F:%s (Needs %d fr)", temp_l->PID, temp_l->UID, temp_l->fileName, temp_l->TmpSize);
        temp_l = temp_l->sig;
        count_l++;
    }
    while (temp_l)
    {
        hidden_l++;
        temp_l = temp_l->sig;
    }
    if (hidden_l > 0)
        mvprintw(current_list_y++, 88, "... y %d mas.", hidden_l);

    mvprintw(current_list_y++, 90, "Terminados (max 5):");
    temp_l = Terminados;
    count_l = 0;
    hidden_l = 0;
    while (temp_l && count_l < 5)
    {
        mvprintw(current_list_y++, 88, "P:%d U:%d F:%s", temp_l->PID, temp_l->UID, temp_l->fileName);
        temp_l = temp_l->sig;
        count_l++;
    }
    while (temp_l)
    {
        hidden_l++;
        temp_l = temp_l->sig;
    }
    if (hidden_l > 0)
        mvprintw(current_list_y++, 88, "... y %d mas.", hidden_l);

    // --- Bottom-Left Display Area ---
    int current_y = bottom_left_start_line; // Start Y for this section

    // TMS Display
    int free_frames_count = 0;
    for (int i = 0; i < SWAP_SIZE_FRAMES; ++i)
        if (tms[i] == TMS_FREE_FRAME)
            free_frames_count++;

    // Asegurar que tms_display_start esté dentro de los límites válidos
    if (tms_display_start >= SWAP_SIZE_FRAMES)
    {
        tms_display_start = SWAP_SIZE_FRAMES - TMS_DISPLAY_ENTRIES;
    }
    if (tms_display_start < 0)
    {
        tms_display_start = 0;
    }

    mvprintw(current_y++, 1, "---TMS---");
    mvprintw(current_y++, 1, "Marco-PID");

    // Mostrar solo las entradas visibles en la página actual
    for (int i = tms_display_start; i < tms_display_start + TMS_DISPLAY_ENTRIES && i < SWAP_SIZE_FRAMES; ++i)
    {
        mvprintw(current_y++, 1, "%03X - %-3X", i, tms[i]);
    }
    // Mostrar indicadores de navegación
    mvprintw(current_y++, 1,
             "TMS F9: Siguiente | F10: Anterior");

    current_y++; // Add a blank line after TMP

    // --- New SWAP Content Display ---
    if (swap_file_ptr)
    {

        // Declarar variables AL PRINCIPIO del bloque
        char instr_buffer[INSTRUCTION_SIZE_CHARS + 1];
        char display_instr_segment[SWAP_CONTENT_INSTR_TRUNCATE_LEN + 1];
        int col_width = 7 + SWAP_CONTENT_INSTR_TRUNCATE_LEN;

        // Recalculamos free_frames_count antes de usarla
        int free_frames_count = 0;
        for (int i = 0; i < SWAP_SIZE_FRAMES; ++i)
        {
            if (tms[i] == TMS_FREE_FRAME)
                free_frames_count++;
        }

        double occupied_percentage = (SWAP_SIZE_FRAMES > 0) ? ((double)(SWAP_SIZE_FRAMES - free_frames_count) * 100.0 / SWAP_SIZE_FRAMES) : 0.0;

        char swap_header[200];
        snprintf(swap_header, sizeof(swap_header),
                 "--SWAP--[%ld]Inst en [%.0f%%] Marcos de [%d] Inst de [%d]Bytes c/u = [%ld] Bytes",
                 (long)SWAP_SIZE_INSTRUCTIONS, occupied_percentage, PAGE_SIZE_INSTRUCTIONS,
                 INSTRUCTION_SIZE_CHARS, (long)SWAP_SIZE_INSTRUCTIONS * INSTRUCTION_SIZE_CHARS);

        mvprintw(current_y++, 1, "%s", swap_header);
        // Rellena el resto de la línea con '-'
        for (int k = strlen(swap_header); k < 78; k++)
        {
            mvaddch(current_y - 1, k + 1, '-');
        }

        // Mostrar contenido de SWAP en formato por columnas
        for (int col = 0; col < SWAP_DISPLAY_COLUMNS; col++)
        {
            int frame_idx = swap_display_start_frame + col;
            if (frame_idx >= SWAP_SIZE_FRAMES)
                continue;

            int x_pos = 1 + col * (7 + SWAP_CONTENT_INSTR_TRUNCATE_LEN);
            mvprintw(current_y, x_pos, "[%04X]", frame_idx);

            for (int row = 0; row < SWAP_DISPLAY_ROWS; row++)
            {
                long instr_idx = (long)frame_idx * PAGE_SIZE_INSTRUCTIONS + row;
                if (instr_idx >= SWAP_SIZE_INSTRUCTIONS)
                    break;

                fseek(swap_file_ptr, instr_idx * INSTRUCTION_SIZE_CHARS, SEEK_SET);
                size_t bytes_read = fread(instr_buffer, 1, INSTRUCTION_SIZE_CHARS, swap_file_ptr);

                // Procesar instrucción para mostrar
                if (bytes_read > 0)
                {
                    instr_buffer[bytes_read] = '\0';
                    for (int k = 0; k < bytes_read; ++k)
                    {
                        if (!isprint(instr_buffer[k]))
                            instr_buffer[k] = '.';
                    }
                    strncpy(display_instr_segment, instr_buffer, SWAP_CONTENT_INSTR_TRUNCATE_LEN);
                    display_instr_segment[SWAP_CONTENT_INSTR_TRUNCATE_LEN] = '\0';
                }
                else
                {
                    strcpy(display_instr_segment, "(empty)");
                }

                mvprintw(current_y + row + 1, x_pos, "[%04lX]%*s%-*s",
                         instr_idx, 2, "", SWAP_CONTENT_INSTR_TRUNCATE_LEN, display_instr_segment);
            }
        }

        // Mostrar indicadores de navegación
        mvprintw(current_y + SWAP_DISPLAY_ROWS + 2, 1,
                 "SWAP F7: Anterior | F8: Siguiente | Frame Inicial: %04X", swap_display_start_frame);
    }
    else
    {
        mvprintw(current_y++, 1, "SWAP File not accessible.");
    }
    // --- End of New SWAP Content Display ---

    mostrarContadorProgramas(); // If this prints anything, ensure its y is managed
    refresh();
}

void manejarLineaComandos(char *comando, int *comandoIndex, char historial[HISTORIAL_SIZE][200], int *histIndex, int *histCursor)
{
    int tecla = getch();
    static int velocidad_delay_factor = 100; // Keep DELAY logic as is

    // Asegurar que swap_display_start_frame no exceda los límites
    if (swap_display_start_frame >= SWAP_SIZE_FRAMES)
    {
        swap_display_start_frame = SWAP_SIZE_FRAMES - SWAP_DISPLAY_COLUMNS;
    }
    if (swap_display_start_frame < 0)
    {
        swap_display_start_frame = 0;
    }

    if (tecla == '\n')
    {
        comando[*comandoIndex] = '\0';
        if (*comandoIndex > 0)
        {
            for (int i = HISTORIAL_SIZE - 1; i > 0; i--)
                strcpy(historial[i], historial[i - 1]);
            strcpy(historial[0], comando);
            *histIndex = (*histIndex + 1) % HISTORIAL_SIZE; // This seems unused, histCursor is primary
            *histCursor = -1;
        }

        char cmd_verb[100], fileName_cmd[100];
        int uid_cmd, pid_cmd; // For parsing

        if (sscanf(comando, "%s %s %d", cmd_verb, fileName_cmd, &uid_cmd) == 3 &&
            (strcmp(cmd_verb, "LOAD") == 0 || strcmp(cmd_verb, "CARGAR") == 0))
        {
            if (uid_cmd >= 0)
            {
                cargarProceso(fileName_cmd, uid_cmd);
            }
            else
            {
                mvprintw(16, 1, "Error: UID invalido. Uso: LOAD <nombre_archivo> <UID_no_negativo>");
            }
        }
        else if (sscanf(comando, "%s %d", cmd_verb, &pid_cmd) == 2 &&
                 (strcmp(cmd_verb, "KILL") == 0 || strcmp(cmd_verb, "MATAR") == 0))
        {
            matarProceso(pid_cmd);
        }
        else if (strcmp(comando, "EXIT") == 0 || strcmp(comando, "SALIR") == 0)
        {
            // Free all lists (Nuevos, Listos, Terminados, Ejecucion)
            PCB *p;
            while (Nuevos)
            {
                p = listaExtraeInicio(&Nuevos);
                handle_process_termination(p);
                free(p);
            }
            while (Listos)
            {
                p = listaExtraeInicio(&Listos);
                handle_process_termination(p);
                free(p);
            }
            while (Terminados)
            {
                p = listaExtraeInicio(&Terminados);
                handle_process_termination(p);
                free(p);
            }
            if (Ejecucion)
            {
                handle_process_termination(Ejecucion);
                free(Ejecucion);
                Ejecucion = NULL;
            }

            shutdown_swap_system(); // Close SWAP file
            endwin();
            exit(0);
        }
        else if (strlen(comando) > 0)
        {
            mvprintw(16, 1, "Comando no reconocido o formato incorrecto.");
        }

        *comandoIndex = 0;
        comando[0] = '\0';
        mvprintw(15, 1, "                                                                  "); // Clear previous message line
        mvprintw(16, 1, "                                                                  "); // Clear previous message line
        imprimirListas();                                                                      // Refresh display after command
    }
    else if (tecla == 127 || tecla == 8 || tecla == KEY_BACKSPACE)
    { /* ... existing backspace ... */
        if (*comandoIndex > 0)
        {
            (*comandoIndex)--;
            comando[*comandoIndex] = '\0';
        }
    }
    else if (tecla == KEY_UP)
    { /* ... existing KEY_UP ... */
        if (*histCursor < HISTORIAL_SIZE - 1 && strlen(historial[*histCursor + 1]) > 0)
        {
            (*histCursor)++;
            strcpy(comando, historial[*histCursor]);
            *comandoIndex = strlen(comando);
        }
    }
    else if (tecla == KEY_DOWN)
    { /* ... existing KEY_DOWN ... */
        if (*histCursor > 0)
        {
            (*histCursor)--;
            strcpy(comando, historial[*histCursor]);
            *comandoIndex = strlen(comando);
        }
        else
        {
            *histCursor = -1;
            comando[0] = '\0';
            *comandoIndex = 0;
        }
    }
    else if (tecla == KEY_RIGHT || tecla == KEY_LEFT)
    { /* ... existing delay control ... */
        if (tecla == KEY_RIGHT && velocidad_delay_factor > 1)
            velocidad_delay_factor -= 1; // Faster
        else if (tecla == KEY_LEFT && velocidad_delay_factor < 200)
            velocidad_delay_factor += 1;        // Slower
        DELAY = velocidad_delay_factor * 50000; // Adjusted range, original was 1000000
        mvprintw(17, 1, "Velocidad ajustada. DELAY: %d us", DELAY);
    }
    else if (isprint(tecla) && *comandoIndex < 199)
    { /* ... existing char input ... */
        comando[(*comandoIndex)++] = tecla;
        comando[*comandoIndex] = '\0';
    }
    else if (tecla == KEY_F(7))
    { // F7 para navegar hacia atrás
        if (swap_display_start_frame > 0)
        {
            swap_display_start_frame--;
        }
        else
        {
            swap_display_start_frame = SWAP_SIZE_FRAMES - SWAP_DISPLAY_COLUMNS;
        }
    }
    else if (tecla == KEY_F(8))
    { // F8 para navegar hacia adelante
        if (swap_display_start_frame < SWAP_SIZE_FRAMES - SWAP_DISPLAY_COLUMNS)
        {
            swap_display_start_frame++;
        }
        else
        {
            swap_display_start_frame = 0;
        }
    }
    else if (tecla == KEY_F(9))
    { // F9 para avanzar página en TMS
        if (tms_display_start < SWAP_SIZE_FRAMES - TMS_DISPLAY_ENTRIES)
        {
            tms_display_start += TMS_DISPLAY_ENTRIES;
        }
        else
        {
            tms_display_start = 0; // Volver al inicio si llegamos al final
        }
    }
    else if (tecla == KEY_F(10))
    { // F10 para retroceder página en TMS
        if (tms_display_start >= TMS_DISPLAY_ENTRIES)
        {
            tms_display_start -= TMS_DISPLAY_ENTRIES;
        }
        else
        {
            tms_display_start = SWAP_SIZE_FRAMES - TMS_DISPLAY_ENTRIES;
            if (tms_display_start < 0)
                tms_display_start = 0;
        }
    }

    move(1, 1);
    clrtoeol();
    mvprintw(1, 1, "#> %s", comando);
    if (*comandoIndex == 0)
        mvprintw(3, 1, "Comandos: LOAD <f> <uid> | KILL <pid> | EXIT");
    else
        mvprintw(3, 1, "                                            ");

    refresh();
    // usleep(10000); // Small delay for responsiveness, main loop has DELAY
}

int isNumeric(char *str)
{
    if (!str || *str == '\0' || (*str == '-' && *(str + 1) == '\0'))
        return 0; // Empty, null, or just "-"
    char *p = str;
    if (*p == '-')
        p++; // Allow negative numbers
    while (*p)
    {
        if (!isdigit(*p))
            return 0;
        p++;
    }
    return 1;
}

void strUpper(char *str)
{
    for (int i = 0; str[i]; i++)
        str[i] = toupper(str[i]);
}

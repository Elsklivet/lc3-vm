// Adapted from this tutorial: https://justinmeiners.github.io/lc3-vm/index.html
// This is NOT a new or original implementation, it is just my code from following the tutorial.
// Elsklivet (Gavin H)
// Discord @Elsklivet#8867
// gmh33@pitt.edu

#include <stdint.h> // uint16_t
#include <stdio.h>  // FILE
#include <signal.h> // SIGINT

/* windows only */
#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#include <conio.h>  // _kbhit
HANDLE hStdin = INVALID_HANDLE_VALUE;
#endif

// Linux-only includes
#ifdef linux
/* unix */
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>
#endif

// There are 65,536 memory locations, because that's the maximum
// a 16-bit unsigned integer can hold.
uint16_t memory[UINT16_MAX];

/* memory mapped registers */
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};

/* trapcodes */
enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

// For the registers, we use an enum:
enum
{
    R_R0 = 0,   // r0
    R_R1,       // r1
    R_R2,       // r2
    R_R3,       // r3
    R_R4,       // r4
    R_R5,       // r5
    R_R6,       // r6
    R_R7,       // r7
    R_PC,       // pc
    R_COND,     // cond
    R_COUNT     // number of registers
};

// The actual contents of the registers can be stored in 
// another array of 16-bit unsigned integers. 
uint16_t reg[R_COUNT];

// Here's the fun stuff...
// OPCODES:
enum
{
    OP_BR = 0, // branch 
    OP_ADD,    // add  
    OP_LD,     // load 
    OP_ST,     // store 
    OP_JSR,    // jump register 
    OP_AND,    // bitwise and 
    OP_LDR,    // load register 
    OP_STR,    // store register 
    OP_RTI,    // unused 
    OP_NOT,    // bitwise not 
    OP_LDI,    // load indirect 
    OP_STI,    // store indirect 
    OP_JMP,    // jump 
    OP_RES,    // reserved (unused) 
    OP_LEA,    // load effective address 
    OP_TRAP    // execute trap 
};

// Condition flags
enum {                  // Correct me if I'm wrong but... aren't these just
    FL_POS = 1 << 0,    // 0x01 (0b0001)
    FL_ZRO = 1 << 1,    // 0x02 (0b0010)
    FL_NEG = 1 << 2     // 0x04 (0b0100) ? Why use the shift here?
};

// Here is an example assembly program provided in the tutorial:
/*
.ORIG x3000                        ; this is the address in memory where the program will be loaded
LEA R0, HELLO_STR                  ; load the address of the HELLO_STR string into R0
PUTs                               ; output the string pointed to by R0 to the console
HALT                               ; halt the program
HELLO_STR .STRINGZ "Hello World!"  ; store this string here in the program
.END                               ; mark the end of the file
*/

// And here is an example with a loop:
/*
AND R0, R0, 0                      ; clear R0
LOOP                               ; label at the top of our loop
ADD R0, R0, 1                      ; add 1 to R0 and store back in R0
ADD R1, R0, -10                    ; subtract 10 from R0 and store back in R1
BRn LOOP                           ; go back to LOOP if the result was negative
... ; R0 is now 10!
*/

// Helpers:
uint16_t sign_extend(uint16_t x, int bit_count){
    // 1001 0111 (4-1) => 1111 0010 & 1 = 0000 0000
    // 0110 0010 (4-1) => 0000 1100 & 1 = 0000 0000 ???
    // Why/how does this work?
    if ((x >> (bit_count - 1)) & 1){
        x |= (0xFFFF << bit_count);
    }

    return x;
}

/* Our computers are little-endian but lc3 programs are big-endian, 
so swap the numbers around. This is easy to achieve by shifting
the bottom bits to the top and top bits to the bottom, then or-ing
them together. */
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

// Update flags
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) // a 1 in the left-most bit indicates negative
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

/* Reads a compiled image file into the VM */
void read_image_file(FILE* file){
    /* where to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    uint16_t max_read_size = UINT16_MAX - origin;
    uint16_t* ptr = memory + origin;
    size_t read = fread(ptr, sizeof(uint16_t), max_read_size, file);

    while(read-- > 0){
        *ptr = swap16(*ptr);
        ++ptr;
    }
}

int read_image(const char* image_path){
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}

#ifdef linux
uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}
#endif

#if defined(_WIN32) || defined(_WIN64)
uint16_t check_key()
{
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

DWORD fdwMode, fdwOldMode;

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); /* save old mode */
    fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT  /* no input echo */
            ^ ENABLE_LINE_INPUT; /* return when one or
                                    more characters are available */
    SetConsoleMode(hStdin, fdwMode); /* set new mode */
    FlushConsoleInputBuffer(hStdin); /* clear buffer */
}

void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode);
}
#endif

void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

// All of the instructions need to be implemented here.
// Here's a PDF of the ISA https://justinmeiners.github.io/lc3-vm/supplies/lc3-isa.pdf
// This is an exercise to the reader, so I'd like to do it without the tutorial's given code.

// OP_ADD
// Encodings:
// 15  12 | 11 9 | 8  6 | 5 | 4 3 | 2 0 |
//  0001  |  DR  | SR1  | 0 |  00 | SR2 |
// 15  12 | 11 9 | 8  6 | 5 | 4       0 |
//  0001  | DR   | SR1  | 1 |    imm5   |


// Main loop
int main(int argc, const char* argv[]){
    // Load Arguments
    if(argc < 2) {
        // Incorrect usage. Expect at least one path to an image file.
        printf("Usage: lc3.exe [image file 1] ... [image file n]\n");
        exit(2);
    }
    for(int i = 0; i < argc; ++i){
        if(!read_image(argv[i])) {
            printf("Critical failure loading image: %s\n",  argv[i]);
            exit(1);
        }
    }

    // Setup
    #ifdef linux
    signal(SIGINT, handle_interrupt);
    #endif
    disable_input_buffering();

    // Set PC to its starting position, originating at 0x3000
    enum { PC_START = 0x3000};
    reg[R_PC] = PC_START;

    int running = 1;
    while(running){
        // Fetch phase:
        uint16_t instruction = mem_read(reg[R_PC]++);
        uint16_t op = instruction >> 12;

        switch(op){
            case OP_ADD:
                // ADD
                /* destination register */
                {
                uint16_t r0 = (instruction >> 9) & 0x7;
                /* first operand (SR1) */
                uint16_t r1 = (instruction >> 6) & 0x7;
                /* immediate flag */
                uint16_t imm_flag = (instruction >>5 ) & 0x1;

                if(imm_flag){
                    /* immediate mode */
                    uint16_t imm5 = sign_extend(instruction & 0x1f, 5);
                    reg[r0] = reg[r1] + imm5;
                }else{
                    /* third register mode */
                    uint16_t r2 = instruction & 0x7;
                    reg[r0] = reg[r1] + reg[r2];
                }
                update_flags(r0);
                }
                break;
            case OP_AND:
                // AND
                {
                uint16_t r0 = (instruction >> 9) & 0x7; /* 0x7 = 0b0111 */
                uint16_t r1 = (instruction >> 6) & 0x7;
                uint16_t imm_flag = (instruction >> 5) & 0x1;
                if(imm_flag){
                    /* immediate mode */
                    /* 0x1f = 0001 1111 */
                    uint16_t imm5 = sign_extend(instruction & 0x1f, 5); 
                    reg[r0] = reg[r1] & imm5;
                }else{
                    /* third register mode */
                    uint16_t r2 = instruction & 0x7;
                    reg[r0] = reg[r1] & reg[r2];
                }
                update_flags(r0);
                }
                break;
            case OP_NOT:
                // NOT
                {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t r1 = (instruction >> 6) & 0x7;
                reg[r0] = ~reg[r1];
                update_flags(r0);
                }
                break;
            case OP_BR:
                // BRANCH
                /*
                if ((n AND N) OR (z AND Z) OR (p AND P))
                    PC = PC‡ + SEXT(PCoffset9);
                */
                {
                uint16_t pc_offset = sign_extend(instruction & 0x1ff, 9);
                uint16_t cond_flags = (instruction >> 9) & 0x7;
                if(cond_flags & reg[R_COND]){
                    reg[R_PC] += pc_offset;
                }
                }
                break;
            case OP_JMP:
                // JUMP and RETURN
                {
                uint16_t r1 = (instruction >> 6) & 0x7;
                reg[R_PC] = reg[r1];
                }
                break;
            case OP_JSR:
                // JUMP REGISTER
                /*
                R7 = PC;†
                if (bit[11] == 0)
                    PC = BaseR;
                else
                    PC = PC† + SEXT(PCoffset11);
                */
                {
                uint16_t long_flag = (instruction >> 11) & 0x1;
                reg[R_R7] = reg[R_PC];
                if(long_flag){
                    /* pc offset is 11-bits wide @ 0 */
                    uint16_t long_pc_offset = sign_extend(instruction & 0x7ff, 11);
                    reg[R_PC] += long_pc_offset; /* JSR */
                }else{
                    /* BaseR is 3 bits wide @ 6 and the bottom six bits are 0 */
                    uint16_t r1 = (instruction >> 6) & 0x7;
                    reg[R_PC] = reg[r1]; /* JSRR */
                }
                }
                break;
            case OP_LD:
                // LOAD
                {
                uint16_t r0 = (instruction >> 9) & 0x7; // 0x7 = 0b0111
		        uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                reg[r0] = mem_read(reg[R_PC] + pc_offset);
                update_flags(r0);
                }
                break;
            case OP_LDI:
                // LOAD INDIRECT
                {
		        uint16_t r0 = (instruction >> 9) & 0x7; // 0x7 = 0b0111
		        uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flags(r0);
                }
                break;
            case OP_LDR:
                // LOAD REGISTER
                {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t r1 = (instruction >> 6) & 0x7;
                uint16_t offset = sign_extend(instruction & 0x3f, 6);
                reg[r0] = mem_read(reg[r1] + offset);
                update_flags(r0);
                }
                break;
            case OP_LEA:
                // LOAD EFFECTIVE ADDRESS
                {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                reg[r0] = reg[R_PC] + pc_offset;
                update_flags(r0);
                }
                break;
            case OP_ST:
                // STORE
                {
                uint16_t sr = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                mem_write(reg[R_PC] + pc_offset, reg[sr]);
                }
                break;
            case OP_STI:
                // STORE INDIRECT
                {
                uint16_t sr = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1ff, 9);
                mem_write(mem_read(reg[R_PC] + pc_offset), reg[sr]);
                }
                break;
            case OP_STR:
                // STORE REGISTER
                {
                uint16_t sr = (instruction >> 9) & 0x7;
                uint16_t baseR = (instruction >> 6) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x3f, 6);
                mem_write(reg[baseR] + pc_offset, reg[sr]);
                }
                break;
            case OP_TRAP:
                // EXECUTE TRAPCODE
                switch (instruction & 0xFF) // Bottom 8 bits are trapcode
                {
                    case TRAP_GETC:
                        // TRAP GETC
                        /* read a single ASCII char */
                        { reg[R_R0] = (uint16_t)getchar(); }
                        break;
                    case TRAP_OUT:
                        // TRAP OUT
                        {
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                        }
                        break;
                    case TRAP_PUTS:
                        // TRAP PUTS
                        {
                        uint16_t * chars = memory + reg[R_R0];
                        while(*chars){
                            putc((char)*chars, stdout);
                            ++chars;
                        }
                        fflush(stdout);
                        }
                        break;
                    case TRAP_IN:
                        // TRAP IN
                        {
                        printf("Enter a character: ");
                        char c = getchar();
                        putc(c, stdout);
                        reg[R_R0] = (uint16_t)c;
                        }
                        break;
                    case TRAP_PUTSP:
                        // TRAP PUTSPs
                        /* one char per byte (two bytes per word)
                        here we need to swap back to
                        big endian format */
                        {
                        uint16_t *chars = memory + reg[R_R0];
                        while(*chars)
                        {
                            char char1 = (*chars) & 0xFF;
                            putc(char1, stdout);
                            char char2 = (*chars) >> 8;
                            if (char2) putc(char2, stdout);
                            ++chars;
                        }
                        fflush(stdout);
                        }
                        break;
                    case TRAP_HALT:
                        // TRAP HALT
                        {
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                        }
                        break;
                }
                break;
            case OP_RES:
                // DO NOTHING
            case OP_RTI:
                // DO NOTHING
            default:
                // BAD OPCODE
                printf("Bad opcode given: %x", op);
                break;
        }
    }

    // Handle shutdown
    restore_input_buffering();
    return 0;
}

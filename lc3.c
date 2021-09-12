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
                break;
            case OP_AND:
                // AND
                break;
            case OP_NOT:
                // NOT
                break;
            case OP_BR:
                // BRANCH
                break;
            case OP_JMP:
                // JUMP
                break;
            case OP_JSR:
                // JUMP REGISTER
                break;
            case OP_LD:
                // LOAD
                break;
            case OP_LDI:
                // LOAD INDIRECT
                break;
            case OP_LDR:
                // LOAD REGISTER
                break;
            case OP_LEA:
                // LOAD EFFECTIVE ADDRESS
                break;
            case OP_ST:
                // STORE
                break;
            case OP_STI:
                // STORE INDIRECT
                break;
            case OP_STR:
                // STORE REGISTER
                break;
            case OP_TRAP:
                // EXECUTE TRAPCODE
                break;
            case OP_RES:
            case OP_RTI:
            default:
                // BAD OPCODE
                break;
        }
    }

    // Handle shutdown
    return 0;
}
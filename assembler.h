#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
typedef enum instructionFormat{
        R = 0,
        I,
        J,
}InstrFormat;

struct rFormat{
        uint8_t rs;
        uint8_t rt;
        uint8_t rd;
        uint8_t shamt;
        uint8_t funct;
};

struct iFormat{
        uint8_t rs;
        uint8_t rt;
        uint32_t imm;
};

struct jFormat{
        uint32_t imm;
};

typedef struct Tokenize{
        void * data;
        struct Tokenize * nextToken;
}Token;

typedef struct Instruction{
        InstrFormat format;
        uint8_t opCode;
        union {
                struct rFormat r;
                struct iFormat I;
                struct jFormat j;
        };
        struct Instruction * nextInstr;
}Instr;

typedef struct _BasicBlock{
        char blockName[64];
        Token * headInstr;
        uint64_t address;
        struct _BasicBlock * nextBlock;
}BasicBlock;

typedef struct Variable{
        char varName[64];
        uint64_t address;
}Var;

typedef struct VmMemory{
        uint32_t * pStackBase;
        uint32_t * pStackCur;

        uint32_t * pHeapBase;
        uint32_t * pHeapCur;

        uint32_t * pDataBase;
        uint32_t * pDataCur;

        uint32_t * pTextBase;
        uint32_t * pTextCur;

}Memory;

typedef struct VirtualMachine{
        int32_t reg[32];
        int32_t pc;
        Memory * pMem;
        Var * varSet[256];
        BasicBlock * headBlock;
        FILE * binCur;
}VM;

int8_t isSection(Token * pToken);
int8_t isInstr(Token * pToken);
int8_t isPseudo(Token * pToken);

uint64_t isDataAddr(Token * pToken, VM * pVm);
uint64_t isTextAddr(Token * pToken, VM * pVm);

bool parseAsm(VM * pVm); // Convert pseudo instr to real instr
bool Lowerer(VM * pV); // Compile instr
bool GlobOpt(VM * pVm);

bool organizeSection(VM * pVm);
bool inlinee(VM *pVm);

char * codeGen(VM * pVm); // obj to binary
char * changeExt(char * pStr);

Token * tokenize(char * pStr);
void debugPrint(VM * pVm);
void printInstr(Token * pToken);
void * stringRemoveNonAlphaNum(char *str);
uint32_t strNum(char *str);
void tokenLink(Token * head, Token * newToken);
void tokenUnlink(Token * head, int32_t index);
void tokenAdd(Token * head, int32_t index, Token * newToken);

void BlockLink(BasicBlock * head, BasicBlock * newBlock);
void BlockUnlink(BasicBlock * head, int32_t index);
void BlockAdd(BasicBlock * head, int32_t index, BasicBlock * newBlock);
BasicBlock * BlockEdge(BasicBlock * head);

char * instrSet[] = {"","addiu", "addu", "and", "andi", "beq","bne", "j", "jal", "jr", "lui", "lw", "la","nor", "or", "ori", "sltiu", "sltu", "sll","srl", "sw", "subu", "la_ori", "la_lui"};
uint32_t instrOp[] = {0, 0x9, 0x21, 0x24, 0xc, 0x4, 0x5, 0x2, 0x3, 0x8, 0xf, 0x23, 0xff, 0x27, 0x25, 0xd, 0xb, 0x2b, 0x0, 0x2, 0x2b, 0x23};
char * pseudoInstr[] = {"","la"};
char * sectionKind[] = {"","data", "text"};
Token * headList;

#define SWAP_UINT32(x) (((x) >> 24) | (((x) & 0x00FF0000) >> 8) | (((x) & 0x0000FF00) << 8) | ((x) << 24))
//Var * varSet[256] = {0, };
//uint32_t varNum = 0;

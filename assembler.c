#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include "assembler.h"
#include <ctype.h>
#define DEBUG 1

int main(int argc, char *argv[])
{
    FILE *fInput, *fOutput;
    char *pFilename;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <*.s>\n", argv[0]);
        fprintf(stderr, "Example: %s sample_input/example?.s\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    fInput = fopen(argv[1], "r");
    if (fInput == NULL) {
        perror("ERROR");
        exit(EXIT_FAILURE);
    }

    pFilename = strdup(argv[1]);
    if(changeExt(pFilename) == NULL) {
        fprintf(stderr, "'%s' file is not an assembly file.\n", pFilename);
        exit(EXIT_FAILURE);
    }

    fOutput = fopen(pFilename, "w");
    if (fOutput == NULL) {
        perror("ERROR");
        exit(EXIT_FAILURE);
    }
    VM pVm = { 0, };
    pVm.binCur = fInput;

    Memory pMem = { 0, };

    pMem.pStackBase = (uint32_t *) mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    pMem.pStackCur = pMem.pStackBase;

    pMem.pHeapBase = (uint32_t *) mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    pMem.pHeapCur = pMem.pHeapBase;

    pMem.pDataBase = (uint32_t *) mmap(0, 0x8000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    pMem.pDataCur = pMem.pDataBase;

    pMem.pTextBase = (uint32_t *) mmap(0, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    pMem.pTextCur = pMem.pTextBase;

    pVm.pMem = &pMem;

    memset(pVm.varSet, 0x0, sizeof(void *) * 256);

    pVm.headBlock = NULL;

    #ifdef DEBUG
      printf("STACK : %p\nHEAP : %p\nDATA : %p\nTEXT : %p\n", pMem.pStackBase, pMem.pHeapBase, pMem.pDataBase, pMem.pTextBase);
    #endif
    if(!parseAsm(&pVm)){
      fprintf(stderr, "Parse Error!\n");
      exit(EXIT_FAILURE);
    }

    if(!organizeSection(&pVm)){
      fprintf(stderr, "Section Error!\n");
      exit(EXIT_FAILURE);
    }

    #ifdef DEBUG
    printf("[DATA]\n");
    for(int32_t i = 0; pVm.varSet[i]; i++)
      printf("word * %s = 0x%llx\n", pVm.varSet[i]->varName, pVm.varSet[i]->address);

    printf("[TEXT]\n");
    debugPrint(&pVm);
    #endif

    if(!inlinee(&pVm)){
      fprintf(stderr, "inlinee Error!\n");
      exit(EXIT_FAILURE);
    }
    debugPrint(&pVm);

    if(!GlobOpt(&pVm)){
      fprintf(stderr, "GlobOpt Error!\n");
      exit(EXIT_FAILURE);
    }
    debugPrint(&pVm);

    if(!Lowerer(&pVm)){
      fprintf(stderr, "Lowerer Error!\n");
      exit(EXIT_FAILURE);
    }
    debugPrint(&pVm);

    char * binaryDat = codeGen(&pVm);
    if(binaryDat == NULL){
      fprintf(stderr, "Code Generate Error!\n");
      exit(EXIT_FAILURE);
    }

    fwrite(binaryDat, strlen(binaryDat), 1, fOutput);
    fwrite("\n", 1, 1, fOutput);
    fclose(fInput);
    fclose(fOutput);
    exit(EXIT_SUCCESS);
}


char * changeExt(char *pStr)
{
    char *pDot = strrchr(pStr, '.');

    if (!pDot || pDot == pStr || (strcmp(pDot, ".s") != 0)) {
        return NULL;
    }

    pStr[strlen(pStr) - 1] = 'o';
    return "";
}

void tokenLink(Token * head, Token * newToken){
  Token * pToken = head;
  while(pToken->nextToken != NULL){ pToken = pToken->nextToken; }
  pToken->nextToken = newToken;
}

void tokenUnlink(Token * head, int32_t index){
  Token * pToken = head;
  while(--index){ pToken = pToken->nextToken; }
  Token * delToken = pToken->nextToken;
  pToken->nextToken = pToken->nextToken->nextToken;
  free(delToken);
}

void tokenAdd(Token * head, int32_t index, Token * newToken){
  Token * pToken = head;
  while(--index){ pToken = pToken->nextToken; }
  Token * tToken = pToken->nextToken;
  pToken->nextToken = newToken;
  newToken->nextToken = tToken;
}

void BlockLink(BasicBlock * head, BasicBlock * newBlock){
  BasicBlock * pBlock = head;
  while(pBlock->nextBlock != NULL){ pBlock = pBlock->nextBlock; }
  pBlock->nextBlock = newBlock;
}

void BlockUnlink(BasicBlock * head, int32_t index){
  BasicBlock * pBlock = head;
  while(--index){ pBlock = pBlock->nextBlock; }
  BasicBlock * delBlock = pBlock->nextBlock;
  pBlock->nextBlock = pBlock->nextBlock->nextBlock;
  free(delBlock);
}

void BlockAdd(BasicBlock * head, int32_t index, BasicBlock * newBlock){
  BasicBlock * pBlock = head;
  while(--index){ pBlock = pBlock->nextBlock; }
  BasicBlock * tBlock = pBlock->nextBlock;
  pBlock->nextBlock = newBlock;
  newBlock->nextBlock = tBlock;
}

BasicBlock * BlockEdge(BasicBlock * head){
  BasicBlock * pBlock = head;
  while( pBlock->nextBlock ){ pBlock = pBlock->nextBlock; }
  return pBlock;
}

void * stringRemoveNonAlphaNum(char *str){
    int32_t i = 0;
    int32_t j = 0;
    char c;
    while ((c = str[i++]) != '\0')
    {
        if (isalnum(c) || c == '$' || c == '-')
        {
            str[j++] = c;
        }
    }
    str[j] = '\0';
   return str;
}

Token * tokenize(char * pStr){
    Token * head = (Token *) malloc(sizeof(Token));
    head->nextToken = NULL;
    head->data = (void *)stringRemoveNonAlphaNum(strdup(strtok(pStr, " \t\n")));
    char * data = strtok(0, " \t\n");
    while( data != NULL ){
      Token * newToken = (Token *) malloc(sizeof(Token));
      newToken->nextToken = NULL;
      newToken->data = (void *)stringRemoveNonAlphaNum(strdup(data));
      tokenLink(head, newToken);
      data = strtok(0, " \t\n");
    }
    return head;
}

bool parseAsm(VM * pVm){
  printf("{Parse ASM}\n");
  char tempLine[256];
  Token * pToken = NULL;
  headList = (Token *)malloc(sizeof(Token));
  headList->nextToken = NULL;
  headList->data = NULL;
  // Tokenize
  while(fgets(tempLine, sizeof(tempLine), pVm->binCur)){
    pToken = tokenize(tempLine);
    Token * element = (Token *)malloc(sizeof(Token));
    element->nextToken = NULL;
    element->data = (void *)pToken;
    tokenLink(headList, element);
  }
  #ifdef DEBUG
  // List iterate
  for(Token * curList = headList; curList; curList = curList->nextToken){
    if( curList->data ){
      printf("[");
      printInstr(curList->data);
      printf("]\n");
    }
  }
  #endif

  return true;
}

bool  organizeSection(VM * pVm){
  printf("{Organize Section}\n");
  Token * recordToken = NULL;
  uint64_t binaryBase = 0x400000;
  for(Token * curList = headList; curList; curList = recordToken){
    if(curList->data){
      Token * pToken = (Token *)curList->data;
      uint8_t sectionKind = isSection(pToken);
      if( sectionKind ){ // is Section Token
        Token * pLine = (Token *)curList->nextToken;
        while( pLine && pLine->data && !isSection((Token *)pLine->data) ){
          if( sectionKind == 1){ // is data Section
            Token * pCur = (Token *)pLine->data;
            if( !strcmp((char *)pCur->data, "word")){
              Token * value = pCur->nextToken;
              *(pVm->pMem->pDataCur) = strNum((char *)value->data);
              pVm->pMem->pDataCur += 1;
            }
            else{
              Var * var = (Var *)malloc(sizeof(Var));
              strcpy(var->varName, pCur->data);
              var->address = 0x10000000 + ((pVm->pMem->pDataCur) - (pVm->pMem->pDataBase))*4;
              Token * value = pCur->nextToken->nextToken;
              *(pVm->pMem->pDataCur) = strNum((char *)value->data);
              pVm->pMem->pDataCur += 1;
              int32_t index = 0;
              for(; pVm->varSet[index]; index++) { }
              pVm->varSet[index] = var;
            }
          }
          else if( sectionKind == 2){ // is text Section
            Token * pCur = (Token *)pLine->data;

            if( !isInstr(pCur) ){ // is Block Entry Point
              BasicBlock * newBlock = (BasicBlock *)malloc(sizeof(BasicBlock));
              strcpy(newBlock->blockName, pCur->data);
              newBlock->headInstr = NULL;
              newBlock->nextBlock = NULL;
              newBlock->address = binaryBase;
              if( !pVm->headBlock )
                pVm->headBlock = newBlock;
              else
                BlockLink(pVm->headBlock, newBlock);
            }
            else{
              BasicBlock * eBlock = BlockEdge(pVm->headBlock);
                    Token * element = (Token *)malloc(sizeof(Token));
                    element->data = (void *)pCur;
                    element->nextToken = NULL;
              if( !eBlock->headInstr )
                eBlock->headInstr = element;
              else
                tokenLink(eBlock->headInstr, element);
              if(strcmp(pCur->data,"la"))
                binaryBase += 4;
              else
                binaryBase += 8;
            }

          }
          pLine = pLine->nextToken;
        }
        recordToken = pLine;
      }
    }
    recordToken = curList->nextToken;
  }
  return true;
}
bool inlinee(VM *pVm){
  printf("{Inlinee}\n");
  for(BasicBlock * pBlock = pVm->headBlock; pBlock; pBlock = pBlock->nextBlock){
    uint32_t index = 0;
    for(Token * curInstr = pBlock->headInstr; curInstr;){
      Token * opToken = (Token *)curInstr->data;
      uint8_t pseudoKind = isPseudo(opToken);
      if( pseudoKind == 1) { // opcode : la
        char * reg = strdup((char *)opToken->nextToken->data);
        char * address = strdup((char *)opToken->nextToken->nextToken->data);
        tokenUnlink(pBlock->headInstr, index);

        Token * luiOp = (Token *)malloc(sizeof(Token));
        luiOp->data = (void *)strdup("la_lui");
        luiOp->nextToken = NULL;

        Token * luiReg = (Token *)malloc(sizeof(Token));
        luiReg->nextToken = NULL;
        luiReg->data = (void *)reg;

        Token * luiAddr = (Token *)malloc(sizeof(Token));
        luiAddr->nextToken = NULL;
        luiAddr->data = (void *)address;

        tokenLink(luiOp, luiReg);
        tokenLink(luiOp, luiAddr);


        Token * luiElem = (Token *)malloc(sizeof(Token));
        luiElem->data = (void *)luiOp;
        luiElem->nextToken = NULL;
        tokenAdd(pBlock->headInstr, index++, luiElem);

        Token * oriOp = (Token *)malloc(sizeof(Token));
        oriOp->data = (void *)strdup("la_ori");
        oriOp->nextToken = NULL;

        Token * oriReg1 = (Token *)malloc(sizeof(Token));
        oriReg1->nextToken = NULL;
        oriReg1->data = (void *)reg;

        Token * oriReg2 = (Token *)malloc(sizeof(Token));
        oriReg2->nextToken = NULL;
        oriReg2->data = (void *)reg;

        Token * oriAddr = (Token *)malloc(sizeof(Token));
        oriAddr->nextToken = NULL;
        oriAddr->data = (void *)address;

        tokenLink(oriOp, oriReg1);
        tokenLink(oriOp, oriReg2);
        tokenLink(oriOp, oriAddr);

        Token * oriElem = (Token *)malloc(sizeof(Token));
        oriElem->data = (void *)oriOp;
        oriElem->nextToken = NULL;
        tokenAdd(pBlock->headInstr, index, oriElem);
        curInstr = oriElem->nextToken;
      }
      else{
        curInstr = curInstr->nextToken;
      }
      index++;
    }
  }
  return true;
}
int8_t isPseudo(Token * pToken){
  for(int32_t i = 0; pseudoInstr[i]; i++){
    if(!strcmp((char *)pToken->data, pseudoInstr[i]))
      return i;
  }
  return false;
}

int8_t isInstr(Token * pToken){
  for(int32_t i = 0; instrSet[i]; i++){
    if(!strcmp((char *)pToken->data, instrSet[i]))
      return i;
  }
  return false;
}

int8_t isSection(Token * pToken){
  for(int32_t i = 0; sectionKind[i]; i++){
    if(!strcmp((char *)pToken->data, sectionKind[i]))
      return i;
  }
  return false;
}

uint64_t isDataAddr(Token *pToken, VM * pVm){
    for(uint32_t i = 0; pVm->varSet[i]; i++){
      if(!strcmp((char *)pToken->data, pVm->varSet[i]->varName))
        return pVm->varSet[i]->address;
    }
    return 0;
}

uint64_t isTextAddr(Token * pToken, VM * pVm){
  for(BasicBlock * pBlock = pVm->headBlock; pBlock; pBlock = pBlock->nextBlock){
    if(!strcmp((char *)pToken->data, pBlock->blockName))
      return pBlock->address;
  }
  return 0;
}
bool GlobOpt(VM * pVm){
  printf("{GlobOpt}\n");
  uint64_t pc = 0x400000;
  for(BasicBlock * pBlock = pVm->headBlock; pBlock; pBlock = pBlock->nextBlock){
    pBlock->address = pc;
    pc = pBlock->address;
    for(Token * curInstr = pBlock->headInstr; curInstr; curInstr = curInstr->nextToken){
      Token * opToken = (Token *)curInstr->data;
      if( isInstr(opToken) ){
        Token * r = opToken->nextToken;
        while( r ){
          if( isDataAddr(r, pVm) ) // eliminate la inlinee
          {
            uint8_t opCode = isInstr(opToken);
            if( opCode == 22 || opCode == 23 ){
              uint64_t addr = isDataAddr(r, pVm);
              r->data = malloc(0x100);
              if( opCode == 22 ){
                if(addr&0xffff){
                  strcpy(opToken->data, "ori");
                  sprintf(r->data,"0x%x", addr&0xffff);
                }
                else{
                  tokenUnlink(pBlock->headInstr, ((pc&0xffff)-(addr&0xffff))/4);
                  pc -= 4;
                }
              }
              else{
                strcpy(opToken->data, "lui");
                sprintf(r->data,"0x%x", addr>>16);
              }
            }
          }
          r = r->nextToken;
        }
      }
      pc += 4;
    }
  }
  return true;
}
bool Lowerer(VM * pVm){
  printf("{Lowerer}\n");
  uint64_t pc = 0x400000;
  for(BasicBlock * pBlock = pVm->headBlock; pBlock; pBlock = pBlock->nextBlock){
    pc = pBlock->address;
    for(Token * curInstr = pBlock->headInstr; curInstr; curInstr = curInstr->nextToken){
      Token * opToken = (Token *)curInstr->data;
      if( isInstr(opToken) ){
        Token * r = opToken->nextToken;
        while( r ){
          if( isTextAddr(r, pVm) ){
            uint8_t opCode = isInstr(opToken);
            uint64_t addr = isTextAddr(r, pVm);
            if( opCode == 5 || opCode == 6){
              uint16_t relAddr = (int16_t)((addr&0xffff)-(pc&0xffff) - 4)/4;
              r->data = malloc(0x100);
              sprintf(r->data,"0x%x", relAddr);
            }
            else{
              r->data = malloc(0x100);
              sprintf(r->data,"0x%x", addr);
            }
          }
          r = r->nextToken;
        }
      }
      pc += 4;
    }
  }


  return true;
}

char * int_to_bin_digit(unsigned int in, int count, char * out)
{
    unsigned int mask = 1U << (count-1);
    int i;
    for (i = 0; i < count; i++) {
        out[i] = (in & mask) ? '1' : '0';
        in <<= 1;
    }
    return out;
}

void printInstr(Token * pToken){
  for(Token * cur = pToken; cur; cur = cur->nextToken)
    printf("%s ", cur->data);
  printf("\n");
}

uint32_t strNum(char * str){
  if(!strncmp(str, "0x", 2))
    return strtol(str, 0, 16);
  else
    return strtol(str, 0 ,10);
}

char * codeGen(VM * pVm){
  printf("{Code Generate}\n");
  for(BasicBlock * pBlock = pVm->headBlock; pBlock; pBlock = pBlock->nextBlock){
    for(Token * curInstr = pBlock->headInstr; curInstr; curInstr = curInstr->nextToken){
      Token * opToken = curInstr->data;
      printInstr(opToken);
      uint8_t opCode = isInstr(opToken);
      uint32_t dInstr = 0;
      switch (opCode){
        case 1:
        case 4:
        case 15:
        case 16:{
          dInstr |= instrOp[opCode] << 26;
          dInstr |= strNum(&opToken->nextToken->nextToken->data[1]) << 21;
          dInstr |= strNum(&opToken->nextToken->data[1]) << 16;
          dInstr |= strNum(opToken->nextToken->nextToken->nextToken->data);
        }break;
        case 5:
        case 6:{
          dInstr |= instrOp[opCode] << 26;
          dInstr |= strNum(&opToken->nextToken->data[1]) << 21;
          dInstr |= strNum(&opToken->nextToken->nextToken->data[1]) << 16;
          dInstr |= strNum(opToken->nextToken->nextToken->nextToken->data);
        }break;

        case 10:{
          dInstr |= instrOp[opCode] << 26;
          dInstr |= 0 << 21;
          dInstr |= strNum(&opToken->nextToken->data[1]) << 16;
          dInstr |= strNum(opToken->nextToken->nextToken->data);
        }break;
        case 2:
        case 3:
        case 13:
        case 14:
        case 17:
        case 21:{
          dInstr |= 0;
          dInstr |= strNum(&opToken->nextToken->nextToken->data[1]) << 21;
          dInstr |= strNum(&opToken->nextToken->nextToken->nextToken->data[1]) << 16;
          dInstr |= strNum(&opToken->nextToken->data[1]) << 11;
          dInstr |= 0;
          dInstr |= instrOp[opCode];
        }break;
	      case 9:{
          dInstr |= 0;
          dInstr |= strNum(&opToken->nextToken->data[1]) << 21;
          dInstr |= 0;
          dInstr |= 0;
          dInstr |= 0;
          dInstr |= instrOp[opCode];
        }break;
        case 8:
        case 7:{
          dInstr |= instrOp[opCode] << 26;
          dInstr |= 0;
          dInstr |= 0;
          dInstr |= strNum(opToken->nextToken->data) >> 2;
        }break;
        case 18:
        case 19:{
          dInstr |= 0;
          dInstr |= 0;
          dInstr |= strNum(&opToken->nextToken->nextToken->data[1]) << 16;
          dInstr |= strNum(&opToken->nextToken->data[1]) << 11;
          dInstr |= strNum(opToken->nextToken->nextToken->nextToken->data) << 6;
          dInstr |= instrOp[opCode];
        }break;
        case 11:
        case 20:{
          char * pImm = strtok(opToken->nextToken->nextToken->data, "$");
          char * pReg = strtok(0, ")");
          dInstr |= instrOp[opCode] << 26;
          dInstr |= strNum(pReg) << 21;
          dInstr |= strNum(&opToken->nextToken->data[1]) << 16;
          dInstr |= strNum(pImm) & 0xffff;
        }break;
      }
      *(pVm->pMem->pTextCur) = dInstr;
      pVm->pMem->pTextCur += 1;
    }
  }
  printf("\nCode : %p \n", pVm->pMem->pTextBase);
  char * textBin = calloc(1,0x100000);
  int32_t textSize = 0;
  for( int32_t i = 0; i < (pVm->pMem->pTextCur-pVm->pMem->pTextBase); i++){
    char * tmp = calloc(1,32);
    int_to_bin_digit(pVm->pMem->pTextBase[i], 32, tmp);
    strcat(textBin, tmp);
    textSize += 4;
  }

  char * dataBin = calloc(1,0x100000);
  int32_t dataSize = 0;
  for( int32_t i = 0; i < (pVm->pMem->pDataCur-pVm->pMem->pDataBase); i++){
    char * tmp = calloc(1,32);
    int_to_bin_digit(pVm->pMem->pDataBase[i], 32, tmp);
    strcat(dataBin, tmp);
    dataSize += 4;
  }
  char * pBin = calloc(1,0x100000);
  strcat(pBin, int_to_bin_digit(textSize, 32, calloc(1,32)));
  strcat(pBin, int_to_bin_digit(dataSize, 32, calloc(1,32)));
  strcat(pBin, textBin);
  strcat(pBin, dataBin);
  return pBin;
}
void debugPrint(VM * pVm){
  #ifdef DEBUG
  for(BasicBlock * pBlock = pVm->headBlock; pBlock; pBlock = pBlock->nextBlock){
    printf("%s : \n", pBlock->blockName);
    uint64_t address = pBlock->address;
    for(Token * curInstr = pBlock->headInstr; curInstr; curInstr = curInstr->nextToken){
      printf("\t0x%llX :", address);
      if(curInstr->data){
        printf(" ");
        printInstr(curInstr->data);
        address += 4;
      }
      printf("\n");
    }
  }
  #endif
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_LINE 512
#define MAX_TOK 8
#define MAX_TOK_LEN 64
#define MAX_LABELS 512
#define CODE_START 0x1000

int hadError = 0;
int inFirst = 0;

typedef enum { R, I, BR, MOV, PRIV, NO_OP, OTHER } InstrType;

typedef struct {
    char name[16];
    uint32_t opcode;
    InstrType type;
    int numOps;
} InstrInfo;

InstrInfo instrTable[] = {
    {"and",0x00,R,3},{"or",0x01,R,3},{"xor",0x02,R,3},{"not",0x03,OTHER,2},
    {"shftr",0x04,R,3},{"shftri",0x05,I,2},{"shftl",0x06,R,3},{"shftli",0x07,I,2},
    {"br",0x08,BR,1},{"brr",0x09,BR,1},{"brnz",0x0b,OTHER,2},{"call",0x0c,BR,1},
    {"return",0x0d,NO_OP,0},{"brgt",0x0e,R,3},{"priv",0x0f,PRIV,4},
    {"mov",0x10,MOV,2},{"addf",0x14,R,3},{"subf",0x15,R,3},
    {"mulf",0x16,R,3},{"divf",0x17,R,3},{"add",0x18,R,3},
    {"addi",0x19,I,2},{"sub",0x1a,R,3},{"subi",0x1b,I,2},
    {"mul",0x1c,R,3},{"div",0x1d,R,3}
};
int tableSize = sizeof(instrTable)/sizeof(InstrInfo);

typedef enum { NONE, CODE, DATA } Section;

typedef struct {
    char name[256];
    uint64_t addr;
} Label;

Label lbls[MAX_LABELS];
int numLbls = 0;


void addLabelToArray(const char *lbl, uint64_t addr) {
    for(int i=0;i<numLbls;i++)
        if(strcmp(lbls[i].name,lbl)==0){
            fprintf(stderr,"Error: duplicate label '%s'\n",lbl);
            exit(1);
        }
    strcpy(lbls[numLbls].name,lbl);
    lbls[numLbls++].addr = addr;
}

uint64_t findLabelAddress(const char *lbl) {
    for(int i=0;i<numLbls;i++)
        if(strcmp(lbls[i].name,lbl)==0)
            return lbls[i].addr;
    fprintf(stderr,"Error: undefined label '%s'\n",lbl);
    exit(1);
}


void cleanLine(char *line){
    for(int i=0;line[i];i++)
        if(line[i]==';'){ line[i]='\0'; break; }
    size_t l=strlen(line);
    if(l && line[l-1]=='\n') line[l-1]='\0';
}

int splitIntoTokens(char *line,char toks[MAX_TOK][MAX_TOK_LEN]){
    for(int i=0;line[i];i++)
        if(line[i]==','||line[i]=='('||line[i]==')') line[i]=' ';
    int n=0;
    char *p=strtok(line," \t");
    while(p && n<MAX_TOK){ strcpy(toks[n++],p); p=strtok(NULL," \t"); }
    return n;
}

int getRegisterNumber(const char *r){
    if(r[0]!='r') exit(1);
    int n=atoi(&r[1]);
    if(n<0||n>31) exit(1);
    return n;
}

uint64_t convertToNumber(const char *lit){
    if(lit[0]==':'){
        if(inFirst) return 0;
        return findLabelAddress(&lit[1]);
    }
    return strtoull(lit,NULL,0);
}


void writeLdMacro(FILE *out,int rd,uint64_t v){
    fprintf(out,"\txor r%d, r%d, r%d\n",rd,rd,rd);
    for(int s=48;s>=0;s-=12){
        fprintf(out,"\taddi r%d, %llu\n",rd,(v>>s)&0xFFF);
        fprintf(out,"\tshftli r%d, 12\n",rd);
    }
    fprintf(out,"\taddi r%d, %llu\n",rd,(v>>60)&0xF);
}


int tryExpandMacro(FILE *out,char toks[MAX_TOK][MAX_TOK_LEN],int n,uint64_t *addr){
    if(!strcmp(toks[0],"push")){
        fprintf(out,"\tmov (r31)(-8), %s\n",toks[1]);
        fprintf(out,"\tsubi r31, 8\n");
        *addr+=8; return 1;
    }
    if(!strcmp(toks[0],"pop")){
        fprintf(out,"\tmov %s, (r31)(0)\n",toks[1]);
        fprintf(out,"\taddi r31, 8\n");
        *addr+=8; return 1;
    }
    if(!strcmp(toks[0],"ld")){
        int r=getRegisterNumber(toks[1]);
        uint64_t v=convertToNumber(toks[2]);
        writeLdMacro(out,r,v);
        *addr+=52; return 1;
    }
    if(!strcmp(toks[0],"in")){
        fprintf(out,"\tpriv %s, %s, r0, 3\n",toks[1],toks[2]);
        *addr+=4; return 1;
    }
    if(!strcmp(toks[0],"out")){
        fprintf(out,"\tpriv %s, %s, r0, 4\n",toks[1],toks[2]);
        *addr+=4; return 1;
    }
    return 0;
}

void printResolvedInstr(FILE *out,char toks[MAX_TOK][MAX_TOK_LEN],int n){
    fprintf(out,"\t%s",toks[0]);
    for(int i=1;i<n;i++){
        if(i==2 && toks[2][0]=='r' && n==4)
            fprintf(out,", (%s)(%s)",toks[2],toks[3]);
        else
            fprintf(out,"%s%s",(i==1?" ":" ,"),toks[i]);
    }
    fprintf(out,"\n");
}

void handleTabLine(FILE *out,char *line,Section sec,uint64_t *addr){
    char buf[MAX_LINE]; strcpy(buf,&line[1]);
    if(buf[0]==':') return;
    char toks[MAX_TOK][MAX_TOK_LEN];
    int n=splitIntoTokens(buf,toks);
    if(sec==CODE){
        if(!tryExpandMacro(out,toks,n,addr)){
            printResolvedInstr(out,toks,n);
            *addr+=4;
        }
    } else if(sec==DATA){
        fprintf(out,"\t%llu\n",(unsigned long long)convertToNumber(buf));
        *addr+=8;
    }
}

void firstPass(FILE *in,FILE *mid){
    inFirst=1;
    Section sec=NONE,last=NONE;
    uint64_t addr=CODE_START;
    char line[MAX_LINE];
    while(fgets(line,sizeof(line),in)){
        cleanLine(line);
        if(!line[0]) continue;
        if(!strcmp(line,".code")){ sec=CODE; if(last!=CODE)fprintf(mid,".code\n"); last=CODE; continue; }
        if(!strcmp(line,".data")){ sec=DATA; if(last!=DATA)fprintf(mid,".data\n"); last=DATA; continue; }
        if(line[0]==':'){ addLabelToArray(&line[1],addr); continue; }
        if(line[0]=='\t') handleTabLine(mid,line,sec,&addr);
    }
    inFirst=0;
}

void secondPass(FILE *mid,FILE *out){
    char line[MAX_LINE];
    Section sec=NONE;
    while(fgets(line,sizeof(line),mid)){
        cleanLine(line);
        if(!line[0]) continue;
        if(!strcmp(line,".code")){ sec=CODE; continue; }
        if(!strcmp(line,".data")){ sec=DATA; continue; }
        if(line[0]=='\t'){
            if(sec==CODE){
                uint32_t mc;
                char buf[MAX_LINE]; strcpy(buf,&line[1]);
                char toks[MAX_TOK][MAX_TOK_LEN];
                int n=splitIntoTokens(buf,toks);
                extern uint32_t convertToMachineCode(char toks[MAX_TOK][MAX_TOK_LEN],int n);
                mc=convertToMachineCode(toks,n);
                fwrite(&mc,4,1,out);
            } else {
                uint64_t v=convertToNumber(&line[1]);
                fwrite(&v,8,1,out);
            }
        }
    }
}


int main(int argc,char **argv){
    FILE *in=fopen(argv[1],"r");
    FILE *mid=fopen(argv[2],"w+");
    FILE *out=fopen(argv[3],"wb");
    firstPass(in,mid);
    rewind(mid);
    secondPass(mid,out);
    fclose(in); fclose(mid); fclose(out);
    return 0;
}

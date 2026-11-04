/* test muzyki AdLib 0.8.5 (host i Amiga): render wpisu MUSIC.LBX, suma probek, powrot petli. Bez float. */
#include KONW_C
#include <stdio.h>
static uint8_t *lbx; static long lbxn;
static uint32_t rd32(const uint8_t*p){return p[0]|(p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int main(int ac,char**av){
  FILE*f=fopen("MUSIC.LBX","rb"); fseek(f,0,2); lbxn=ftell(f); fseek(f,0,0); lbx=malloc(lbxn); fread(lbx,1,lbxn,f); fclose(f);
  int e=atoi(av[1]); long sek=atol(av[2]);
  uint32_t o=rd32(lbx+8+4*e), q=rd32(lbx+12+4*e);
  char bl[99]; konw_t*k=Konw_Na_Zywo(11025,lbx+o,q-o,bl,99);
  if(!k){printf("blad %s\n",bl);return 1;}
  uint32_t suma=2166136261u; static signed char b[4096]; long tot=0; int petle=0; uint32_t pop=0,start=0;
  while(tot<sek*11025){int n=Konw_Graj(k,b,4096),i; if(n<=0)break; for(i=0;i<n;i++){suma^=(uint8_t)b[i];suma*=16777619u;} tot+=n; if(k->teraz<pop){petle++; if(petle==1)start=k->teraz;} pop=k->teraz;}
  printf("wpis %d petla=%d for_tik=%lu suma=%08lx probek=%ld powrotow=%d po_powrocie=%lu\n",e,k->petla,(unsigned long)k->petla_tik,(unsigned long)suma,tot,petle,(unsigned long)start);
  return 0;}

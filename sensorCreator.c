#include <stdio.h> 
#include <stdlib.h> 
#include <ctype.h> 
#include <sys/msg.h> 
#include <string.h>
#include <semaphore.h>
#include <sys/shm.h> 
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <wait.h>
#include <stdbool.h>

#define MAX_DISP 10
#define MAX_GEST 5
#define MAX_TOTAL MAX_DISP*MAX_GEST

#define MAX_TAM_NOMBRE 20
#define TAM_BUFFER_MC 20

#define ANADIR 0
#define ELIMINAR 1
#define CONMUTA 3
#define EXIT 2
#define SIN_DEFINIR 10

typedef struct dispositivo{
  char nombre[MAX_TAM_NOMBRE];
  float consumo;
  int opciones;
  bool ON;
}disp;

pid_t pid;

int interfaz_ini(char id);
void creaDispo(sem_t*mutex,sem_t*cambios,disp *seg,sem_t*MC,disp*segServ);
void borraDispo(sem_t*mutex,sem_t*cambios,disp *seg);
void borraTodo(sem_t*mutex,sem_t*cambios,disp *seg);
void listaDispo(disp *seg);
void conmutaDispo(sem_t*mutex,sem_t*cambios,disp *seg);
char seleccionaGestor();
int iniciaRecursos(char id,sem_t**cambios,sem_t**mutex,disp**seg);
void cierraRecursos(sem_t**cambios,sem_t**mutex,disp**seg);

int main (int argc, char * argv[]){
  char id = seleccionaGestor();
  
  sem_t *cambios = NULL;
  sem_t *mutex = NULL;
  disp *seg = NULL;
  if (iniciaRecursos(id,&cambios,&mutex,&seg) == 0){
    printf("ERROR: se produjo un error al inicializar los semaforos y la memoria compartida\n");
  }
  else{
    key_t clave; 
    int shmid;

  
    sem_t *MC = NULL;
    key_t claveServ;
    disp *segServ = NULL;
    int shmidServ;
  
    claveServ = ftok(".",'M');
    if ((MC = sem_open("MC",0))==SEM_FAILED)
      printf("Error al abrir el semaforo\n");
    else{
      if((shmidServ = shmget(claveServ,(MAX_DISP)*sizeof(disp),IPC_CREAT|0660))==-1) 
	printf("No se pudo obener la id del segmento de memoria compartida\n"); 
      else{
	if((segServ=shmat(shmidServ,NULL,0))== (disp *)-1) 
	  printf("Error al mapear el segmento\n");  
	else {
	  int select;
	  int end = 1;
	  while (end){
	    select = interfaz_ini(id);
	    switch(select){
	    case(0):
	      {
		id = seleccionaGestor();
		cierraRecursos(&cambios,&mutex,&seg);
		if (iniciaRecursos(id,&cambios,&mutex,&seg)==0){
		  printf("Se ha producido un error al cambiar de gestor, cerrando\n");
		  end = 1;
		}
		break;
	      }
	    case(1):
	      {
		creaDispo(mutex,cambios,seg,MC,segServ);
		break;
	      }
	    case(2):
	      {
		borraDispo(mutex,cambios,seg);
		break;
	      }
	    case(3):
	      {
		conmutaDispo(mutex,cambios,seg);
		break;
	      }
	    case(4):
	      {
		listaDispo(seg);
		break;
	      }
	    case(5):
	      {
		borraTodo(mutex,cambios,seg);
		break;
	      }
	    case(6):
	      {
		end = 0;
		printf("Cerrando terminal\n\n");
		break;
	      }
	    }
	  }
	  shmdt(segServ);
	}
      }
      sem_close(MC);
    }
    shmdt(seg);	   
    sem_close(mutex);
    sem_close(cambios);
  }
  return(0);
}

int iniciaRecursos(char id,sem_t**cambios,sem_t**mutex,disp**seg){
  key_t clave; 
  int shmid;
  int result = 1;
  char claveMutex[] = {'m','u','t','e','x',id,'\0'};  
  char claveCambios[] = {'c','a','m','b','i','o',id,'\0'};
  char claveMemoria = id;

  if ((*cambios = sem_open(claveCambios,0)) == SEM_FAILED){
    result = 0;
  }
  else {
    if ((*mutex = sem_open(claveMutex,0)) == SEM_FAILED){
      result = 0;
    }
    else{   
      clave=ftok(".",claveMemoria); 

      if((shmid = shmget(clave,(MAX_DISP)*sizeof(disp),IPC_CREAT|0660))==-1) 
	{ 
	  result = 0;
	} 
      else{ 
	if((*seg=shmat(shmid,NULL,0))== (disp *)-1) 
	  result = 0;
      }
    }
  }
  return (result);
}

void cierraRecursos(sem_t**cambios,sem_t**mutex,disp**seg){
  shmdt(*seg);	   
  sem_close(*mutex);
  sem_close(*cambios);
}

int interfaz_ini(char id){
   int select = 0;
   printf("\nTERMINAL DE SENSORIZADO DEL GESTOR %c\n",id);
   printf("Seleccione que desea hacer:\n");
   printf("0.-Cambiar de gestor\n");
   printf("1.-Registrar nuevo sensor\n");
   printf("2.-Eliminar un sensor\n");
   printf("3.-Conmutar estado de un sensor\n");
   printf("4.-Listar todos mis sensores\n");
   printf("5.-Borrar todos los sensores del gestor\n");
   printf("6.-Salir\n\n");
   printf("Opción: ");
   scanf("%d",&select);
   printf("\n");
   while ((select>6) || (select<0)){
     printf("Por favor, introduzca una opción adecuada: ");
     scanf("%d",&select);
     printf("\n");
   }
   return (select);
}

void creaDispo(sem_t*mutex,sem_t*cambios,disp *seg,sem_t*MC,disp*segServ){
  char tipo[1000];
  float consumo;
  printf("Introduzca nombre: ");
  scanf("%999s",tipo);

  //printf("Antes del wait\n");
  sem_wait(MC);
  //printf("Despues del wait\n");
  int repetido = 0;
  int h = -1;
  for (int i = 0; i<TAM_BUFFER_MC && repetido == 0; i++){
    if (segServ[i].consumo==-1 && h == -1){
      h = i;
    }
    if (strcmp(tipo,segServ[i].nombre)==0){
      repetido = 1;
      h = i;
    }
  }
  sem_post(MC);
  
  if (repetido == 1){
    sem_wait(MC);
    consumo = segServ[h].consumo;
    sem_post(MC);
  }
  else{
    printf("El dispositivo es nuevo, introduzca el consumo: ");
    scanf("%f",&consumo);
    sem_wait(MC);
    strcpy(segServ[h].nombre,tipo);
    printf("%s",segServ[0].nombre);
    segServ[h].consumo = consumo;
    sem_post(MC);
    printf("Datos actualizados en el servidor\n");
  }
  
  sem_wait(mutex);
  int hueco = -1;
  for (int i=0; i<MAX_DISP;i++){
    if (seg[i].consumo == -1){
      hueco = i;
      i = MAX_DISP;
    }
  }
  if (hueco == -1)
    printf("No queda hueco en el gestor\n");
  else{
    strcpy(seg[hueco].nombre,tipo);
    seg[hueco].consumo = consumo;
    seg[hueco].ON = true;
    seg[hueco].opciones = ANADIR;
    sem_post(cambios);
    printf("Sensor añadido con exito");
  }
  sem_post(mutex);
  printf("\n");
}

void borraDispo(sem_t*mutex,sem_t*cambios,disp *seg){

  int hueco = -1;
  printf("+--------+---------------+---------------+---------------+\n");
  printf("| ID\t | NOMBRE\t | CONSUMO\t | WORKING\t |\n");
  printf("+--------+---------------+---------------+---------------+\n");
  for (int i = 0; i<MAX_DISP;i++){
    if (seg[i].consumo != -1)
      {
	printf("| %d\t |%13.13s\t | %10.2f\t | %6.6s\t |\n",i,seg[i].nombre,seg[i].consumo,seg[i].ON?"TRUE":"FALSE");
      }
  }
  printf("+--------+---------------+---------------+---------------+\n");
  printf("Introduzca el id del sensor a borrar: ");
  scanf("%d",&hueco);
  
  if (hueco<0||hueco>=MAX_DISP||seg[hueco].consumo==-1)
    printf("No ha seleccionado un id válido\n");
  else{
    sem_wait(mutex);
    seg[hueco].consumo = -1;
    seg[hueco].opciones = ELIMINAR;
    sem_post(cambios);
    sem_post(mutex);
    printf("Sensor borrado con exito");
  }
  printf("\n");
}

void borraTodo(sem_t*mutex,sem_t*cambios,disp *seg){

  char answer[1000];
  
  printf("¿Esta seguro de que desea borrar todos los sensores? y/n: ");
  scanf("%999s",answer);
  
  if (answer[0] =='y'){
    sem_wait(mutex);
    for (int i = 0; i<MAX_DISP; i++){
      seg[i].consumo = -1;
      seg[i].opciones = ELIMINAR;
    }
    sem_post(cambios);
    sem_post(mutex);
    printf("Sensores borrados con exito\n");
  }
  else{
    printf("Operación cancelada\n");
  }
  printf("\n");
}

void listaDispo(disp *seg){
  printf("+--------+---------------+---------------+---------------+\n");
  printf("| ID\t | NOMBRE\t | CONSUMO\t | WORKING\t |\n");
  printf("+--------+---------------+---------------+---------------+\n");
  for (int i = 0; i<MAX_DISP;i++){
    if (seg[i].consumo != -1)
      {
	printf("| %d\t |%13.13s\t | %10.2f\t | %6.6s\t |\n",i,seg[i].nombre,seg[i].consumo,seg[i].ON?"TRUE":"FALSE");
      }
  }
  printf("+--------+---------------+---------------+---------------+\n");
}

void conmutaDispo(sem_t*mutex,sem_t*cambios,disp *seg){
  int hueco = -1;
  printf("+--------+---------------+---------------+---------------+\n");
  printf("| ID\t | NOMBRE\t | CONSUMO\t | WORKING\t |\n");
  printf("+--------+---------------+---------------+---------------+\n");
  for (int i = 0; i<MAX_DISP;i++){
    if (seg[i].consumo != -1)
      {
	printf("| %d\t |%13.13s\t | %10.2f\t | %6.6s\t |\n",i,seg[i].nombre,seg[i].consumo,seg[i].ON?"TRUE":"FALSE");
      }
  }
  printf("+--------+---------------+---------------+---------------+\n");
  
  printf("Introduzca el id del sensor a conmutar: ");
  scanf("%d",&hueco);
  
  if (hueco<0||hueco>=MAX_DISP||seg[hueco].consumo==-1)
    printf("No ha seleccionado un id válido\n");
  else{
    sem_wait(mutex);
    if (seg[hueco].ON == 1){
      seg[hueco].ON = 0;
      seg[hueco].opciones = CONMUTA;
    }
    else{
      seg[hueco].ON = 1;
      seg[hueco].opciones = CONMUTA;
    }
    sem_post(cambios);
    sem_post(mutex);
    printf("Sensor conmutado con exito");
  }
  printf("\n");
}

char seleccionaGestor(){
  sem_t*gestores;
  key_t clave; 
  int *seg = NULL;
  int shmid;

  char result[1000];

  char validos[MAX_GEST];
  
  if ((gestores = sem_open("gestores",0)) == SEM_FAILED){
    printf("Error al abrir el semaforo\n");
  }
  else{   
    clave=ftok(".",'G'); 

    if((shmid = shmget(clave,(MAX_GEST)*sizeof(int),IPC_CREAT|0660))==-1) 
      { 
	printf("No se pudo obener la id del segmento de memoria compartida\n"); 
      } 
    else{ 
      if((seg=shmat(shmid,NULL,0))== (int *)-1) 
	printf("Error al mapear el segmento\n"); 
      else{
	printf("Los gestores disponibles son: \n");
	sem_wait(gestores);
	for (int i=0; i<MAX_GEST; i++){
	  if (seg[i] == 1){
	    validos[i] = i+48;
	    printf("\t- GESTOR %c\n",i+48);
	    seg[i]=1;
	  }
	}
	sem_post(gestores);
	shmdt(seg);
      }
      sem_close(gestores);
    }
  }
  //Comprobamos que el termino elegido exista
  int apto = 0;
  while (apto == 0){
    printf("Indique el gestor sobre el que desea operar: ");
    scanf("%999s",result);
    for (int i=0; i<MAX_GEST; i++){
      if (validos[i]==result[0])
	apto =1;
    }
    if (apto == 0)
      printf("Eleccion no valida\n");
  }
  return result[0];
}
								    

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

#define MAX_DISP 20
#define MAX_GEST 5
#define MAX_TAM_NOMBRE 20

#define ANADIR 0
#define ELIMINAR 1
#define EXIT 2
#define CONMUTA 3
#define SIN_DEFINIR 10

typedef struct dispositivo{
  char nombre[MAX_TAM_NOMBRE];
  float consumo;
  int opciones;
  bool ON;
}disp;

struct mymsgbuf{ 
   long mtype; 
   disp dispo; 
}; 

pid_t pid;
int autodestruction = 0;
int escr_msg(int qid,struct mymsgbuf *qbuf); 
int leer_msg(int qid,long type,struct mymsgbuf *qbuf); 

void iniciaRecursos(char id);
void eliminaRecursos(char id);
void iniciaDestruccion();
int interfaz_ini(char id);

char obtenerId();
void liberarId(char id);

int main (){
  char id = obtenerId();
  iniciaRecursos(id);
  sem_t *cambios = NULL;
  sem_t *mutex = NULL;
  sem_t *cola = NULL;
  
  key_t clave; 
  disp *seg = NULL;
  int shmid;

  key_t claveCola; 
  int msgqueue_id; 
  struct mymsgbuf qbuffer;

  if (id == '!')
    printf("Se ha producido un error, ya hay %d gestores en uso\n",MAX_GEST);
  else{
    //Preparamos las claves personalizadas del gestor;
    char claveMutex[] = {'m','u','t','e','x',id,'\0'};  
    char claveCambios[] = {'c','a','m','b','i','o',id,'\0'};
    char claveMemoria = id;
    //printf("Clave mutex: %s, clave cambios: %s, clave memoria %c\n",claveMutex,claveCambios,claveMemoria);
    claveCola=ftok(".",'q');
    if ((msgqueue_id=msgget(claveCola,IPC_CREAT|0660))==-1) 
      { 
	printf("Error al iniciar la cola\n"); 
      }
    else{
      if ((cambios = sem_open(claveCambios,0)) == SEM_FAILED){
	printf("Error al abrir el semaforo\n");
      }
      else{
	//printf("Cambios creado\n");
	if ((mutex = sem_open(claveMutex,0)) == SEM_FAILED){
	  printf("Error al abrir el semaforo\n");
	}
	else{
	  //printf("Mutex creado\n");
	  if ((cola = sem_open("cola",0)) == SEM_FAILED){
	    printf("Error al abrir el semaforo\n");
	  }
	  else{   
	    clave=ftok(".",claveMemoria); 

	    if((shmid = shmget(clave,(MAX_DISP)*sizeof(disp),IPC_CREAT|0660))==-1) 
	      { 
		printf("No se pudo obener la id del segmento de memoria compartida\n"); 
	      } 
	    else{ 
	      if((seg=shmat(shmid,NULL,0))== (disp *)-1) 
		printf("Error al mapear el segmento\n"); 
	      else{
		struct sigaction act;

		//Inicialización de memoria compartida
		sem_wait(mutex);
		for (int i=0;i<MAX_DISP;i++){
		  seg[i].consumo=-1;
		  seg[i].opciones = SIN_DEFINIR;
		}
		sem_post(mutex);
	     
		/* Se crea el proceso hijo */ 
		pid = fork(); 

		switch(pid) 
		  {
		 
		  case -1:   /* error del fork() */ 
		    perror("fork"); 
		    break;
		 
		  case 0:    /* proceso hijo o actualizador*/ 
		    /*Este proceso se va a encargar de revisar actualizaciones en los dispositivos
		      registrados y reflejarlo en el servidor, cuando reciba una señal 1  procedera a 
		      finalizar su ejecucion*/
		    act.sa_handler = iniciaDestruccion ; /*función a ejecutar*/ 
		    act.sa_flags = 0;                    /* ninguna acción especifica */ 
		    sigemptyset(&act.sa_mask);
		    sigaction(1,&act, NULL);
		    disp tabla[MAX_DISP];
		    //Entramos en el bucle infinito, a la espera de cambios en la mc, cuando los hay encontramos que ha cambiado
		    //y lo comunicamos al servidor por colas de mensajes
		    while (autodestruction==0){
		      sem_wait(cambios);
		      sem_wait(mutex);
		      for (int i=0;i<MAX_DISP;i++){
			tabla[i]=seg[i];
			seg[i].opciones = SIN_DEFINIR;
		      }
		      sem_post(mutex);

		      for (int i = 0; i<MAX_DISP; i++){
			if (tabla[i].opciones != SIN_DEFINIR){
			  qbuffer.mtype = 1;
			  qbuffer.dispo = tabla[i];
			  sem_wait(cola);
			  escr_msg(msgqueue_id,&qbuffer);
			  sem_post(cola);
			}

		      }
		      sleep(1);
		    }
		 
		    for (int i=0;i<MAX_DISP;i++)
		      seg[i].consumo=-1;
		    break;
		 
		  default:   /* padre o interfaz */ 		 
		    while (autodestruction==0){
		      int select = interfaz_ini(id);
		      switch(select){
		      case(1):
			{
			  printf("+---------------+---------------+---------------+\n");
			  printf("| NOMBRE\t| CONSUMO\t| WORKING\t|\n");
			  printf("+---------------+---------------+---------------+\n");
			  for (int i = 0; i<MAX_DISP;i++){
			    if (seg[i].consumo != -1)
			      {
				printf("|%13.13s\t|%10.2f\t|%10.10s\t|\n",seg[i].nombre,seg[i].consumo,seg[i].ON?"TRUE":"FALSE");
			    
			      }
			  }
			  printf("+---------------+---------------+---------------+\n");
			  break;
			}
		      case(2):
			{
			  autodestruction = 1;
			  liberarId(id);
			  sem_wait(mutex);
			  for (int i=0; i<MAX_DISP;i++){
			    if (seg[i].consumo != -1)
			      seg[i].opciones = ELIMINAR;
			  }
			  sem_post(mutex);
			  sem_post(cambios);
			  sleep(3);
			  kill(pid,1);
			  break;
			}
		      case(3):
			{
			  autodestruction = 1;
			  sem_wait(mutex);
			  seg[0].opciones = EXIT;
			  sem_post(mutex);
			  sem_post(cambios);
			  sleep(3);
			  kill(pid,1);
			  break;
			}	
		      }
		    }     
		    break;
		  }    	    
		shmdt(seg);
	      }
	    }
	    sem_close(mutex);
	  }
	  sem_close(cola);
	}
	sem_close(cambios);
      }
      if (pid != 0){
      eliminaRecursos(id);
      }
    }
  }
  return(0);
}

char obtenerId(){
  sem_t*gestores;
  key_t clave; 
  int *seg = NULL;
  int shmid;

  int hueco= -1;
  
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
	sem_wait(gestores);
	for (int i=0; i<MAX_GEST&& hueco == -1; i++){
	  if (seg[i] == 0){
	    hueco = i;
	    seg[i]=1;
	  }
	}
	sem_post(gestores);
	shmdt(seg);
      }
      sem_close(gestores);
    }
  }
  char result;
  if (hueco == -1)
    result = '!';
  else
    result = hueco +48;
  return result;
}

void liberarId(char id){
  sem_t*gestores;
  key_t clave; 
  int *seg = NULL;
  int shmid;

  int hueco ;
  
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
	hueco = id - 48;
	sem_wait(gestores);
	seg[hueco]=0;
	sem_post(gestores);
	shmdt(seg);
      }
      sem_close(gestores);
    }
  }
}

int escr_msg(int qid,struct mymsgbuf *qbuf) 
{ 
   int resultado;
   
   resultado=msgsnd(qid,qbuf,sizeof(disp),0);
      
   return (resultado);

}

int leer_msg(int qid,long type,struct mymsgbuf *qbuf) 
{ 
   int resultado;
   
   resultado=msgrcv(qid,qbuf,sizeof(disp),type,0); 
    
   return (resultado); 
} 

void iniciaRecursos(char id){
  char claveMutex[] = {'m','u','t','e','x',id,'\0'};  
  char claveCambios[] = {'c','a','m','b','i','o',id,'\0'};
  char claveMemoria = id;
  if (sem_open(claveCambios,O_CREAT,0600,0) != SEM_FAILED){
    //printf ("Semáforo huecos creado con éxito\n");
  }
  else{
    printf("Error en la creación del semáforo cambios\n");
  }
  if (sem_open(claveMutex,O_CREAT,0600,1) != SEM_FAILED){
    //printf ("Semáforo mutex creado con éxito\n");
  }
  else{
    printf("Error en la creación del semáforo mutex\n");
  }

      
  key_t clave; 
  int shmid;
   
  clave=ftok(".",claveMemoria); 

  if((shmid = shmget(clave,(MAX_DISP)*sizeof(disp),IPC_CREAT|IPC_EXCL|0660))==-1) 
    { 
      printf("El segmento de memoria compartida ya existe\n"); 
    } 
  else{ 
    //printf("Nuevo segmento creado\n");    
  }
}

void eliminaRecursos(char id){
  char claveMutex[] = {'m','u','t','e','x',id,'\0'};  
  char claveCambios[] = {'c','a','m','b','i','o',id,'\0'};
  char claveMemoria = id;
  if (sem_unlink(claveCambios) == 0){
    //printf("El semáforo cambios se eliminó con éxito\n");
  }
  else{
    printf("Error al eliminar el semáforo cambios\n");
  }
  if (sem_unlink(claveMutex) == 0){
    //printf("El semáforo mutex se eliminó con éxito\n");
  }
  else{
    printf("Error al eliminar el semáforo mutex\n");
  }
     
  key_t clave; 
  int shmid;
   
  clave=ftok(".",claveMemoria); 

  if((shmid = shmget(clave,(MAX_DISP)*sizeof(disp),IPC_CREAT|0660))==-1) 
    { 
      printf("No se ha podido obtener el id del segmento de meoria\n"); 
    } 
  else{ 
    shmctl(shmid,IPC_RMID,NULL);
    //printf("Segmento borrado con éxito\n");   
  }   
}

void iniciaDestruccion(void){
  autodestruction = 1;
}

int interfaz_ini(char id){
   int select = 0;
   printf("\nTERMINAL DEL GESTOR %c\n",id);
   printf("Seleccione que desea hacer:\n");
   printf("1.-Listar mis dispositivos\n");
   printf("2.-Eliminar gestor y dispositivos del servidor\n");
   printf("3.-Eliminar gestor y cerrar sevidor\n\n");
   printf("Opción: ");
   scanf("%d",&select);
   printf("\n");
   while ((select>3) || (select<1)){
     printf("Por favor, introduzca una opción adecuada: ");
     scanf("%d",&select);
     printf("\n");
   }
   return (select);
}

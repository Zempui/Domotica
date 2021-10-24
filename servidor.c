#include <stdio.h> 
#include <stdlib.h> 
#include <ctype.h> 
#include <sys/msg.h> 
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <stdbool.h>

#define TAM_BUFFER_MC 20
#define NUM_SEMAFOROS 2
#define NUM_PREDEFINIDOS 6
#define MAX_TAM_NOMBRE 20

#define MAX_DISP 20
#define MAX_GEST 5
#define MAX_TOTAL MAX_DISP*MAX_GEST

#define ANADIR 0
#define ELIMINAR 1
#define EXIT 2
#define CONMUTA 3
#define SIN_DEFINIR 10


/**
 * Programa servidor de domótica.
 * Su función es servir de central de control para los diversos clientes,
 * debe crear la cola de mensajes y la zona de memoria compartida a utilizar,
 * así como imprimir por pantalla una tabla que se vaya actualizando
 * automáticamente según se vayan conectando o desconectando dispositivos.
 */



/**
 * Estructura electodoméstico.
 * Contiene el nombre y el consumo de cada electrodoméstico que se defina.
 */
typedef struct{
  char nombre[MAX_TAM_NOMBRE];
  float consumo;
  int opciones;
  bool ON;
}disp;

/**
 * Estructura msgbuf
 * Contiene el tipo de mensajes que se van a enviar a la cola de mensajes.
 */
struct msgbuf{
  long mtype;
  disp dispo;
};

void imp_Tabla(disp tabla[MAX_TOTAL],disp*MC);
void iniciaRecursos();
void eliminaRecursos();
int escr_msg(int qid,struct msgbuf *qbuf);
int leer_msg(int qid,long type,struct msgbuf *qbuf);

int main(){
  eliminaRecursos();
  iniciaRecursos();
 
  key_t clave; 
  disp *seg = NULL;
  int shmid;
  sem_t *semMC;
  
  key_t claveCola; 
  int msgqueue_id; 
  struct msgbuf qbuffer; 
   
  clave=ftok(".",'M'); 
  if ((semMC = sem_open("MC",0))==SEM_FAILED)
    printf("Error al abrir el semaforo\n");
  else
    {
      if((shmid = shmget(clave,(TAM_BUFFER_MC)*sizeof(disp),IPC_CREAT|0660))==-1) 
	{ 
	  printf("No se pudo obener la id del segmento de memoria compartida\n"); 
	} 
      else{ 
	if((seg=shmat(shmid,NULL,0))== (disp *)-1) 
	  printf("Error al mapear el segmento\n"); 
	else{
	  claveCola = ftok(".",'q');
	  if ((msgqueue_id=msgget(claveCola,IPC_CREAT|0660))==-1)
	    printf("Error al iniciar la cola\n");
	  else{
	      //Electrodomesticos predefinidos
	      disp predef[NUM_PREDEFINIDOS];
	      char *nombreEd[NUM_PREDEFINIDOS]={"bombilla","ventilador","lavadora","lavavajillas","TV","nevera"};
	      float consumoEd[NUM_PREDEFINIDOS]={0.06,0.3,1.5,2.0,0.25,0.35};

	      for(int i=0;i<NUM_PREDEFINIDOS;i++)
		{
		  strcpy(predef[i].nombre,nombreEd[i]);
		  predef[i].consumo=consumoEd[i];
		  predef[i].opciones=SIN_DEFINIR;
		}
	      //Electrodomestico vacío
	      disp vacio;
	      vacio.consumo=-1;
	      vacio.opciones=SIN_DEFINIR;
	      vacio.ON=false;
	      strcpy(vacio.nombre,"void");

	      //Añadimos los electrodomesticos predifinidos para inicializar la memoria compartida
	      sem_wait(semMC);
	      for (int i=0; i<NUM_PREDEFINIDOS; i++){
		seg[i] = predef[i];
	      }
	      for (int i=NUM_PREDEFINIDOS; i<TAM_BUFFER_MC; i++)
		seg[i]=vacio;
	      sem_post(semMC);

	      //Creamos una tabla local que almacenara los dispositivos de la casa y la inicailizamos vacía
	      disp tablaDispos[MAX_TOTAL];
	      for (int i=0;i<MAX_TOTAL;i++){
		tablaDispos[i] = vacio;
	      }
	      //Interfaz de bienvenida para el usuario:
	      printf(".---.  _                                _    .-.      \n: .; ::_;                              :_;   : :      \n:   .'.-. .--. ,-.,- .-..-. .--. ,-.,-..-. .-' : .--. \n: .; :: :' '_.': ,. :: `; :' '_.': ,. :: :' .; :' .; :\n:___.':_;`.__.':_;:_;`.__.'`.__.':_;:_;:_;`.__.'`.__.'\n\n");
	      //El servidor entrará en un bucle que no finalizará hasta que reciba
	      //esa instrucción de un cliente.
	      bool exit = false;
	      while(exit==false)
		{
		  leer_msg(msgqueue_id,0,&qbuffer);
		  switch(qbuffer.dispo.opciones)
		    {

		      // Para añadir un nuevo electrodoméstico, lo escribimos en la tabla local y pedimos que nos la imprima actualizada
		    case ANADIR:{
		      int hueco = -1;
		      for (int i=0;i<MAX_TOTAL && hueco == -1; i++){
			if (tablaDispos[i].consumo == -1)
			  hueco = i;
			
		      }
		      if (hueco == -1)
			printf("ERROR: no queda hueco en la tabla\n");
		      else{
			tablaDispos[hueco] = qbuffer.dispo;
			tablaDispos[hueco].opciones = SIN_DEFINIR;
			imp_Tabla(tablaDispos,seg);
		      }
		      break;
		    }
		      // Para eliminar un dispositivo de la lista, identifica su nombre y busca si hay algún dispositivo
		      // de ese tipo almacenado, si es así, sustituye su espacio en la cola por un electrodomestico del tipo
		      // "vacio"
		    case ELIMINAR:{
		      bool found = false;
		      for (int i=0;i<MAX_TOTAL&&found==false;i++)
			{
			  if(strcmp(tablaDispos[i].nombre,qbuffer.dispo.nombre)==0&&tablaDispos[i].ON==qbuffer.dispo.ON)
			    {
			      found=true;
			      tablaDispos[i]=vacio;
			    }
			}
		      imp_Tabla(tablaDispos,seg);
		      found=false;
		      break;
		    }
		    case CONMUTA:{
		      bool found = false;
		      for (int i=0;i<MAX_TOTAL&&found==false;i++)
			{
			  if(strcmp(tablaDispos[i].nombre,qbuffer.dispo.nombre)==0)
			    {
			      found=true;
			      tablaDispos[i].ON=qbuffer.dispo.ON;
			    }
			}
		      imp_Tabla(tablaDispos,seg);
		      found=false;
		      break;
		    }
		      // Para cerrar el programa, se elimina el contenido de la MC y de la cola, posteriormente, se sale
		      // del bucle y se libera la memoria reservada.
		    case EXIT:
		      exit=true;
		      printf("Cerrando servidor\n");

		      break;
	      
		    default:
		      printf("Algo raro ha sucedido...\n%s.%s=%d",qbuffer.dispo.nombre,"opciones",qbuffer.dispo.opciones);
		      break;
		    }
	      }
	    }
	  shmdt(seg);
	}
      }
      sem_close(semMC);
    }
  eliminaRecursos();
  return(0);
}


  
/**
 * Función imp_tabla
 * Imprime por pantalla una tabla con todos los dispositivos de cada tipo y el consumo de cada uno de ellos,
 * así como el consumo total.
 */
void imp_Tabla(disp tabla[MAX_TOTAL],disp *MC)
{
  //abro el semáforo MC
  sem_t *sem_MC=NULL;
  if((sem_MC=sem_open("MC", 0600))==NULL)
    printf("ERROR: no se ha podido abrir el semaforo en imp_Tabla\n");
  else{
    //Calculamos cuantos tipos de dispositivos tenemos en la memoria compartida
    sem_wait(sem_MC);
    int elementos =0;
    for (int i = 0; i<TAM_BUFFER_MC; i++){
      if (MC[i].consumo != -1)
	elementos++;
    }
    sem_post(sem_MC);
  
    //inicializamos el contador a 0, se ocupará de decir cuántos elementos de cada tipo hay.
    int contador[elementos];
    for(int i=0;i<elementos;i++)
      {
	contador[i]=0;
      }
    printf("+---------------+---------------+---------------+---------------+\n");
    printf("|Nombre\t\t|Num. Disp.\t|Consumo I(kWh)\t|Consumo T(kWh)\t|\n");
    printf("+---------------+---------------+---------------+---------------+\n");
    
    //Repetiremos para cada elemento de la tabla un algoritmo que busca en memoria compartida al elemento cuyo nombre coincida.
    for (int i=0;i<MAX_TOTAL;i++)
      {	
	for (int j=0;j<elementos;j++)
	  {
	    if (sem_wait(sem_MC)!=0)
	      printf("ERROR: el semáforo no ha podido ser bajado\n");
	    if(strcmp(tabla[i].nombre,MC[j].nombre)==0&&tabla[i].ON==1)
	      contador[j]++;
	    if (sem_post(sem_MC)!=0)
	      printf("ERROR: el semáforo no ha podido ser subido\n");
	  }
      }
    
    
    //imprimimos los datos obtenidos de la tabla.
    float consumoTotal=0;
    for (int i=0;i<elementos;i++)
      {
	consumoTotal += contador[i]*MC[i].consumo;
	if (sem_wait(sem_MC)!=0)
	  printf("ERROR: el semáforo no ha podido ser subido\n");
	printf("|%13.13s\t|%13d\t|%9.3f\t|%9.3f\t|\n",MC[i].nombre,contador[i],MC[i].consumo,(contador[i]*MC[i].consumo));
	if (sem_post(sem_MC)!=0)
	  printf("ERROR: el semáforo no ha podido ser bajado\n");
      }
    printf("+---------------+---------------+---------------+---------------+\nConsumo total: %9.3f KWh\n\n\n\n",consumoTotal);
  } 
}

void iniciaRecursos(){
  //Creamos el semaforo
  if (sem_open("MC",O_CREAT,0600,1) != SEM_FAILED){
    // printf ("Semáforo MC creado con éxito\n");
  }
  else{
    printf("Error en la creación del semáforo MC\n");
  }
    //Creamos el semaforo
  if (sem_open("cola",O_CREAT,0600,1) != SEM_FAILED){
    //printf ("Semáforo cola creado con éxito\n");
  }
  else{
    printf("Error en la creación del semáforo cola\n");
  }
     
      
  key_t clave; 
  int shmid;
  //Creamos la memoria compartida
  clave=ftok(".",'M'); 

  if((shmid = shmget(clave,(TAM_BUFFER_MC)*sizeof(disp),IPC_CREAT|IPC_EXCL|0660))==-1) 
    { 
      printf("El segmento de memoria compartida ya existe\n"); 
    } 
  else{ 
    //printf("Nuevo segmento creado\n");    
  }

  //Memoria compartida y semaforo para la asignacion de gestores
  
    //Creamos el semaforo
  if (sem_open("gestores",O_CREAT,0600,1) != SEM_FAILED){
    //printf ("Semáforo gestores creado con éxito\n");
  }
  else{
    printf("Error en la creación del semáforo gestores\n");
  }
  key_t claveGest; 
  int shmidGest;
  //Creamos la memoria compartida
  claveGest=ftok(".",'G'); 

  shmidGest = shmget(claveGest,(MAX_GEST)*sizeof(int),IPC_CREAT|IPC_EXCL|0660); 
  
  int *segGest = shmat(shmidGest,NULL,0);
  for (int i=0; i<MAX_GEST; i++)
    {
      segGest[i]= 0;
      //printf("%d\n",segGest[i]);
    }
  shmdt(segGest);
  //printf("Nuevo segmento creado\n");    
}

void eliminaRecursos(){
  //Cerramos el semaforo
  if (sem_unlink("cola") == 0){
    //printf("El semáforo cola se eliminó con éxito\n");
  }
  else{
    //printf("Error al eliminar el semáforo cola\n");
  }

    //Cerramos el semaforo
  if (sem_unlink("MC") == 0){
    //printf("El semáforo MC se eliminó con éxito\n");
  }
  else{
    //printf("Error al eliminar el semáforo MC\n");
  }
     
  key_t clave; 
  int shmid;

  //Cerramos la memoria compartida
  clave=ftok(".",'M'); 

  if((shmid = shmget(clave,(TAM_BUFFER_MC)*sizeof(disp),IPC_CREAT|0660))==-1) 
    { 
      printf("No se ha podido obtener el id del segmento de memoria\n"); 
    } 
  else{ 
    shmctl(shmid,IPC_RMID,NULL);
    //printf("Segmento borrado con éxito\n");   
  }

  //Cerramos la cola
  int msgqueue_id;
  msgqueue_id=msgget(clave,IPC_CREAT|0660);
  msgctl(msgqueue_id,IPC_RMID,NULL);

  
  //Memoria compartida y semaforo para la asignacion de gestores
  
  //Eliminamos el semaforo
  if (sem_unlink("gestores")== 0){
    // printf ("Semáforo gestores eliminado con éxito\n");
  }
  else{
    //printf("Error en la eliminacion del semáforo gestores\n");
  }
  key_t claveGest; 
  int shmidGest;
  //Eliminamos la memoria compartida
  claveGest=ftok(".",'G'); 

  if((shmidGest = shmget(claveGest,(MAX_GEST)*sizeof(int),IPC_CREAT|0660))==-1) 
    { 
      printf("El segmento de memoria gestores ya existe\n"); 
    } 
  else{
    shmctl(shmidGest,IPC_RMID,NULL);
    //printf("Segmento gestores borrado con éxito\n");
    
  }
}


/**
 * Función escr_msg
 * Sirve para escribir un mensaje en la cola, en este caso, para añadir un dispositivo a la lista
 */
int escr_msg(int qid,struct msgbuf *qbuf){
  int resultado;
  resultado=msgsnd(qid,qbuf,sizeof(disp),0);
  return resultado;
}


/**
 * Función leer_msg
 * Sirve para leer un mensaje de la cola, en este caso, para obtenerlo de la lista
 */
int leer_msg(int qid,long type,struct msgbuf *qbuf){
  int resultado;
  resultado=msgrcv(qid,qbuf,sizeof(disp),type,0);
  return resultado;
}

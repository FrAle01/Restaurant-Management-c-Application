#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define SCNu16   "d"
// decimal scanf format for int16_t

#define BUFF_DIM 1048
#define MAX_TABLE 3
#define CL_DIM 49 // dim messaggio protocollo con cliente prenotazione


struct booked{
    char date[8+1];
    int hour;
    char tb[4];         // tavolo
    uint16_t bookId;    // codice prenotazione
    char name[25];
    int seat;           // posti prenotati
    char timestamp[20]; // dd-mm-yyyy hh:mm:ss
    int attendance;     // per indicare quando il cliente si presenta alla prenotazione
};

struct device{
    int fd;
    char type;
    char addr[16];
};

struct tavolo{
    char tb[4];
    char room[7];
    char descr[20];
    int chair;          // posti del tavolo
    int avaiable;       // tavolo libero o no
};
struct tavolo tav[MAX_TABLE];       // tavoli presenti nel ristorante

struct comanda{
    char orderId[5];    // codice comanda
    char tbl[4];        // codice tavolo
    int tableFD;        // socket del tavolo (per notificare stato comanda)
    char order[75];     // comnda effettiva
    char status;        // stato comanda ('a' in attesa, 'p' in preparazione, 's' in servizio)
};

void get_ts(char* arg){     // funzione che inserisce in arg il timestamp
    time_t raw;
    struct tm*  ts;

    time(&raw);
    ts = localtime(&raw);
    sprintf(arg, "%02d-%02d-%d_%02d:%02d:%02d", (*ts).tm_mday, (*ts).tm_mon, (*ts).tm_year+1900, (*ts).tm_hour, (*ts).tm_min, (*ts).tm_sec);
}

void inserisci_tavoli(FILE* fd){
    int i;
    for(i=0; i < MAX_TABLE; i++){
        fscanf(fd, "%s %s %s %d", tav[i].tb, tav[i].room, tav[i].descr, &tav[i].chair);
    }
}

void deleteLine(FILE* src, const int linenum){
    FILE *temp;
    char aux[BUFF_DIM];
    int count = 1;

    temp = fopen("delete.temp", "w");

    while ((fgets(aux, BUFF_DIM, src)) != NULL){
      if (linenum != count)
         fputs(aux, temp);
      count++;
   }

   fclose(temp);
}

int main (int argnum, char** arg) {

    int listener, ret, new_sd;
    uint32_t cl_len;
    struct sockaddr_in my_addr, cl_addr;
    int port;
    
    char buffer[BUFF_DIM];
    struct device new_dev, dev;         // per segnare la tipologia del device connesso
    char option;                        // per l'operazione da eseguire per il client
    struct booked request, reserv;      // per protocollo con client prenotazione
    struct booked attend;               // per controllo su table device
    uint16_t reservationIds = rand()%1000 + 2001;   // genera un valore casuale tra 2001 e 3000 da cui inizieranno i codici di prenotazione assegnati
    uint16_t td_value;


    fd_set master_r;     // creo insiemi di descrittori
    fd_set read_fds;     // creo insiemi di descrittori
    int fdmax;

    FILE *devices;      // file dei dispositivi
    FILE *tavoli;       // file dei tavoli del ristorante
    FILE *prenotazioni; // file prenotazioni confermate

    FD_ZERO(&master_r); // pulizia insiemi di descrittori    
    FD_ZERO(&read_fds); // pulizia insiemi di descrittori   

    tavoli = fopen("tables.txt", "r");  // il file dei tavoli è preesistente e definito dalle caratteristiche del ristorante  
    if(tavoli != NULL){
        inserisci_tavoli(tavoli);           // assegno i tavoli esistenti (nel file) alle variabili
        fclose(tavoli); 
    }else{
        printf("File tables.txt non esistente\n");
        exit(1);
    }
    listener = socket(AF_INET, SOCK_STREAM, 0); // creazione socket
    port = (argnum == 2) ? atoi(arg[1]) : 4242;

    // creazione indirizzo
    memset(&my_addr, 0, sizeof(my_addr));  // PULIZIA
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &my_addr.sin_addr);

    ret = bind(listener, (struct sockaddr*)&my_addr, sizeof(my_addr)); // associazione indirizzo a socket

        
    ret = listen(listener, 10);
    if (ret == -1){
            perror("Errore sulla bind: ");
            exit(1);
    }
    FD_SET(listener, &master_r);        // metto il socket di ascolto nell'insieme totale
    FD_SET(fileno(stdin), &master_r);   // metto stdin nell'insieme master
    fdmax = listener;

    while(1){  
        int i=0;
        read_fds = master_r;
        select(fdmax+1, &read_fds, NULL, NULL, NULL);
        for(; i <= fdmax; i++){
            
            if(FD_ISSET(i, &read_fds)){ // socket pronto perché rimasto in insieme read_fds
                if(i == listener){      // ricevuta richiesta su socket di ascolto
                    cl_len = sizeof(cl_addr);
                    new_sd = accept(listener, (struct sockaddr*)&cl_addr, &cl_len);   // accept di richieste
                    FD_SET(new_sd, &master_r);
                    if(new_sd > fdmax){ fdmax = new_sd; }

                    recv(new_sd, (void*)&new_dev.type, 1, 0);

                    new_dev.fd = new_sd;
                    inet_ntop(AF_INET, &cl_addr.sin_addr, new_dev.addr, cl_len);
                    // inserisco il device appena collegato nel file dei dispositivi
        printf("%d %c %s\n", new_dev.fd, new_dev.type, new_dev.addr);
                    devices = fopen("devices.txt", "a");
                    fprintf(devices, "%d %c %s\n", new_dev.fd, new_dev.type, new_dev.addr);
                    fclose(devices);


                }else{ //  Gestione richieste su socket connessi

                    char disp=' ';

                    // recupero il tipo ti dispositivo collegato al socket interessato
                    devices = fopen("devices.txt", "r");
                    while(!feof(devices)){
                        fscanf(devices, "%d %c %s", &dev.fd, &dev.type, dev.addr);
                        if (dev.fd == i){
                            disp = dev.type;
                            break;
                        }
                    }                  
                    fclose(devices);

                    switch(disp){
                        case 'C':
                            memset(buffer, '\0', BUFF_DIM); // pulizia buffer
                            ret = recv(i, (void*)buffer, CL_DIM, 0);
                            if(ret == 0){       // Client si è disconnesso
                                int line = 0;
                                close(i);
                                FD_CLR(i, &master_r);
                                devices = fopen("devices.txt", "r+");
                                while(!feof(devices)){
                                    fscanf(devices, "%d %c %s", &dev.fd, &dev.type, dev.addr);
                                    line++;
                                    if (dev.fd == i){
                                        fseek(devices, 0, SEEK_SET);
                                        deleteLine(devices, line);
                                        break;
                                    }
                                }                  
                                fclose(devices);
                                remove("devices.txt");
                                rename("delete.temp", "devices.txt");
                                continue;
                            }
                            sscanf(buffer,"%c", &option);
                            if(option == 'F'){           // client ha richiesto la disponibilità
                                int j,k;
                                int conta;
                                uint16_t dim, dim_net;
                                char disponibilita[BUFF_DIM];
                                sscanf(buffer, "%c %s %d %s %02d", &option, request.name, &request.seat, request.date, &request.hour);
                                for(j = 0; j<MAX_TABLE; j++){   
                                    if(tav[j].chair >= request.seat){        // metto disponibili i tavoli con almeno i posti richiesti dal client
                                        tav[j].avaiable = 1;
                                    }else{
                                        tav[j].avaiable = 0;                // non disponibili quelli con pochi posti
                                    }
                                }
                                prenotazioni = fopen("reservation.txt", "r");
                                if(prenotazioni != NULL){   // file prenotazioni deve esistere per controllare se ci sono gia prenotazioni effettuate
                                    while(!feof(prenotazioni)){
                                        fscanf(prenotazioni,"%s %02d %s %*d %*s %*d %*s %*d", reserv.date, &reserv.hour, reserv.tb);
                                        if(strcmp(reserv.date, request.date) == 0 && reserv.hour==request.hour){
                                            for(j = 0; j < MAX_TABLE; j++){
                                                if(strcmp(reserv.tb, tav[j].tb)==0){
                                                    tav[j].avaiable = 0;
                                                }
                                            }
                                        }
                                    }
                                    fclose(prenotazioni);
                                }
                                k = 1;
                                conta = 0;
                                memset(disponibilita, '\0', BUFF_DIM);
                                for(j = 0; j< MAX_TABLE; j++){
                                    if(tav[j].avaiable == 1){
                                        ret = sprintf(disponibilita + conta,"%d) %s %s %s\n", k, tav[j].tb, tav[j].room, tav[j].descr);
                                        conta = conta +ret;
                                        k++;
                                    }
                                }
                                if(k == 1){     // non ci sono tavoli disponibili
                                    dim = 0;    // non devo controllare l'endianess di 0
                                    send(i, (void*)&dim, sizeof(uint16_t), 0);
                                    continue;
                                }
                                dim = sizeof(disponibilita);
                                dim_net= htons(dim);
                                send(i, (void*)&dim_net, sizeof(uint16_t), 0);
                                send(i, (void*)disponibilita, dim, 0);


                            }else if(option == 'B'){     // client vuole prenotare
                                int stillFree = 1;
                                sscanf(buffer, "%c %s %02d %s %s %d", &option, request.date, &request.hour, request.tb, request.name, &request.seat);
                                prenotazioni = fopen("reservation.txt", "r");
                                if(prenotazioni != NULL){ 
                                    while(!feof(prenotazioni)){
                                        fscanf(prenotazioni,"%s %02d %s %*d %*s %*d %*s %*d", reserv.date, &reserv.hour, reserv.tb);
                                        if(strcmp(reserv.date, request.date) == 0 && reserv.hour==request.hour && strcmp(reserv.tb, request.tb)==0){ // Il tavolo non è più disponibile, qualcuno lo ha prenotato già (possibile per concorrenza FIND)
                                            stillFree = 0;
                                            request.bookId  = 0;
                                            request.bookId = htons(request.bookId);
                                            send(i, (void*)&request.bookId, sizeof(uint16_t), 0);
                                            break;
                                        }
                                    }
                                    fclose(prenotazioni);
                                }
                                prenotazioni = fopen("reservation.txt", "a");
                                if(stillFree){ // prenotazione possibile, la aggiungo al file

                                    get_ts(request.timestamp);          // creo il timestamp per la prenotazione
                                    request.bookId = reservationIds++;  // assegno il codice di prenotazione
                                    // salvo i valori della prenotazione nel file
                                    fprintf(prenotazioni,"%s %d %s %d %s %d %s 0\n", request.date, request.hour, request.tb, request.bookId, request.name, request.seat, request.timestamp);    // zero nel campo 'attendance'
                                    request.bookId = htons(request.bookId);                            
                                    send(i, (void*)&request.bookId, sizeof(uint16_t), 0);
                                }
                                fclose(prenotazioni);
                            } 

                        break;
                        case 'T':
                            
                            ret = recv(i, (void*)&td_value, sizeof(uint16_t), 0);
                            if(!ret){
                                int line = 0;
                                close(i);
                                FD_CLR(i, &master_r);
                                devices = fopen("devices.txt", "r+");
                                while(!feof(devices)){
                                    fscanf(devices, "%d %c %s", &dev.fd, &dev.type, dev.addr);
                                    line++;
                                    if (dev.fd == i){
                                        fseek(devices, 0, SEEK_SET);
                                        deleteLine(devices, line);
                                        break;
                                    }
                                }                  
                                fclose(devices);
                                remove("devices.txt");
                                rename("delete.temp", "devices.txt");
                                continue;
                            }
                            td_value = ntohs(td_value);
                    printf("Da table: %d\n", td_value);
                            if(td_value > 2000){    // gestione del codice di prenotazione inviato 
                                prenotazioni = fopen("reservation.txt", "r+");
                                if(prenotazioni != NULL){
                                    while(!feof(prenotazioni)){
                                        fscanf(prenotazioni, "%*s %*d %s %d %*s %*d %*s %d", attend.tb, &attend.bookId, &attend.attendance);
                    printf("%s %d %d\n", attend.tb, attend.bookId, attend.attendance);
                                        if(td_value == attend.bookId && attend.attendance == 0){    // prenotazione esistente e ancora non usata

                    printf("Found %s %d %d\n", attend.tb, attend.bookId, attend.attendance);
                                            uint16_t n_tav;
                                            sscanf(attend.tb, "T%d", &n_tav);
                                            n_tav = htons(n_tav);
                                            send(i, (void*)&n_tav, sizeof(uint16_t), 0);
                                            fseek(prenotazioni, -1, SEEK_CUR);  // metto il puntatore prima del valore di attendance
                                            fprintf(prenotazioni, "1");     // metto 'attendance' a 1 indicando che un cliente ha usato quella prenotazione
                    printf("codice trovato\n");
                                            break;
                                        }
                                        if(td_value == attend.bookId && attend.attendance == 1){    // prenotazione esistente ma già usata
                                            uint16_t used = MAX_TABLE +1;
                                            used = htons(used);
                                            send(i, (void*)&used, sizeof(uint16_t), 0);
                    printf("codice già usato\n");
                                            break;
                                        }                                    
                                    }
                                    if(feof(prenotazioni)){ // terminato il file ma non trovata la prenotazione
                    printf("Prenotazione non trovata\n");
                                        uint16_t not_found = 0;
                                    //  not_found = htons(not_found);   non necessario perché endianess non importante per 0
                                        send(i, (void*)&not_found, sizeof(uint16_t), 0);

                                    }
                                    fclose(prenotazioni);
                                }
                            }
                            if(td_value <= 2000){   // arrivo nuova comanda e dim msg data da td_value
                                
                                memset(buffer, '\0', BUFF_DIM);         // pulizia buffer
                                recv(i, (void*)buffer, td_value, 0);    // ricevo la comanda

                            }

                        break;
                    /*
                        case 'K':
                        break;*/
                        default:
                            memset(buffer, '\0', BUFF_DIM);
                            fgets(buffer, BUFF_DIM, stdin);
                            printf("%s\n", buffer);
        
                        break;
                    }
                    
                    
                    
                }
            }


        }
        


        


    }
        

        

    exit(0);
}
    

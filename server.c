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

#define PRIu16 "hu"
// printf format for uint16_t

#define BUFF_DIM 1024
#define MAX_TABLE 10
#define CL_DIM 49 // dim messaggio protocollo con cliente prenotazione
#define STAT_UPDATE_DIM 8

#define ORDINATIONS "ordinations.txt"
#define CONN_DEVICES "devices.txt"
#define RESERVATIONS "reservation.txt"
#define TABLES "tables.txt"



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
int sock_tb[MAX_TABLE];             // array associativo TavoloId <-> socket

struct comanda{
    char tbl[4];        // codice tavolo
    char orderId[5];    // codice comanda
    char ordered[75];     // comanda effettiva
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
    int j;
    for(j=0; j < MAX_TABLE; j++){
        fscanf(fd, "%s %s %s %d", tav[j].tb, tav[j].room, tav[j].descr, &tav[j].chair);
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

int tbtosock(char *cod_tav){    // dato un codice di tavolo ne restituisce il socket connesso
    int n, sock;
    sscanf(cod_tav, "T%d", &n);
    if(n <= MAX_TABLE){
        sock = sock_tb[--n];
        return sock;
    }else{
        return -1;  // codice tavolo non esistente
    }
}

int socktotb(int sock){         // dato un socket restituisce, se esiste, il codice del tavolo associato
    int j;
    for(j = 0; j< MAX_TABLE; j++){
        if(sock_tb[j] == sock){
            return ++j;
        }
    }
    return -1;      // socket non associato a td
}

void broadcat_kd(uint16_t orders, int exeption){
    FILE *dvcs;
    struct device devk;
    dvcs = fopen(CONN_DEVICES, "r");        // comunico a tutti gli altri kd che una comanda è stata accettata
    if(dvcs != NULL){
        while(!feof(dvcs)){
            fscanf(dvcs, "%d %c %*s", &devk.fd, &devk.type);
            if(feof(dvcs)){
                    break;
                }
                if(devk.type == 'K' && devk.fd != exeption){ 
                    uint16_t wait_com = orders + 2000;
                    wait_com = htons(wait_com);
                    send(devk.fd, (void*)&wait_com, sizeof(uint16_t), 0);
                }
        }
        fclose(dvcs);
    }
}

char* statostr(char letter){
    switch(letter){
        case 'a':
            return "<in attesa>\0";
        break;
        case 'p':
            return "<in preparazione>\0";
        break;
        case 's':
            return "<in servizio>\0";
        break;
        default:
            return "!STATO non riconosciuto!\0";
        break;
    }

}

void printOrder(char* ord, char* tab, char* comId, char st, char all){
    int count = 0;
    int ret = -1;
    char piatto[4];
    char c_num[3];
    int num;    

    if(all){
        printf("%s %s %s\n", comId, tab, statostr(st));
    }
    if(st == ' ' && !all){
        printf("%s %s\n", comId, tab);
    }
    if (!strcmp(tab, " ") && !all){
        printf("%s %s\n", comId, statostr(st));
    }

    while(ord[ret] != '\n'){     // parsing della stringa carattere per carattere
        count = 0;
        ret++;
        while(ord[ret]!= '-'){ // leggo codice piatto
            piatto[count] = ord[ret];
            count++;
            ret++;
        }
        ret++;
        piatto[count] = '\0';
        printf("%s ", piatto);
        count = 0;
        while(ord[ret] != ' ' && ord[ret] != '\n'){   // leggo la quantità
            c_num[count] = ord[ret];
            count++;
            ret++;
        }
        c_num[count] = '\0';
        num = atoi(c_num);  // quantità da ASCII a int
        printf("%d\n", num);
    }
    fflush(stdout);
}

int main (int argnum, char** arg) {

    int listener, ret, new_sd;
    socklen_t cl_len;
    struct sockaddr_in my_addr, cl_addr;
    int port;
    
    char buffer[BUFF_DIM];
    struct device new_dev, dev;         // per segnare la tipologia del device connesso
    char option;                        // per l'operazione da eseguire per il client
    struct booked request, reserv;      // per protocollo con client prenotazione
    struct booked attend;               // per controllo su table device
    uint16_t reservationIds = rand()%1000 + 2001;   // genera un valore casuale tra 2001 e 3000 da cui inizieranno i codici di prenotazione assegnati
    uint16_t td_value, kd_value;
    uint16_t  w_order = 0;              // numero ordini in attesa
    int j;
    char typed[5];                     // opzione in input 'stat' o 'stop'


    fd_set master_r;     // creo insiemi di descrittori
    fd_set read_fds;     // creo insiemi di descrittori
    int fdmax;

    FILE *devices;      // file dei dispositivi
    FILE *tavoli;       // file dei tavoli del ristorante
    FILE *prenotazioni; // file prenotazioni confermate
    FILE *comande;      // file comande da tutti i td

    FD_ZERO(&master_r); // pulizia insiemi di descrittori    
    FD_ZERO(&read_fds); // pulizia insiemi di descrittori   

    tavoli = fopen(TABLES, "r");  // il file dei tavoli è preesistente e definito dalle caratteristiche del ristorante  
    if(tavoli != NULL){
        inserisci_tavoli(tavoli);           // assegno i tavoli esistenti (nel file) alle variabili
        fclose(tavoli); 
    }else{
        printf("File tables.txt non esistente\n");
        fflush(stdout);
        exit(1);
    }

    for(j = 0; j < MAX_TABLE; j++){ // inizializzo tutti i tavoli 
        sock_tb[j]  = -1;
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

    // pulizia file
    fclose(fopen(ORDINATIONS, "w"));
    fclose(fopen(CONN_DEVICES, "w"));
    fclose(fopen(RESERVATIONS, "w"));


    printf("\nComandi accettati:\n");
    printf("> stat [table|status] -> Mostra comande relative a 'table' o 'status', tutte quelle giornaliere se opzione mancante\n");
    printf("> stop -> avvia procedura di spegnimento a tutti i table e kitchen devices, se tutte le comande sono in stato di servizio\n\n");
    fflush(stdout);


    while(1){  
        printf("> ");
        fflush(stdout);
        int i=0;
        read_fds = master_r;
        select(fdmax+1, &read_fds, NULL, NULL, NULL);
        for(; i <= fdmax; i++){
            
            if(FD_ISSET(i, &read_fds)){ // socket pronto perché rimasto in insieme read_fds
    /* --- NEW CONNECTION --- */
                if(i == listener){      // ricevuta richiesta su socket di ascolto
                    cl_len = sizeof(cl_addr);
                    new_sd = accept(listener, (struct sockaddr*)&cl_addr, &cl_len);   // accept di richieste
                    FD_SET(new_sd, &master_r);
                    if(new_sd > fdmax){ fdmax = new_sd; }
        /* --- TYPE from new device --- */       
                    recv(new_sd, (void*)&new_dev.type, 1, 0);

                    new_dev.fd = new_sd;
                    inet_ntop(AF_INET, &cl_addr.sin_addr, new_dev.addr, cl_len);
                    // inserisco il device appena collegato nel file dei dispositivi
    printf("New connection on fd %d, device type: %c, client IP: %s\n", new_dev.fd, new_dev.type, new_dev.addr);
    fflush(stdout);
                    devices = fopen(CONN_DEVICES, "a");
                    fprintf(devices, "%d %c %s\n", new_dev.fd, new_dev.type, new_dev.addr);
                    fclose(devices);


                }else{ //  Gestione richieste su socket connessi e su stdin (gestito come default dai socket remoti)

                    char disp=' ';

                    // recupero il tipo ti dispositivo collegato al socket interessato
                    devices = fopen(CONN_DEVICES, "r");
                    while(!feof(devices)){
                        fscanf(devices, "%d %c %s", &dev.fd, &dev.type, dev.addr);
                        if (dev.fd == i){
                            disp = dev.type;
                            break;
                        }
                    }                  
                    fclose(devices);

                    switch(disp){

/* ----- GESTIONE PROTOCOLLO CON CLIENT ----- */
                        case 'C':
                            memset(buffer, '\0', BUFF_DIM); // pulizia buffer
                            ret = recv(i, (void*)buffer, CL_DIM, 0);
                            if(ret == 0){       // Client si è disconnesso
                                int line = 0;
                                close(i);
                                FD_CLR(i, &master_r);
                                devices = fopen(CONN_DEVICES, "r+");
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
                                remove(CONN_DEVICES);
                                rename("delete.temp", CONN_DEVICES);
                                continue;
                            }
                            sscanf(buffer,"%c", &option);
    /* --- FIND from cli --- */
                            if(option == 'F'){           // client ha richiesto la disponibilità
                                int j,k;
                                int conta;
                                uint16_t dim, dim_net;
                                char disponibilita[BUFF_DIM];
                                sscanf(buffer, "%c %s %d %s %02d", &option, request.name, &request.seat, request.date, &request.hour);
            printf("Request table on %s %02d from user %s\n", request.date, request.hour, request.name);
            fflush(stdout);
                                for(j = 0; j<MAX_TABLE; j++){   
                                    if(tav[j].chair >= request.seat){        // metto disponibili i tavoli con almeno i posti richiesti dal client
                                        tav[j].avaiable = 1;
                                    }else{
                                        tav[j].avaiable = 0;                // non disponibili quelli con pochi posti
                                    }
                                }
                                prenotazioni = fopen(RESERVATIONS, "r");
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
                                dim = strlen(disponibilita)+1;
                                dim_net= htons(dim);
                                send(i, (void*)&dim_net, sizeof(uint16_t), 0);
                                send(i, (void*)disponibilita, dim, 0);

    /* --- BOOK from cli --- */
                            }else if(option == 'B'){     // client vuole prenotare
                                int stillFree = 1;
                                sscanf(buffer, "%c %s %02d %s %s %d", &option, request.date, &request.hour, request.tb, request.name, &request.seat);
            printf("Request table %s from user %s\n", request.tb, request.name);
            fflush(stdout);  
                                prenotazioni = fopen(RESERVATIONS, "r");
                                if(prenotazioni != NULL){ 
                                    while(!feof(prenotazioni)){
                                        fscanf(prenotazioni,"%s %02d %s %*d %*s %*d %*s %*d", reserv.date, &reserv.hour, reserv.tb);
                                        if(strcmp(reserv.date, request.date) == 0 && reserv.hour==request.hour && strcmp(reserv.tb, request.tb)==0){ // Il tavolo non è più disponibile, qualcuno lo ha prenotato già (possibile per concorrenza FIND)
                                            stillFree = 0;
                                            request.bookId  = 0;
                                            request.bookId = htons(request.bookId);
            printf("Table already taken\n");
            fflush(stdout);
                                            send(i, (void*)&request.bookId, sizeof(uint16_t), 0);
                                            break;
                                        }
                                    }
                                    fclose(prenotazioni);
                                }
                                prenotazioni = fopen(RESERVATIONS, "a");
                                if(stillFree){ // prenotazione possibile, la aggiungo al file
            printf("Booking confirmed\n");
            fflush(stdout);

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
/* ----- FINE GESTIONE PROTOCOLLO CON CLIENT ----- */


/* ----- GESTIONE PROTOCOLLO CON TABLE DEVICE ----- */
                        case 'T':
                            
                            ret = recv(i, (void*)&td_value, sizeof(uint16_t), 0);
                            if(!ret){
                                int line = 0, t;
                                close(i);
                                FD_CLR(i, &master_r);
                                devices = fopen(CONN_DEVICES, "r+");
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
                                remove(CONN_DEVICES);
                                rename("delete.temp", CONN_DEVICES);
                                t = socktotb(i);
                                sock_tb[--t] = -1;  // rimuovo associazione tavolo da un socket
                                continue;
                            }
                            td_value = ntohs(td_value);
                    // Protocollo di login su td --> (tuti i codice prenotazione ricevuti  > 2000)
    /* --- LOGIN from td --- */
                            if(td_value > 2000){    // gestione del codice di prenotazione inviato 
            printf("From table: %d\n", td_value);
                                prenotazioni = fopen(RESERVATIONS, "r+");
                                if(prenotazioni != NULL){
                                    while(!feof(prenotazioni)){
                                        fscanf(prenotazioni, "%*s %*d %s %hu %*s %*d %*s %d", attend.tb, &attend.bookId, &attend.attendance);
                                        if(td_value == attend.bookId && attend.attendance == 0){    // prenotazione esistente e ancora non usata
                                            uint16_t n_tav;
                                            int ii;

            printf("Found table: %s, on booking id: %d already used(%d)\n", attend.tb, attend.bookId, attend.attendance);
            fflush(stdout);
                                            sscanf(attend.tb, "T%hu", &n_tav);
                                            ii = n_tav - 1;
                                            if(sock_tb[ii] == -1){   //al tavolo ancora non c'è associato un socket
                                                sock_tb[ii] = i;    // associo il socket 'i' al tavolo
                                            }else{
                                                printf("Error! Tavolo già in uso\n");
                                                fflush(stdout);
                                            }

                                            n_tav = htons(n_tav);
                                            send(i, (void*)&n_tav, sizeof(uint16_t), 0);
                                            fseek(prenotazioni, -1, SEEK_CUR);  // metto il puntatore prima del valore di attendance
                                            fprintf(prenotazioni, "1");     // metto 'attendance' a 1 indicando che un cliente ha usato quella prenotazione
                                            break;
                                        }
                                        if(td_value == attend.bookId && attend.attendance == 1){    // prenotazione esistente ma già usata
                                            uint16_t used = MAX_TABLE +1;
            printf("Found table: %s, on booking id: %d already used(%d)\n", attend.tb, attend.bookId, attend.attendance);
            fflush(stdout);
                                            used = htons(used);
                                            send(i, (void*)&used, sizeof(uint16_t), 0);
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
    /* --- COMANDA from td --- */
                            if(td_value <= 2000){   // arrivo nuova comanda e dim msg data da td_value
                                
                                memset(buffer, '\0', BUFF_DIM);         // pulizia buffer
                                recv(i, (void*)buffer, td_value, 0);    // ricevo la comanda
            printf("New order from table T%d\n\n", socktotb(i));
            fflush(stdout);

                                comande = fopen(ORDINATIONS, "a+");
                                if(comande != NULL){
                                    int tb_cd;
                                    char from_tb[4];
                                    tb_cd = socktotb(i);
                                    sprintf(from_tb, "T%d", tb_cd);
                                    fprintf(comande, "%s %s a\n", from_tb, buffer);

                                    fclose(comande);
                                }
                                w_order++;
                                sprintf(buffer, "RICEVUTA\n");
                                send(i, (void*)buffer, STAT_UPDATE_DIM, 0);  
        /* --- broadcast to kd --- */
                                broadcat_kd(w_order, -1);   // notifico i kd della nuova comanda
                            }
                        break;
/* ----- FINE GESTIONE PROTOCOLLO CON TABLE DEVICE ----- */

                    
/* ----- GESTIONE PROTOCOLLO CON KITCHEN DEVICE ----- */
                      case 'K':
                            ret = recv(i, (void*)&kd_value, sizeof(uint16_t), 0);
                            if(!ret){
                                int line = 0;
                                close(i);
                                FD_CLR(i, &master_r);
                                devices = fopen(CONN_DEVICES, "r+");
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
                                remove(CONN_DEVICES);
                                rename("delete.temp", CONN_DEVICES);
                                continue;
                            }

                            kd_value = ntohs(kd_value);
    /* --- TAKE from kd --- */
                            if(kd_value == 1){      // kitchen ha eseguito una take --> invio comanda in attesa da più tempo
                                struct comanda accepted_com;
                                int tb_fd;
                                comande = fopen(ORDINATIONS, "r+");
                                if(comande != NULL){
                                    char bufferk[BUFF_DIM];
                                    uint16_t kbuf_len, kbuf_len_net;
                                    while(!feof(comande)){
                                        fscanf(comande,"%s %s", accepted_com.tbl, accepted_com.orderId);    // salvo tavolo e comanda accettata
                                        fgets(bufferk, BUFF_DIM, comande);
                                        kbuf_len = strlen(bufferk);
                                        if(bufferk[kbuf_len-2] == 'a'){     // comanda in attesa -> la invio al kd
                                            char temp_buf[BUFF_DIM];
                                            bufferk[kbuf_len-2] = '\n';      // rimuovo lo stato dal messaggio a kd
                                            bufferk[kbuf_len-3] = '\n';      // rimuovo lo spazio tra comanda e stato per il msg  
                                              
                                            fseek(comande, -2, SEEK_CUR);   // metto il puntatore nel file davanti allo stato
                                            fprintf(comande, "p");          // metto la stato preparazione anche nel file

                                            sprintf(temp_buf, "%s %s%s", accepted_com.tbl, accepted_com.orderId, bufferk);   // costruisco il msg per il kd
            printf("Comanda accettata da kd: %s\n", temp_buf);
            fflush(stdout);
                                            kbuf_len =  strlen(temp_buf) +1;
                                            kbuf_len_net = htons(kbuf_len);
                                            send(i, (void*)&kbuf_len_net, sizeof(uint16_t), 0);
                                            send(i, (void*)temp_buf, kbuf_len, 0);       // invio la comanda in cucina
                                            w_order--;
                                            break;
                                        }

                                    }
                                    fclose(comande);
                                }
        /* --- notification to td --- */
                                memset(buffer, '\0', BUFF_DIM);         // pulizia buffer
                                tb_fd = tbtosock(accepted_com.tbl);
            printf("Notification to table: %s\n", accepted_com.tbl);
                                sprintf(buffer, "%s p", accepted_com.orderId); // costruisco messaggio per comunicare a td comanda accettata
                                send(tb_fd, (void*)buffer, STAT_UPDATE_DIM, 0);// notifico il td della comanda in preparazione
        /* --- broadcast to kd --- */
                                broadcat_kd(w_order, i);    // notifico tutti i kd che una comanda è atat accettata, eccetto quello che l'ha accettata

                            }

    /* --- READY from kd --- */
                            if(kd_value > 1){   // kd_value rappresenterà la dim del messaggio dal kd (messaggio di comanda in servizio) 
                                char temp_buf[BUFF_DIM];
                                struct comanda ready_com;
                                int tb_fd;
                                uint16_t ready_ok;
                                recv(i, (void*)temp_buf, kd_value, 0);
                                sscanf(temp_buf, "%s %s", ready_com.tbl, ready_com.orderId);
                                tb_fd = tbtosock(ready_com.tbl);
                                sprintf(temp_buf, "%s s", ready_com.orderId);
        /* --- notification to td --- */
                                send(tb_fd, (void*)temp_buf, STAT_UPDATE_DIM, 0);
                                
                                comande = fopen(ORDINATIONS, "r+");       // aggiorno lo stato della comanda nel file                                
                                if(comande != NULL){
                                    char bufferk[BUFF_DIM];
                                    int kbuf_len = 0;
                                    struct comanda current_com;
                                    memset(bufferk, '\0', BUFF_DIM);
                                    while(!feof(comande)){
                                        fscanf(comande,"%s %s", current_com.tbl, current_com.orderId);
                                        kbuf_len = strlen(bufferk);
                                        fgets(bufferk, BUFF_DIM, comande);      // posiziono puntatore in fondo riga
                                        if(!strcmp(current_com.tbl, ready_com.tbl) && !strcmp(current_com.orderId, ready_com.orderId)){     // trovo comanda da aggiornare
            printf("Comanda in servizio per tavolo :%s, stato prec: %c\n", current_com.tbl, bufferk[kbuf_len-2]);  
            fflush(stdout);          
                                            fseek(comande, -2, SEEK_CUR);   // metto il puntatore nel file davanti allo stato
                                            fprintf(comande, "s");          // metto la stato preparazione anche nel file
                                            break;
                                        }
                                    }
                                    fclose(comande);
                                }
                                ready_ok = htons(65535);
                                send(i, (void*)&ready_ok, sizeof(uint16_t), 0);  // invio conferma ricezione comanda in servizio

                            }

                        break;
/* ----- FINE GESTIONE PROTOCOLLO CON KITCHEN DEVICE ----- */


/* ----- GESTIONE COMANDI SERVER ----- */
                        default:
                            memset(buffer, '\0', BUFF_DIM);
                            memset(typed, '\0', 5);
                            fgets(buffer, BUFF_DIM, stdin);
                            sscanf(buffer, "%s", typed);
    
    /* --- comando STAT --- */
                            if(!strcmp(typed, "stat")){
                                char to_print[4];
                                struct comanda printing_com;
                                char temp_buff[BUFF_DIM];
                                char check_buff[BUFF_DIM];
                                uint16_t kbuf_len;

                                memset(to_print, '\0', 4);
                                sscanf(buffer, "%s %[^\n]%*c", typed, to_print);
                                memset(temp_buff, '\0', BUFF_DIM);
                                memset(check_buff, '\0', BUFF_DIM);


                                if(!strcmp(to_print, "p") || 
                                   !strcmp(to_print, "s") ||
                                   !strcmp(to_print, "a") ){    // stampo tutte le comande nello stato richiesto

                                    comande = fopen(ORDINATIONS, "r");
                                    if(comande != NULL){
                                        while(!feof(comande)){
                                            fscanf(comande,"%s %s", printing_com.tbl, printing_com.orderId);    // salvo tavolo e comanda 
                                            fgets(temp_buff, BUFF_DIM, comande);
                                            if(!strcmp(check_buff, temp_buff)){      // escape se fine file
                                                break;
                                            }
                                            kbuf_len = strlen(temp_buff);
                                            if(temp_buff[kbuf_len-2] == to_print[0]){     // stato comanda == richiesta comando stat
                                                temp_buff[kbuf_len-2] = '\n';       // rimuovo lo stato dall' ordine
                                                temp_buff[kbuf_len-3] = '\n';       // rimuovo lo spazio tra comanda e stato per print
                                                printOrder(temp_buff+1, printing_com.tbl, printing_com.orderId, ' ', 0);  
                                                strcpy(check_buff, temp_buff);      // copio il temp_buff (con \n invece di status) in check

                                            }

                                        }
                                        fclose(comande);
                                    }

                                }else if(to_print[0] == 'T'){   // stampo tutte le comande relative al tavolo indicato
                                    char tmp_stat = ' ';

                                    comande = fopen(ORDINATIONS, "r");
                                    if(comande != NULL){
                                        while(!feof(comande)){
                                            fscanf(comande,"%s %s", printing_com.tbl, printing_com.orderId);    // salvo tavolo e comanda 
                                            fgets(temp_buff, BUFF_DIM, comande);
                                            if(!strcmp(check_buff, temp_buff)){      // escape se fine file
                                                break;
                                            }
                                            kbuf_len = strlen(temp_buff);
                                            if(!strcmp(to_print, printing_com.tbl)){     // tavolo comanda == richiesta tavolo stat
                                                tmp_stat = temp_buff[kbuf_len-2];       // salvo stato comanda
                                                temp_buff[kbuf_len-2] = '\n';           // rimuovo lo stato dall'ordine
                                                temp_buff[kbuf_len-3] = '\n';           // rimuovo lo spazio tra comanda e stato per print
                                                printOrder(temp_buff+1, " ", printing_com.orderId, tmp_stat, 0);  
                                                strcpy(check_buff, temp_buff);// copio il temp_buff (con \n invece di status) in check

                                            }

                                        }
                                        fclose(comande);
                                    }

                                }else{      // nessuna opzione in 'stat'
                                    char tmp_stat = ' ';

                                    comande = fopen(ORDINATIONS, "r");
                                    if(comande != NULL){
                                        while(!feof(comande)){
                                            fscanf(comande,"%s %s", printing_com.tbl, printing_com.orderId);    // salvo tavolo e comanda 
                                            fgets(temp_buff, BUFF_DIM, comande);
                                            if(!strcmp(check_buff, temp_buff)){      // escape se fine file (check == temp)
                                                break;
                                            } 
                                            kbuf_len = strlen(temp_buff);
                                            // stampo tutte le comande nel file
                                            tmp_stat = temp_buff[kbuf_len-2];       // salvo stato comanda
                                            temp_buff[kbuf_len-2] = '\n';           // rimuovo lo stato dall'ordine
                                            temp_buff[kbuf_len-3] = '\n';           // rimuovo lo spazio tra comanda e stato per print
                                            printOrder(temp_buff+1, printing_com.tbl, printing_com.orderId, tmp_stat, 1);
                                            strcpy(check_buff, temp_buff);// copio il temp_buff (con \n invece di status) in check

                                        }
                                        fclose(comande);
                                    }

                                }
                                printf("\n");
                                fflush(stdout);

    /* --- comando STOP --- */                                           
                            }else if(!strcmp(typed, "stop")){
                                int end_service = 1;
                                char temp_buf[BUFF_DIM];
                                comande = fopen(ORDINATIONS, "r");    // controllo che tutte le comande siano servite
                                if(comande != NULL){
                                    char stat;
                                    int line_dim;
                                    while(!feof(comande)){
                                        fgets(temp_buf, BUFF_DIM, comande);   // leggo la riga
                                        line_dim = strlen(temp_buf);
                                        stat = temp_buf[line_dim-2];        // controllo il penulmtimo carattere della riga (ultimo è \n)

                                        if(stat != 's' &&
                                            (stat == 'a' || stat == 'p')){        // esiste almeno una comanda non servita ==> Servizio non terminato
                                            end_service = 0;
                                            break;
                                        }

                                    }
                                    fclose(comande);
                                }else{
                                    printf("File 'ordinations.txt' assente\n");
                                    fflush(stdout);
                                }
                                if(end_service){    // avvio chiusura tutti device
                                    devices = fopen(CONN_DEVICES, "r");
                                    if(devices != NULL){
                                        while(!feof(devices)){
                                            fscanf(devices, "%d %c %*s", &dev.fd, &dev.type);
                                            if(feof(devices)){
                                            break;
                                            }
                                            if(dev.type == 'K' || dev.fd == 'T'){   // chiudo la connessione ai kd e td, avviandone la procedura di spegnimento
                                            close(dev.fd);
                                            }
                                        }
                                    fclose(devices);
                                    }
                                    // remove(CONN_DEVICES);      // file devices per le connessioni attive
                                    exit(0);
                                }else{
                                    printf("Stop non eseguibile\n\n");
                                    fflush(stdout);
                                }
                            }else{                           
                                printf("Comando non valido\n\n");
                                fflush(stdout);
                            }
                            
                        break;
/* ----- FINE GESTIONE COMANDI SERVER ----- */
                    }                  
                }
            }
        }
    }
    exit(0);
}

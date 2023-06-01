// client prenotazione

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

#define BUFF_DIM 1024
#define MSG_DIM 49 // 49 = 1 (codice operazione) + 25 (max caratteri per nome) + 3 (max cifre per posti prenotazione)
                   //       + 4 (char per tavolo) + 8 (char data) + 2 (cifre ora) + 6 (spazi + fine stringa)
#define MAX_TABLE 10 // indica il numero massimo di tavoli disponibili (nel nostro ristorante ne abbiamo 3)

struct booking{ // struttura per informazioni sulle prenotazioni
    char date[8+1]; // data per prenotazione
    int hour;       // ora per prenotazione
    char table[4];  // tavolo scelto
    char name[25];  // cognome 
    int seat;       // posti 
};

struct avaiable{    // struttura per salvare le disponibilità date dal server
    char tb[4];
    char room[7];
    char descr[20];
    int av;

};
struct avaiable offer[MAX_TABLE];

int past_date(char* data){
    int day, month, year;
    int t_day, t_month, t_year;
    time_t raw;
    struct tm*  ts;
    
    time(&raw);
    ts = localtime(&raw);
    t_day =(*ts).tm_mday;
    t_month = (*ts).tm_mon + 1;     // tm_mon imposta i mesi da 0 a 11
    t_year = (*ts).tm_year-100;
    
    sscanf(data, "%d-%d-%d", &day, &month, &year);

    if(year < t_year){
        return 1;
    }else if((t_year == year && month < t_month) || month < 1 || month > 12){
        return 1;
    }else if((t_month == month && day < t_day) || day < 1 || day > 31){
        return 1;
    }
    return 0;

}

void format_data(char* data){
    int day, month, year;
    sscanf(data, "%d-%d-%d", &day, &month, &year);
    sprintf(data, "%02d-%02d-%02d", day, month, year);
}

int main (int argnum, char** arg) {

    int sd, ret;
    struct sockaddr_in server_addr;
    char buffer[BUFF_DIM];
    char option[5]; // conterrà i comandi scritti sullo stdin
    int port;       // porta del server, se specificata a esecuzione client, 4242 i default
    
    struct booking req; // struttura per costuire la richiesta di prenotazione al server
    char code;          // codice di protocollo per segnalare al server l'operazione in esecuzione
    char device = 'C';  // 'C' = client prenotazione, 'T' = table device, 'K' = kitchen device
    int find_done = 0;  // Variabile per impedire di eseguire una book prima di aver eseguito una find

    port = 4242;        //(argnum == 2) ? atoi(arg[1]) : 4242;       (usiamo 4242 come porta di default se non viene specificata all'esecuzione)
    
    sd = socket(AF_INET, SOCK_STREAM, 0); // creazione socket

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    ret = connect(sd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(ret == -1){
        perror("Errore di connessione: ");
        exit(1);
    }
    send(sd, (void*)&device, 1, 0);   // segnalo al server il tipo di device da cui sto comunicando
    printf("--------------- BENVENUTO NEL SISTEMA DI PRENOTAZIONE DEL RISTORANTE VDG --------------\n");
        
    printf("\nhelp <comando> -> specifica comando\n");
    printf("find -> ricerca disponibilità per una prenotazione\n");
    printf("book -> invia una prenotazione\n");
    printf("esc  -> termina il client\n\n");
    fflush(stdout);

    while(1){
        memset(buffer, '\0', BUFF_DIM);
        fgets(buffer, BUFF_DIM, stdin);
        sscanf(buffer, "%s", option);

        if(!strcmp(option, "find")){
            char msg[MSG_DIM]; 
            uint16_t ans_dim;
            int i, count;
            char check[4];  // variabile per controllare la fine delle proposte arrivte dal server
            code = 'F';
            find_done = 1;      // si sta eseguendo almeno una find
            sscanf(buffer, "%s %s %d %s %02d", option, req.name, &req.seat, req.date, &req.hour);
            // CONTROLLI SU INPUT
            if(past_date(req.date)){               // controllo che la data non sia nel passato
                printf("Controllare sintassi data e che questa sia futura al giorno odierno\n");
                fflush(stdout);
                continue;
            }
            if(req.hour < 12 || req.hour > 24){     // controllo che l'orario inserito concordi con l'apertura del ristorante
                printf("Controllare orari ristorante\n");
                fflush(stdout);
                continue;
            }
            format_data(req.date);      // scrivo la data in formato dd-mm-yy

            // creazione messaggio FIND
            sprintf(msg, "%c %s %d %s %02d", code, req.name, req.seat, req.date, req.hour); // creazione del messaggio per la richiesta al server
        
            send(sd, (void*)msg, MSG_DIM, 0);      // invio richiesta prenotazione
            recv(sd, (void*)&ans_dim, sizeof(uint16_t), 0); // riceve dimensione messaggio con tavoli disponibili
            ans_dim = ntohs(ans_dim);
            if(ans_dim == 0){
                printf("Non ci sono tavoli disponibili per i valori inseriti\n");
                fflush(stdout);
                continue;
            }
            memset(buffer, '\0', BUFF_DIM);
            recv(sd, (void*)buffer, ans_dim, 0);            //  riceve tabella tavoli disponibili
            printf("%s\n", buffer);
            fflush(stdout);
            //sscanf(buffer, "%*d) %s %s %*s %*d) %s %s %*s %*d) %s %s %*s", offer[0].tb, offer[0].room, offer[1].tb, offer[1].room, offer[2].tb, offer[2].room);
            for(i =0; i<MAX_TABLE; i++){       // pulizia di offerta
                memset(offer[i].tb,'\0', sizeof(offer[i].tb));
                memset(offer[i].room,'\0', sizeof(offer[i].room));
                memset(offer[i].descr,'\0', sizeof(offer[i].descr));
            }
            count = 0;
            memset(check, '\0', 4);     // pulizia check
            for(i =0; i<MAX_TABLE; i++){
                char lista[4];
                sscanf(buffer + count, "%s %s %s %s %n", lista, offer[i].tb, offer[i].room, offer[i].descr, &ret);
                if(!strcmp(check,lista)){   // se check == lista allora non vi è più nulla da leggere, da qui in poi tutti i tavoli saranno non disponibili
                                            // non devo aggiornare count perché ret è rimasto il valore precedente
                    offer[i].av = 0;
                }else{
                    offer[i].av = 1;
                    count = count + ret;
                }
                strcpy(check, lista);
            }


        }else if(!strcmp(option, "book")){
            char msg[MSG_DIM];
            int pren_num;         // scelta prenotazione utente
            uint16_t book_ok;     // codice di conferma e id prenotazione
            code = 'B';
            if(!find_done){
                printf("Eseguire una find prima di tentare una prenotazione\n");
                fflush(stdout);
                continue;
            }
            sscanf(buffer, "%s %u", option, &pren_num);
            // verifica prenotazione valida
            if(pren_num <=0 || pren_num > MAX_TABLE){
                printf("Numero prenotazione non valido, scegliere tra le prenotazioni disponibili\n\n");
                fflush(stdout);
                continue;
            }
            if(offer[pren_num-1].av){   // esiste la prenotazione richiesta dall'utente
                strcpy(req.table, offer[pren_num-1].tb);
            }else{
                printf("Numero prenotazione non valido, riprovare!\n\n");
                continue;
            }
            
            //creazione messaggio BOOK
            sprintf(msg, "%c %s %02d %s %s %d", code, req.date, req.hour, req.table, req.name, req.seat);
            send(sd, (void*)msg, MSG_DIM, 0);
            recv(sd, (void*)&book_ok, sizeof(uint16_t), 0); // server risponderà per confermare prenotazione con il codice di questa, 0 se non validata (es inserito un valore errato)
            book_ok = ntohs(book_ok);
            if(book_ok){
                printf("PRENOTAZIONE EFFETTUATA\n");
                printf("Codice prenotazione: %d\n\n", book_ok);
            }else{ // book_ok == 0 --> prenotazione non a buon fine
                printf("PRENOTAZIONE NON PIU' VALIDA\n");
                printf("Prego rieseguire una find\n\n");
            }
            fflush(stdout);

        }else if(!strcmp(option, "help")){
            char command[5]; // indica il comando di cui si richede le specifiche
            sscanf(buffer, "%s %s", option, command);
            if(!strcmp(command, "find")){
                printf("Il comando %s richiede al server la disponibilità per un numero di posti in una data, a una specifica ora\n", command);
                printf("sintassi:\n\t%s <nome> <posti> <data(DD-MM-YY)> <ora(HH)>\n", command);
            }
            if(!strcmp(command, "book")){
                printf("Il comando %s richiede la prenotazione al server con l'opzione scelta\n", command);
                printf("sintassi:\n\t%s <opzione>\n", command);
            }if(!strcmp(command, "esc")){
                printf("Il comando %s interrompe la connessione al server e termina questo client\n", command);
            }
            fflush(stdout);

        }else if(!strcmp(option, "esc")){
            close(sd);
            exit(0);
        
        }else{
            printf("Comando inserito non valido\n\n");
            fflush(stdout);
        }
    }
}
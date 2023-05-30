// client kitchen device

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
// #include <sys/time.h>

#define BUFF_DIM 1024
#define MAX_COMANDE 40

struct comanda{
    char tbl[4];        // codice tavolo
    char orderId[5];    // codice comanda
    char order[75];     // comanda effettiva
    char status; 
};
struct comanda accepted[MAX_COMANDE];   // le comande accettate le salvo in memoria

void printOrder(int n){  // n->indice dell'array 'accepted' della comanda da stampare
    int count = 0;
    int ret = -1;
    char piatto[4];
    char c_num[3];
    int num;       

    printf("%s %s\n", accepted[n].orderId, accepted[n].tbl);

    while(accepted[n].order[ret] != '\n'){     // parsing della stringa carattere per carattere
        count = 0;
        ret++;
        while(accepted[n].order[ret]!= '-'){ // leggo codice piatto
            piatto[count] = accepted[n].order[ret];
            count++;
            ret++;
        }
        ret++;
        piatto[count] = '\0';
        printf("%s ", piatto);
        count = 0;
        while(accepted[n].order[ret] != ' ' && accepted[n].order[ret] != '\n'){   // leggo la quantità
            c_num[count] = accepted[n].order[ret];
            count++;
            ret++;
        }
        c_num[count] = '\0';
        num = atoi(c_num);  // quantità da ASCII a int
        printf("%d\n", num);
    }
}

int checkOrder(char* tb, char* com, int t){
    int j;
    for(j = 0; j < t; j++){
        if(!strcmp(accepted[j].tbl, tb) && !strcmp(accepted[j].orderId, com)){   // ho trovato l'ordine che cerco
            return 1;
        }
    }
    return 0;
}



int main (int argnum, char** arg) {

    int sd, ret;
    struct sockaddr_in server_addr;
    char buffer[BUFF_DIM];
    char option[8];
    int port;
    char device = 'K';   

    int taken_orders = 0;    // ordini accettati dal kd 
    int on_hold;            // ordini in attesa di take
    uint16_t from_srv;      // messaggioda server, se >= 100 --> indica le comande in attesa (#inattesa = from_srv -100)
                            //                     se < 100 --> dimensione messaggio comanda accettata da take
    uint16_t msg_to_srv;    // valore inviato a server

    fd_set master_r;        // creo insiemi di descrittori
    fd_set read_fds;        // creo insiemi di descrittori
    int fdmax;

    FD_ZERO(&master_r); // pulizia insiemi di descrittori    
    FD_ZERO(&read_fds); // pulizia insiemi di descrittori  

    port = 4242;        //(argnum == 2) ? atoi(arg[1]) : 4242 

    sd = socket(AF_INET, SOCK_STREAM, 0);   // creazione socket

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    ret = connect(sd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(ret == -1){
        perror("Errore di connessione: ");
        exit(1);
    }

    FD_SET(sd, &master_r);        // metto il socket di ascolto nell'insieme totale
    FD_SET(fileno(stdin), &master_r);   // metto stdin nell'insieme master
    fdmax = sd;


    send(sd, (void*)&device, 1, 0); // comunico al server il tipo di dispositivo
    
    printf("\nhelp -> specifiche comandi\n");
    printf("take -> accetta una comanda\n");
    printf("show -> mostra comande accettate in preparazione\n");
    printf("ready  -> imposta una comanda in servizio\n\n");

    while(1){
        
        int i=0;

        read_fds = master_r;
        select(fdmax+1, &read_fds, NULL, NULL, NULL);
        for(; i <= fdmax; i++){ 
            if(FD_ISSET(i, &read_fds)){ // socket pronto perché rimasto in insieme read_fds
                if(i==fileno(stdin)){   // socket di standard input

                    memset(buffer, '\0', BUFF_DIM);
                    fgets(buffer, BUFF_DIM, stdin);
                    sscanf(buffer, "%s", option);

                    if(!strcmp(option, "take")){
                        if(on_hold > 0){
                            msg_to_srv = htons(1);
                            send(sd, (void*)&msg_to_srv, sizeof(uint16_t), 0);      // invio 1 al server --> richiesta di comanda da preparare
                            // attendo il messaggio dal server nella select 
                        }else{      // on_hold == 0, non ci sono comande da accettare
                            printf("Non sono presenti comande da accetare\n");
                        }                   


                    }else if(!strcmp(option, "show")){
                        int jj;
                        for(jj = 0; jj < taken_orders; jj++){
                            if(accepted[jj].status == 'p'){
                                printOrder(jj);
                            }
                        }

                    }else if(!strcmp(option, "ready")){
                        char settb[4];
                        char setcom[5];
                        int valid_order = 0;
                        sscanf(buffer,"%s %s-%s", option, settb, setcom);   // leggo codice com e tavolo da segnalare pronti
    printf("lettura comanda: %s, tavolo: %s\n", settb, setcom);
                        valid_order = checkOrder(settb, setcom, taken_orders);    // controllo che l'ordine esista
                        if(valid_order){    // comunico a server, e td, l'ordine in servizio
                            char ready[9];
                            sprintf(ready, "%s %s", settb, setcom);
                            
                            msg_to_srv = strlen(ready) + 1;
                            msg_to_srv = htons(msg_to_srv);
                            send(sd, (void*)&msg_to_srv, sizeof(uint16_t), 0);
                            msg_to_srv = ntohs(msg_to_srv);
                            send(sd, (void*)ready, msg_to_srv, 0);
                            // attendo conferma da server come codice 69420
                        
                        }else{
                            printf("Ordine: %s-%s non esistente\n", settb, setcom);
                        }

                    }else if(!strcmp(option, "help")){

                        char command[8];
                        sscanf(buffer, "%s %s", option, command);
                        if(!strcmp(command, "take")){
                            printf("Il comando %s prende in carico la comanda da più tempo in attesa e mostra i piatti che la compongono\n", command);
                        }
                        if(!strcmp(command, "show")){
                            printf("Il comando %s mostra le comande accettate\n", command);
                            printf("Le comande saranno indicate nel formato:\n\t*cod_comanda* *tavolo*\n *cod_piatto1* *quantità1*\n*cod_piatto2* *quantità2*\n ... \n*cod_piattoN* *quantitàN*\n");

                        }if(!strcmp(command, "ready")){
                            printf("Il comando %s imposta come pronta la comanda indicata\n", command);
                            printf("Sintassi:\n\t*cod_comanda-tavolo*");
                        }
                    }else{
                        printf("Comando inserito non valido\n\n");
                    }

                }else if(i == sd){      // socket a server
                    int j;
                    j = recv(sd, (void*)&from_srv, sizeof(uint16_t), 0);  // ricevo il numero di comande in attesa
                    if(j == 0){     // spegnimento del server
                        close(sd);
                        FD_CLR(sd, &master_r);
                        FD_CLR(fileno(stdin), &master_r);
                        exit(0);
                    }
                    from_srv = ntohs(from_srv);
    printf("value from srv: %d\n", from_srv);    
                    if(from_srv == 65535){
                        printf("COMANDA IN SERVIZIO\n");
                    }else if(from_srv == 2000){
                        on_hold = 0;
                        printf("Non sono presenti comande da accettare");
                    }else if(from_srv > 2000 && from_srv != 65535){
                        on_hold = from_srv - 2000;
                        for(j =0; j < on_hold; j++){
                            printf("*");    // mostro tante * quante le comande in attesa di preparazione
                        }printf("\n");
                    }else if(from_srv < 2000){  // from_srv < 2000 --> dimensione della comanda accettata
                        char temp_buf[BUFF_DIM];
                        int read;
                        memset(temp_buf, '\0', BUFF_DIM);
                        recv(sd, (void*)temp_buf, from_srv, 0);             // comanda da preparare
    printf("da srv: %s\n", temp_buf);

                        accepted[taken_orders].status = 'p';    // imposto l'ordine in preparazione
                        sscanf(temp_buf, "%s %s %n", accepted[taken_orders].tbl, accepted[taken_orders].orderId, &read);    // copio ordine id e tavolo nella struttura
                        strcpy(accepted[taken_orders].order, temp_buf + read);      // ricopio l'ordine stesso
                        printOrder(taken_orders);

                        taken_orders++;     // incremento il numero di ordini accettati dal td
                    }

                }
            }
        }
    }

    exit(0);
}
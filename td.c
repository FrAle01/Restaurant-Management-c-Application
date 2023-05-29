// client table device

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
#define MENU_DIM 1024
#define STAT_COMM 8 // dimensione del messaggio dal server che comunica lo stato di una comanda
                    // cod_com (5) + stato (1) + spazio e finestringa (2)
#define MAX_TABLE 3

struct consumazione{
    char dish[4];
    int cost;
    int quantity;

};

struct ordine{
    char id[5];
    char order[75];
    char status;
};

int conta_piatti(){
    FILE *fd;
    int n;
    char aux[BUFF_DIM];

    fd = fopen("menu.txt", "r");
    if(fd != NULL){
        while(!feof(fd)){       // conto quante righe sono nel file menu, quindi quanti piatti
            fgets(aux, BUFF_DIM, fd);
            n++;
        }
        fclose(fd);
    }
    n--;        // nel while si conta una riga in più
    return n;
}

void pasto_ini(struct consumazione *add, int num_piatti){
    FILE *fd;
    char piatto[4];
    int prezzo, j;

    fd = fopen("menu.txt", "r");
    if(fd != NULL){
        for(j = 0; j < num_piatti ; j++){      
            fscanf(fd, "%s", piatto);   // leggo il codice del piatto
            while(fgetc(fd) != '\n'){}  // mi sposto in fondo alla riga
            fseek(fd, -1, SEEK_CUR);
            fseek(fd, -2, SEEK_CUR);  // riposiziono il puntatore prima della cifra (prezzo nel menù) per leggerla
            fscanf(fd, "%d", &prezzo);
            
            strcpy(add[j].dish, piatto);
            add[j].cost = prezzo;
            add[j].quantity = 0;
            
        }
        fclose(fd);
    }
}

void formato_comanda(char *st, struct consumazione *add, int num_piatti){
    int num;
    char piatto[4];
    int count, ret, i;

printf("In format com: %s", st);

    count = 0;
    ret = -1;       // Incremento subito all'inizio del while, perché ret incrementa di 2  per ogni piatto
                    // ma se incontra '\n' deve uscire
    while(st[ret] != '\n'){     // parsing della stringa carattere per carattere
        count = 0;
        ret++;
        while(st[ret] != '-'){ // leggo codice piatto
            piatto[count] = st[ret];
            count++;
            ret++;
        }
        ret++;
        piatto[count] = '\0';
        num = st[ret] - 48; // quantità da ASCII a int
        ret++;
        for(i=0; i < num_piatti; i++){      // cerco il piatto per aumentarne la quantità ordinata
            if(!strcmp(piatto, add[i].dish)){
                add[i].quantity += num;    // aumento la quantità nell'ordine totale
            }
        }
    }
}



int main (int argnum, char** arg) {

    int sd, ret;
    struct sockaddr_in server_addr;
    char buffer[1024];
    char option[8];
    int port;
    char menu[MENU_DIM];                    // variabile che conterrà il menù letto dal file
    char device = 'T';              

    uint16_t cod_pren;                      // codice inserito dal cliente che esegue il programma sul device
    uint16_t tb_num;                        // valore inviato dal server per associare il device a un tavolo (vedi tb)
    char tb[4];                             // tavolo associato al table_device, per il codice di prenotazione, comunicato dal server
    int n_piatti = conta_piatti();          // numero di piatti nel file menù
    struct consumazione pasto[n_piatti];    // struttura che per ogni piatto tiene conto della quantità ordinata durante il pasto
    char file_comande[20];                  // file unico per la ogni prenotazione, contiene tutte le comande richieste durante un pasto
    int order_count = 0;                    // contatore delle comande effettuate sul device

    fd_set master_r;     // creo insiemi di descrittori
    fd_set read_fds;     // creo insiemi di descrittori
    int fdmax;
    
    FILE* f_menu;
    FILE* comande;

    FD_ZERO(&master_r); // pulizia insiemi di descrittori    
    FD_ZERO(&read_fds); // pulizia insiemi di descrittori  

    port = (argnum == 2) ? atoi(arg[1]) : 4242;             // usiamo 4242 come porta di default se non viene specificata all'esecuzione

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



    printf("--------------- BENVENUTI AL RISTORANTE DA NONNA PINA -------------");
    while(1){   // controllo che il codice di prenotazione inserito sia valido
        printf("Prego inserire il numero della prenotazione:\n");
        scanf("%d", &cod_pren);
        fflush(stdin);
        if(cod_pren <= 2000){       // per costruzione del servizio i codici di prenotazione saranno tutti > 2000
            printf("Codice prenotazione inserito non valido, si prega di riprovare\n\n");
            continue;
        }

        cod_pren = htons(cod_pren);
        send(sd, (void*)&cod_pren, sizeof(uint16_t), 0);    // comunico il codice di prenotazione  al server per validarlo
        recv(sd, (void*)&tb_num, sizeof(uint16_t), 0);      // riceve risposta da server con tavolo identificato (0 < tb_num < MAX_TABLE)
                                                            // 0 se cod prenotazione non valido
                                                            // MAX_TABLE + 1 (>MAX_TABLE) se codice già usato su altro tb_device
        tb_num = ntohs(tb_num);

        if(tb_num == 0){
            printf("Codice prenotazione inserito non valido, si prega di riprovare\n\n");
        }else{
            if(tb_num > 0 && tb_num <= MAX_TABLE){  // il tavolo associato al device deve esistere
                sprintf(tb, "T%d", tb_num);
                break;
            }
            else{           // si è ricevuto il valore MAX_TABLE+1
                printf("Codice di prenotazione già usato da un altro device\n");
            }
        }
    }
    cod_pren = ntoh(cod_pren);
    sprintf(file_comande, "ordini%d.txt", cod_pren);   // ogni table device ha un proprio file di comande in base al codice di prenotazione

    f_menu = fopen("menu.txt", "r");
    if(f_menu != NULL){  
        char c;
        int j = 0;  
        while(!feof(f_menu)){       // copio il contenuto del file menù nella variabile
            c = fgetc(f_menu);
            if(c == EOF){
                break;
            }
            menu[j] = c; 
            j++;
        }
        fclose(f_menu);
    }else{
        printf("File menù assente\n");  // no menu no comande da inviare
        close(sd);
        exit(1);
    }
    fflush(stdout);
    printf("\t--- Menù del giorno ---\n");
    printf("%s\n", menu);

    pasto_ini(pasto, n_piatti);      // nella struttura assegno a ogni piatto il proprio prezzo e inizializzo la quantità a 0;

    {   int jj;
        for(jj = 0; jj < n_piatti; jj++){
            printf("Piatti:\n%s %d %d\n", pasto[jj].dish, pasto[jj].cost, pasto[jj].quantity);
        }
    }
    printf("\nhelp -> specifiche comandi\n");
    printf("menu -> mostra il menù dei piatti\n");
    printf("comanda -> invia una comanda\n");
    printf("conto  -> chiede il conto\n\n");


    while(1){
        int i=0;

        read_fds = master_r;
        select(fdmax+1, &read_fds, NULL, NULL, NULL);
        for(; i <= fdmax; i++){ 
            if(FD_ISSET(i, &read_fds)){ // socket pronto perché rimasto in insieme read_fds
                if(i==fileno(stdin)){   // socket di standard input
                    
                    memset(buffer, '\0', BUFF_DIM);
                    fgets(buffer, BUFF_DIM, stdin);
                    fflush(stdin);
                    sscanf(buffer, "%s", option);

                    if(!strcmp(option, "menu")){
                        printf("\t--- Menù del giorno ---\n"); 
                        printf("%s\n", menu);

                    }else if(!strcmp(option, "comanda")){

                        struct ordine nuova_comanda;
                        uint16_t msg_dim, msg_dim_net;
                        int count = 0;
        printf("Sono in comanda... %s\n", buffer);    
                        order_count++;  // incremento il numero di comande eseguite sul device
                        sscanf(buffer, "%s %n", option, &count);
        printf("count: %d\n", count);
                        formato_comanda(buffer+8/*count dovrebbe valere 8-->'comanda '*/, pasto, n_piatti);
        printf("%s\n", buffer);
                        sprintf(nuova_comanda.id, "com%d", order_count);    // assegno un codice incrementale alla comanda eseguita
                        sprintf(nuova_comanda.order, "%s", buffer);         // ricopio la comanda nel formato definito
                        nuova_comanda.status = 'a';                         // attribuisco alla comanda lo stato di attesa
                        comande = fopen(file_comande, "a");
                        fprintf(comande, "%s %s %c\n", nuova_comanda.id, nuova_comanda.order, nuova_comanda.status);
                        fclose(comande);

                        msg_dim = sizeof(nuova_comanda.order);
                        msg_dim_net = htons(msg_dim);
                        send(sd, (void*)&msg_dim_net, sizeof(uint16_t), 0); // comunico la dimensione del messaggio
                        send(sd, (void*)nuova_comanda.order, msg_dim, 0);   // comunico l'ordine al server



                        
                    }else if(!strcmp(option, "conto")){

                        // controllo che tutte le comande siano servite
                        FILE *check;
                        int end_service = 1;
                        check = fopen(file_comande, "r");
                        if(check != NULL){
                            char stat;
                            while(!feof(check)){
                                fscanf(check, "%*s %*s %c", &stat);
                                if(stat != 's'){        // esiste almeno una comanda non servita ==> Servizio non terminato
                                    end_service = 0;
                                    break;
                                }
                            }
                            fclose(check);
                        }else{
                            printf("File %s assente\n", file_comande);
                        }

                        if(end_service){                        
                            int j;
                            int tot = 0, parziale= 0;
                            for(j = 0; j< n_piatti; j++){
                                if(pasto[j].quantity != 0){
                                    parziale = pasto[j].cost*pasto[j].quantity;
                                    printf("%s  %d  %d\n", pasto[j].dish, pasto[j].quantity, parziale);
                                    tot += parziale;
                                }
                            }
                            printf("Totale: %d", tot);

                            // Avvio spegnimento del td
                            close(sd);
                            FD_CLR(sd, &master_r);
                            FD_CLR(fileno(stdin), &master_r);
                            exit(0);
                        }else{
                            printf("Comande ancora in attesa o preparazione, si prega di richiedere il conto a fine servizio\n");
                        }



                    }else if(!strcmp(option, "help")){
                        
                        char command[8];
                        sscanf(buffer, "%s %s", option, command);
                        if(!strcmp(command, "menu")){
                            printf("Il comando %s richiede il menù del ristorante per la giornata in corso\n", command);
                        }
                        if(!strcmp(command, "comanda")){
                            printf("Il comando %s aggiunge una comanda in cucina in attesa di essere accettata da un cuoco\n", command);
                            printf("sintassi:\n\t%s *cod_piatto1-quantità1 cod_piatto2-quantità2... cod_piattoN-quantitàN*\n", command);
                        }if(!strcmp(command, "conto")){
                            printf("Il comando %s richiede il conto\n", command);
                        }
                    }else{
                        printf("Comando inserito non valido ma anche comanda\n\n");
                    }
                }else if(i == sd){  // notifica sulle comande dal server
                    recv(sd, (void*)buffer, 10, 0);
                }
            }
        }
        
    }
        

    
    exit(0);
}
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h> 
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

int main() {
    int dev_id = hci_get_route(NULL); // Cerchiamo la prima antenna bluethoot accesa e funzionante senza dover necessariamente indicare un MAC ADDRESS
    if (dev_id < 0) { // Errore: se stampa -1.
        perror("Errore: Nessun dispositivo Bluetooth trovato");
        exit(1);
    }

    int sock = hci_open_dev(dev_id); // usiamo l'indirizzo dev_id per e crea una socket_HCI verso il chip dell'antenna
    if (sock < 0) {
        perror("Errore: Impossibile aprire il socket");
        exit(1);
    }

    // Impostiamo il filtro HCI per ascoltare solo gli eventi BLE
    struct hci_filter filter;

    hci_filter_clear(&filter); // Whitelist 
    hci_filter_set_ptype(HCI_EVENT_PKT, &filter); // Filtriamo i pacchetti, consideriamo solo gli Advertising dei dispositivi
    hci_filter_set_event(EVT_LE_META_EVENT, &filter); // Consideriamo solo il Low Energy ( Classic bluetooth viene filtrato )


    // Set Socket Options: applichiamo il filtro alla socket HCI
    if (setsockopt(sock, SOL_HCI, HCI_FILTER, &filter, sizeof(filter)) < 0) {
        perror("Errore nell'impostazione del filtro socket");
        close(sock);
        exit(1);
    }

    // Configura e accende la scansione
    
    // Nel BLE possiamo avere scansione ATTIVA(0x01) e PASSIVA(0x00 );
    uint8_t scan_type = 0x01; // Mettendo ATTIVA, verrà inviata una SCAN_REQ ai dispositivi scansionati

    // Interval e Window rispettivamente "ogni quanto la radio si sveglia" e "Per quanto tempo resta accesa"
    // In questo caso usiamo htobs() (Host TO Bluetooth Short) per la gestione automatica dell'Endiannes 
    uint16_t interval = htobs(0x0010);
    uint16_t window = htobs(0x0010);

    uint8_t own_type = 0x00;
    uint8_t filter_policy = 0x00; //Nessuna policy di filtraggio

    // Settiamo i parametri e li mandiamo al Chip
    hci_le_set_scan_parameters(sock, scan_type, interval, window, own_type, filter_policy, 1000);
    //Avvia la Scansione
    hci_le_set_scan_enable(sock, 0x01, 0x00, 1000);



    printf("Scansione avviata.\n");


    // AVVIO DEL LOOP DI RILEVAMENTI DEGLI ADV_PKTS

    // Loop di lettura ( limitiamo a 30 pacchetti)
    const int limit = 30;

    // Allochiamo un buffer per i rilevamenti
    unsigned char buf[HCI_MAX_EVENT_SIZE];
    
    
    // Cicliamo per "limit" numero di volte
    for (int i = 0; i < limit; i++) {

        // preleviamo i dati tramite sys call read() dell'ambiente Linux/UNIX
        int len = read(sock, buf, sizeof(buf));
        if (len < 0) continue; 



        //Essendo il buffer un insieme di dati grezzi è necessario fare dei controlli

        // Tipo di pacchetto: vediamo il primo byte del buff e vediamo se vale 0x04 ( ovvero HCI_EVENT_PKT)
        // Questo perchè in HCI UART Transport Layer: il primissimo byte di ogni singolo messaggio definisce che cos'è l'intero messaggio.
        // Perciò controllando che sia pari a 0x04 ci assicura che sia un HCI Event.
        if (buf[0] == HCI_EVENT_PKT) {

            // spostiamo il cursore avanti di un byte
            hci_event_hdr *hdr = (void *)(buf + 1); 


            // Adesso controlliamo che sia effettivamente un evento Low Energy ( EVT_LE_META_EVENT = 0x03)
            if (hdr->evt == EVT_LE_META_EVENT) {

                //Mandiamo avanti il cursore, saltando la lunghezza dell'header HCI_EVENT_HDR_SIZE (dim: 2 byte )
                evt_le_meta_event *meta = (void *)(buf + 1 + HCI_EVENT_HDR_SIZE);


                // Infine controlliamo se è un Advertising
                if (meta->subevent == EVT_LE_ADVERTISING_REPORT) {
                    // Modifica del cursore
                    le_advertising_info *info = (void *)(meta->data + 1);


                    // Estrazione del MAC Address
                    char addr_str[18];
                    ba2str(&info->bdaddr, addr_str);

                    // Estrazione del Received Signal Strngth Indicator ( RSSI )
                    int8_t rssi = (int8_t)info->data[info->length];

                    // Estraiamo il nome del dispositivo, mettiamo "sconosciuto" come default
                    char device_name[limit] = "Sconosciuto"; 
                    int data_offset = 0;

                    // Analizziamo il payload blocco per blocco (Formato LTV: Length, Type, Value)
                    while (data_offset < info->length) {
                        int field_length = info->data[data_offset];
                        
                        // Se la lunghezza è 0, la fine dei dati utili è raggiunta
                        if (field_length == 0) break; 
                        
                        int field_type = info->data[data_offset + 1];
                        
                        // Il codice 0x09 indica il "Complete Local Name", 0x08 il "Shortened Local Name"
                        if (field_type == 0x09 || field_type == 0x08) {
                            int name_len = field_length - 1; // Sottraiamo 1 perché non contiamo il byte del 'Type'
                            
                            // Protezione per non superare la dimensione massima dell'array (limit + terminatore)
                            if (name_len > limit) name_len = limit; 
                            
                            // Copiamo i byte del nome nel nostro array
                            memcpy(device_name, &info->data[data_offset + 2], name_len);
                            device_name[name_len] = '\0'; // Aggiungiamo il terminatore di stringa
                            break; // Nome trovato, possiamo uscire dal loop di analisi
                        }
                        
                        // Saltiamo al prossimo blocco di dati (offset = offset precedente + lunghezza del blocco + 1 byte che indica la lunghezza stessa)
                        data_offset += field_length + 1; 
                    }
                    
                    // Stampa finale dei risultati
                    printf("[%02d/30] MAC: %s | RSSI: %4d dBm | Nome: %s\n", i+1, addr_str, rssi, device_name);
                }
            }
        }
    }

    printf("--------------------------------------------------\n");
    printf("Scansione completata. Spegnimento della radio...\n");
    hci_le_set_scan_enable(sock, 0x00, 0x00, 1000);
    
    close(sock);
    return 0;
}
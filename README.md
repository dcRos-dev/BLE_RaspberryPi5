# Bluetooth Low Energy su Raspberry pi 5

Esercizi vari in C/C++ per apprendere il protocollo BLE in ambiente ubuntu/linux. 

Dato che a differenza di una MCU come l'ESP32, una scheda Raspberry ha un'architettura modulare,
dove processore e il chip Cypress/Infineon (chip per il bluetooth) sono separati e comunicano con un BUS UART, è doveroso affidarsi all **HCI (Host Controller Interface)** per 
permettere una corretta comunicazione tra programma ed hardware.


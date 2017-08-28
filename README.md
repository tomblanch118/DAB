# DAB
Electronics schematics, mechanical design and code for the Defuse a Bomb team building activity.

## PCB Version 1.0
There are a few issues with the first version of the PCB, I used A7 and A6 for controlling the RGB LEDs and as the spare io pin. I missed the fact that these pins are analog inputs only (straight to the analog mux). As such version 1 of the pcb requires a few bodge wires from the icsp pins to the leds and buzzer to make it work. As the icsp pins are only used once to burn the bootloader this isnt really a problem and version 2 of the board will fix this. 

You can power the board from an ftdi cable/board however this results in backpowering the 5v regulator/no power to the 3.3v regulator. So far this hasnt proved to be a problem but it probably isnt ideal, YMMV. In future the 5v from the usb bus could be connected to vin but there is a good chance that the 5v regulators dropout voltage will mean that this wont work.

 


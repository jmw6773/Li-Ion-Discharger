# Li-Ion-Discharger
Arduino discharger for Li-Ion batteries with selectable stop voltage and fan speed.

Original Design and Code by Adam Welch
* http://AdamWelch.Uk
* YouTube Video: https://www.youtube.com/embed/qtws6VSIoYk

Modified and extended features added by Jonathan Wells


Features
- Able to discharge 4 18650 batteries simultaneously 
- Setup menu when battery is inserted to select the target discharge voltage.  
- Status LEDs for each battery (passively controlled by Mosfet gate signal) 
- Status monitoring and data transmission through bluetooth 
- Max current is at full charge and determined by the load resistors
  * I’m using 10W 8ohm resistors, so my max current is 525mA. This decreases as the battery discharges 
- Fan pin allows PWM switching a Mosfet to drive a small fan to cool the load resistors (heat sink highly recommended)
 * Be sure to have a fly-back diode on the fan 

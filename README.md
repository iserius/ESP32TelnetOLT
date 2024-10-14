# ESP32 Telnet to OLT Relay
ESP32 Telnet Client to check and control C320 ZTE OLT and config ONU 

its just a Telnet CLient using ESP32 in a simple Terminal Like Web browser
with 8 custom preset command that can save in EEPROM and editable auto script for ONU activation

default login username : adnim password : minda

  ESP32 telnet wraper for OLTC320  12 october 2024
	  
      - v0.5  - add OTA function
      - v0.6  - added status bar header
      - v0.7  - add EEPROM button save function
      - v0.8  - add ONU config script
		- add editable auto config script
		- fix long value result
		- status indicator still not working
		- if preset button is gone, preset buttton get wrong EEPROM address so it not showe,
		make sure erase all flash / EEPROM and generate new config file
  		- fix .replace() calls to use a global regular expression
		- update Status time , heap, temperature, Wifi Signal, Voltage
		- v0.8z dark themes      
		- add simple login page

 

<img width="861" alt="Screenshot 2024-10-14 at 18 13 18" src="https://github.com/user-attachments/assets/251b92bf-3a9d-429d-91db-714c1390021e">

double click custom preset button to change its Name

<img width="804" alt="Screenshot 2024-10-12 at 13 46 49" src="https://github.com/user-attachments/assets/535c8be3-57e3-442d-b510-92ca92ecfe40">
===================================================================================================

<img width="753" alt="Screenshot 2024-10-14 at 18 14 06" src="https://github.com/user-attachments/assets/9afcaf44-0f1d-4b70-a5ff-99d40d9b739b">

===================================================================================================
<img width="538" alt="Screenshot 2024-10-14 at 18 32 39" src="https://github.com/user-attachments/assets/5ffc9dcd-e212-41c7-a547-c1b30cdef244">





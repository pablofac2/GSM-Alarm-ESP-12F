# <center>GSM Alarm</center>

Using ESP8266 (ESP12F on D1 Mini) and SIM800L for gsm connectivity.

## How it works

### Power management

ESP will always go to sleep except for this situations:
- while being wifi AP for configuration or status reading
- while sending SMSs, calling or during a call

### SMS Message Commands

If the SMS is sent from a known phone, it is not mandatory to write the password.
- Status request: "sXXXX" where XXXX is the operation password.
- Arm: "aXXXX" where XXXX is the operation password.
- Disarm: "dXXXX" where XXXX is the operation password.
- Zone Disable: "zdZXXXX" where XXXX is the operation password and Z is the zone number.
- Zone Enable: "zeZXXXX" where XXXX is the operation password and Z is the zone number.
- Siren Disable: "sdSXXXX" where XXXX is the operation password and S is the siren number.
- Siren Enable: "seSXXXX" where XXXX is the operation password and S is the siren number.
- Configuration parameter request: "*?" where * is the parameter description according wifi initial configuration.
- Configuration parameter set: "*: X" where * is the parameter description according wifi initial configuration, and X is the new value.

### Remote control of Outputs

- Output status override: "oOX" where O is the output number and Z is the status (0, 1 or empty to exit override).



## Done



## Pending

- On wifi a text box to send sms wo sending sms
- Debug o wifi on demand
- Reset sim800 if needed
- Wifi should be available to see status / history
- How to arm / disarm?:
-   Sending an sms
-   Calling
- Delete old SMS time to time
- SMS to ask por battery voltage.
- Read battery voltage periodicaly and call in case of low charge.
- If the alarm was armed and there's a power failure, arm it again on power on.
- after ESP_FIREDTOUT, whait alarmConfig.AutoArmDelaySecs to rearm the alarm
- when disabling a zone because it is in trigger state, to let ESP sleep, take that GPIO out of the waking pins.
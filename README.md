# <center>GSM Alarm</center>

Using ESP8266 (ESP12F on D1 Mini) and SIM800L for gsm connectivity.

## How it works

### Power management

ESP will always go to sleep except for this situations:
- while being wifi AP for configuration or status reading
- while sending SMSs, calling or during a call

### SMS Message Commands

If the SMS is sent from a known phone, it is not mandatory to write the password.
- Status request: "XXXXs" where XXXX is the operation password.
- Arm: "XXXXa" where XXXX is the operation password.
- Disarm: "XXXXd" where XXXX is the operation password.
- Zone Disable: "XXXXzdZ" where XXXX is the operation password and Z is the zone number.
- Zone Enable: "XXXXzeZ" where XXXX is the operation password and Z is the zone number.
- Siren Disable: "XXXXsdS" where XXXX is the operation password and S is the siren number.
- Siren Enable: "XXXXseS" where XXXX is the operation password and S is the siren number.
- Siren Forced: "XXXXsfBS" where XXXX is the operation password, B is the state 0 or 1, and S is the siren number.
- Configuration parameter request: "*?" where * is the parameter description according wifi initial configuration.
- Configuration parameter set: "*: X" where * is the parameter description according wifi initial configuration, and X is the new value.
- Fire alarm: XXXXf
- Get commands: XXXXh, XXXXhelp, XXXX?

### Push button

- Press to arm/disarm

## Done

- turn off sim800 led light
- on AlarmLoop() test if it was read too recently, skip this read
- Reset sim800 if needed
- How to arm / disarm?:
-   Sending an sms
-   Calling
-   pushbutton
- SMS to ask por battery voltage.
- after ESP_FIREDTOUT, whait alarmConfig.AutoArmDelaySecs to rearm the alarm
- Read battery voltage periodicaly and call in case of low charge.
- Test consumption of RX pin as output in high and low.
-   RESULT: TX consumes 1mA more when LOW as OUTPUT
-   RESULT: RX consumes 12mA more when LOW as OUTPUT
- Test if SoftwareSerial can work with RX (GPIO3) and TX (GPIO1) inverted. YES!!! IT WORKS!!!!
- BatteryLow properties:
-   Min Battery
-   Max Battery
-   SMS
-   CALL
- New siren properties:
-   Emit a beep when arming or disarming. DONE
-   To be a flashing LED when the alarm is armed.DONE
-   Not delayed to sound as soon as a delayed zone gets triggered (before delay time occurs) and during the activation delay
- New Zones properties:
-   Push button (to arm/disarm/fire alarm)
- New Alarm Properties:
-   Activation delay
- SMS to enable again wifi

## Pending

- fix bug: siren stops sounding, rearming too soon?
- sensors history over wifi
- read again all sms when a new one comes
- siren sound on first advice property
- simultaneous trigger window time
- TriggerNC propertie of zones not working
- To be able to modify properties sending sms
- On wifi a text box to send sms wo sending sms
- Debug o wifi on demand
- Wifi should be available to see status / history
- Delete old SMS time to time
- If the alarm was armed and there's a power failure, arm it again on power on.

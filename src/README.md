# <center>GSM Alarm</center>

Using ESP8266 (ESP12F on D1 Mini) and SIM800L for gsm connectivity.

## How it works

### Power management

ESP will always go to sleep except for this situations:
- while being wifi AP for configuration or status reading
- while sending SMSs, calling or during a call

### Remote control of Outputs



## Done



## Pending

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
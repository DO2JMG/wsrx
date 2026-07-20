# wsrx and ka9q installation with RTL-SDR, 
# using more than one rtl-sdr

## Installing KA9Q Radio

KA9Q Radio is used as the SDR backend. The example below builds KA9Q from source and installs it system-wide.

```
cd ~
git clone https://github.com/ka9q/ka9q-radio.git
cd ka9q-radio
git checkout e1224dcd1991637ba8e1caa68cd802e1b22933de
make -j$(nproc)
sudo make install
```

Add your user to the radio group and log out/in afterwards:

```
sudo usermod -aG radio $(whoami)
```

For best performance, create FFTW wisdom for the Airspy Mini. This can take a long time.

```
cd /etc/fftw
sudo fftwf-wisdom -v -T 1 -o nwisdom rof300000 cob2400 cob1250 cob1202 cob1200
sudo cp -i nwisdom wisdomf
```

## Create the KA9Q radiod configuration:

First rtl-sdr

```
sudo nano radiod@rtl_1.conf
```

```
[global]
hardware = rtlsdr
status = rtl_1.local

[rtlsdr]
device = rtlsdr
description = "rtl_1"
gain = 38.0
bias = false
agc = false
serial = 00000102 # Change serial
```

Second rtl-sdr

```
sudo nano radiod@rtl_2.conf
```

```
[global]
hardware = rtlsdr
status = rtl_2.local

[rtlsdr]
device = rtlsdr
description = "rtl_2"
gain = 38.0
bias = false
agc = false
serial = 00000102 # Change serial
```

Enable and start KA9Q:

```
sudo systemctl daemon-reload

sudo systemctl enable radiod@rtl_1
sudo systemctl start radiod@rtl_1

sudo systemctl enable radiod@rtl_2
sudo systemctl start radiod@rtl_2
```
Check if KA9Q is running:

```
sudo systemctl status radiod@rtl_1

sudo systemctl status radiod@rtl_2
```

## Installing wsrx 

```
cd $home

git clone https://github.com/DO2JMG/wsrx.git

cd wsrx

make clean
make

cd decoder
make clean
make
```

## Configuring wsrx

```
nano /home/pi/wsrx/config.ini
```
Change your call sign and your coordinates. The call sign does not have to be an amateur radio call sign.

```
[station]
callsign = NOCALL
lat = 52.123456
lon = 8.123456
alt = 100
```

Change your radio settings for ka9q

```
[radio1]
name = rtl1
radio_name = rtl_1.local
pcm_name = rtl_1-pcm.local
min_mhz = 402.0
max_mhz = 404.0

[radio2]
name = rtl2
radio_name = rtl_2.local
pcm_name = rtl_2-pcm.local
min_mhz = 404.0
max_mhz = 406.0
```

Make the executables and start scripts executable:

```
chmod +x /home/pi/wsrx/wsrx.sh
chmod +x /home/pi/wsrx/update.sh
```

Starting wsrx and the web interface

Start wsrx manually:

```
cd /home/pi/wsrx

./wsrx.sh 
```

Stop wsrx manually:

```
cd /home/pi/wsrx

./wsrx.sh stop
```

The web interface listens on port 8073 by default. Open it in your browser with the IP address of your receiver, for example:
```
http://receiver-ip-address:8073/
```

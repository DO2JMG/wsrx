### wxrx Radiosonde decoder, monitoring, scan and decoder status

<img width="1163" height="817" alt="screen" src="https://github.com/user-attachments/assets/5115b4b5-745f-42bd-826d-8c3da8e23535" />

<p>&nbsp;</p>
<p>&nbsp;</p>

This guide installs KA9Q Radio, the wsrx radiosonde receiver software and the required decoder programs. wsrx uses KA9Q as SDR backend and expects all decoder binaries in the decoder subdirectory next to the wsrx application.

### Requirements

The installation is intended for Linux systems such as Raspberry Pi OS, Debian or Ubuntu with an Airspy Mini receiver. Run the commands as a user with sudo rights.

```
sudo apt update
sudo apt install -y avahi-utils build-essential make gcc g++ git wget unzip rsync time \
  libairspy-dev libairspyhf-dev libavahi-client-dev libbsd-dev libfftw3-dev \
  libhackrf-dev libiniparser-dev libncurses5-dev libopus-dev librtlsdr-dev \
  libusb-1.0-0-dev libusb-dev portaudio19-dev libasound2-dev libogg-dev \
  uuid-dev libsamplerate-dev
```

### Installing wsrx

```
cd $home

https://github.com/DO2JMG/wsrx

cd wsrx

make clean
make
```

### Configuring wsrx

```
nano /home/pi/wsrx/config.ini
```

Change your call sign and your coordinates. The call sign does not have to be an amateur radio call sign.

```
[station]
callsign = DO2JMG
lat = 52.014168
lon = 8.474337
alt = 100
```

Make the executables and start scripts executable:

```
chmod +x /home/pi/wsrx/wsrx /home/pi/wsrx/wsrx-web /home/pi/wsrx/wsrx.sh /home/pi/wsrx/wsrx-web.sh
```

### Starting wsrx and the web interface

KA9Q must be fully installed before wsrx is started.

Start wsrx manually for the first test:

```
cd /home/pi/wsrx
./wsrx.sh start
./wsrx.sh status
./wsrx.sh log
```

Start the web interface:

```
cd /home/pi/wsrx
./wsrx-web.sh start
./wsrx-web.sh status
```

The web interface listens on port 8073 by default. Open it in your browser with the IP address of your receiver, for example:

```
http://receiver-ip-address:8073/
```

### Installing KA9Q Radio

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

### Configuring KA9Q for Airspy Mini

Create the KA9Q radiod configuration:

```
sudo nano /etc/radio/radiod@wettersonde_rx.conf
```

```
[global]
hardware = airspy
mode = fm
status = wettersonde.local
iface = lo
ttl = 0
data = wettersonde-pcm.local

[airspy]
device = airspy
description = "wettersonde_rx"
frequency = 407m0

[telemetry]
freq = "401m50"

[manual-400]
freq = 0
ttl = 0
```

Enable and start KA9Q:

```
sudo systemctl daemon-reload
sudo systemctl enable radiod@wettersonde_rx
sudo systemctl start radiod@wettersonde_rx
```

Check if KA9Q is running:

```
systemctl status radiod@wettersonde_rx --no-pager
avahi-browse -art | grep -E "wettersonde|ka9q|pcm"
```

### Installing the decoder binaries

wsrx requires the decoder binaries in the decoder folder next to the wsrx executable. If one of these files is missing, wsrx will stop with an error message.

```
cd ~
git clone https://github.com/rs1729/RS.git

# Build the radiosonde decoders
cd ~/RS/demod/mod
make

# Build dft_detect for signal detection
cd ~/RS/scan

gcc dft_detect.c -lm -o dft_detect
```

Create the decoder directory and copy the required files. The radiosonde decoders are in RS/demod/mod, but dft_detect is in RS/scan:

```
mkdir -p /home/pi/wsrx/decoder
cp ~/RS/demod/mod/rs41mod /home/pi/wsrx/decoder/
cp ~/RS/demod/mod/dfm09mod /home/pi/wsrx/decoder/
cp ~/RS/demod/mod/m10m20mod /home/pi/wsrx/decoder/
cp ~/RS/demod/mod/imet54mod /home/pi/wsrx/decoder/
cp ~/RS/scan/dft_detect /home/pi/wsrx/decoder/
chmod +x /home/pi/wsrx/decoder/*
```

Verify the decoder files:

```
ls -lh /home/pi/wsrx/decoder/rs41mod \
       /home/pi/wsrx/decoder/dfm09mod \
       /home/pi/wsrx/decoder/m10m20mod \
       /home/pi/wsrx/decoder/imet54mod \
       /home/pi/wsrx/decoder/dft_detect
```

### Optional: systemd service files

The included start scripts already write log files into the wsrx directory. If you want wsrx and the web interface to start automatically after boot, create these systemd services.

```
sudo nano /etc/systemd/system/wsrx.service
```

```
[Unit]
Description=wsrx radiosonde receiver
After=network-online.target radiod@wettersonde_rx.service
Wants=network-online.target radiod@wettersonde_rx.service

[Service]
Type=forking
WorkingDirectory=/home/pi/wsrx
ExecStart=/home/pi/wsrx/wsrx.sh start
ExecStop=/home/pi/wsrx/wsrx.sh stop
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

```
sudo nano /etc/systemd/system/wsrx-web.service
```
```
[Unit]
Description=wsrx web interface
After=network-online.target wsrx.service
Wants=network-online.target

[Service]
Type=forking
WorkingDirectory=/home/pi/wsrx
ExecStart=/home/pi/wsrx/wsrx-web.sh start
ExecStop=/home/pi/wsrx/wsrx-web.sh stop
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start both services:

```
sudo systemctl daemon-reload
sudo systemctl enable wsrx.service wsrx-web.service
sudo systemctl start wsrx.service wsrx-web.service
```

Show logs:

```
tail -f /home/pi/wsrx/logs/wsrx.log
tail -f /home/pi/wsrx/logs/wsrx-web.log
```

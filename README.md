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

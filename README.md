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

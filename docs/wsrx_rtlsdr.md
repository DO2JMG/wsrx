## wsrx and ka9q installation with RTL-SDR, 
## using more than one rtls-sdr

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


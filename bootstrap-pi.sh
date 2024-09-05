#!/bin/bash

# Raspberry Pi OS 64-bit base, using Raspberry Pi Imager (enable SSH and set password in settings)
# Once imaged, cd to the boot drive and run `sed -i '' "s/rootwait/rootwait ip=<ip>/" cmdline.txt` to set a static ip
# Before running this script, run `sudo apt-get -y update && sudo apt-get -y upgrade && sudo apt-get -y autoremove`, then `sudo reboot`
# To run this script: `sudo apt-get -y install git && git clone https://github.com/MattIPv4/home-automation && ./home-automation/bootstrap-pi.sh`

set -e -o pipefail

# Install firewall
sudo apt-get -y install ufw

# Configure firewall
echo "y" | sudo ufw reset
sudo ufw allow 22
sudo ufw default deny
echo "y" | sudo ufw enable
sudo ufw status verbose

# Install Python
sudo apt-get -y install python3 python3-pip python3-venv

# Install PyDMXControl dependencies
sudo apt-get -y install libsdl2-2.0-0 libsdl2-mixer-2.0-0

# Install divoom-control dependencies
sudo apt-get -y install build-essential libbluetooth-dev

# Install FNM
curl -fsSL https://fnm.vercel.app/install | bash
sed -i "s/fnm env\`/fnm env --use-on-cd\`/" ~/.bashrc
# shellcheck disable=SC1090
source ~/.bashrc

# Install Docker
curl -fsSL https://get.docker.com | sudo sh

# Clone PyDMXControl repo
git clone https://github.com/MattIPv4/PyDMXControl ~/home-automation/lighting/PyDMXControl

# Clone divoom-control and install
git clone https://github.com/MattIPv4/divoom-control ~/home-automation/divoom-control
cd ~/home-automation/divoom-control
fnm use --install-if-missing
npm ci
npm link
cd ~/

# Enable and expose home automation lighting
cd ~/home-automation/lighting
sudo systemctl enable "$PWD/lighting.service"
sudo systemctl start lighting.service
sudo systemctl status lighting.service
sudo ufw allow 8000
cd ~/

# Enable and expose home automation speedtest
cd ~/home-automation/speedtest
sudo systemctl enable "$PWD/speedtest.service"
sudo systemctl start speedtest.service
sudo systemctl status speedtest.service
sudo ufw allow 8080
cd ~/

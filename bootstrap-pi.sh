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

# Install Docker
curl -fsSL https://get.docker.com | sudo sh

# Enable and expose home automation speedtest
cd ~/home-automation/speedtest
sudo systemctl enable "$PWD/speedtest.service"
sudo systemctl start speedtest.service
sudo systemctl status speedtest.service
sudo ufw allow 8080
cd ~/

# Enable and expose home automation smokeping
cd ~/home-automation/smokeping
sudo systemctl enable "$PWD/smokeping.service"
sudo systemctl start smokeping.service
sudo systemctl status smokeping.service
sudo ufw allow 8081
cd ~/

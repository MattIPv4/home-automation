#!/bin/bash

# Raspberry Pi:
#   Raspberry Pi OS 64-bit Lite (no desktop), using Raspberry Pi Imager (enable SSH and set password in settings)
#   Once imaged, cd to the boot drive and run `sed -i '' "s/rootwait/rootwait ip=<ip>/" cmdline.txt` to set a static ip
#   Before running this script, run `sudo rpi-update`, then `sudo reboot`
#
# Jetson Nano:
#   Before installing Armbian, the NVIDIA-provided image may need to be installed first (https://www.nvidia.com/jetsonnano-start/)
#   Armbian Minimal for Jetson Nano, using Etcher (SSH enabled by default as root:1234)
#   Archive image using kernel 6.1 appears to work: https://archive.armbian.com (jetson-nano/archive/Armbian_23.8.1_Jetson-nano_bookworm_current_6.1.50_minimal.img.xz)
#   Before running this script, run `echo "[[ -f ~/.bashrc ]] && . ~/.bashrc" >> ~/.bash_profile && apt install -y avahi-daemon libnss-mdns && sed -i 's/publish-workstation=no/publish-workstation=yes/' /etc/avahi/avahi-daemon.conf`, then `sudo reboot`
#
# Before running this script, if required `sudo hostnamectl hostname <hostname>`, run `sudo apt-get -y update && sudo apt-get -y upgrade && sudo apt-get -y autoremove`, then `sudo reboot`
# To run this script: `sudo apt-get -y install git && sudo git clone https://github.com/MattIPv4/home-automation /opt/home-automation && sudo chown -R $(whoami) /opt/home-automation && /opt/home-automation/bootstrap.sh`

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

# Enable and expose home automation postgres
sudo systemctl enable /opt/home-automation/postgres/postgres.service
sudo systemctl start postgres.service
sudo systemctl status postgres.service
sudo ufw allow 5432

# Enable and expose home automation backup
cp /opt/home-automation/backup/conf/s3.env.example /opt/home-automation/backup/conf/s3.env
nano /opt/home-automation/backup/conf/s3.env || true
sudo systemctl enable /opt/home-automation/backup/backup.service
sudo systemctl start backup.service
sudo systemctl status backup.service

# Enable and expose home automation grafana
sudo systemctl enable /opt/home-automation/grafana/grafana.service
sudo systemctl start grafana.service
sudo systemctl status grafana.service
sudo ufw allow 8080

# Enable and expose home automation speedtest
sudo systemctl enable /opt/home-automation/speedtest/speedtest.service
sudo systemctl start speedtest.service
sudo systemctl status speedtest.service
sudo ufw allow 8081

# Enable and expose home automation smokeping
sudo systemctl enable /opt/home-automation/smokeping/smokeping.service
sudo systemctl start smokeping.service
sudo systemctl status smokeping.service
sudo ufw allow 8082

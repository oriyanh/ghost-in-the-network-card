#!/bin/sh

sudo install net-tools
sudo apt update
sudo apt upgrade -y
sudo apt --fix-broken install

sudo apt install minicom -y
sudo apt install screen -y
sudo apt install pv -y
sudo apt install wireshark -y

rm -rf /tmp/doca
mkdir /tmp/doca
cd /tmp/doca
wget https://content.mellanox.com/ofed/MLNX_OFED-24.04-0.6.6.0/MLNX_OFED_LINUX-24.04-0.6.6.0-ubuntu22.04-x86_64.tgz
tar -xvf MLNX_OFED_LINUX-24.04-0.6.6.0-ubuntu22.04-x86_64.tgz
cd MLNX_OFED_LINUX-24.04-0.6.6.0-ubuntu22.04-x86_64
sudo ./mlnxofedinstall --all -v
sudo /etc/init.d/openibd restart

cp -r /home/oriyanh/nvidia_downloads/ /tmp/doca/

# set largo IB1 ip address
sudo ip addr add 192.168.200.3/24 dev enp4s0f1np1

# test kTLS
openssl s_client -connect 4.4.4.4:443 -tls1_2

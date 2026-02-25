#!/bin/sh

sudo install net-tools
sudo apt upgrade
sudo apt update
sudo apt --fix-broken install

sudo apt install minicom -y
sudo apt install screen -y
sudo apt install pv -y
sudo apt install python3-pi -y

rm -rf /tmp/doca
mkdir /tmp/doca
cd /tmp/doca

cp -r /home/oriyanh/nvidia_downloads/ /tmp/doca/

wget http://www.mellanox.com/downloads/ofed/RPM-GPG-KEY-Mellanox-SHA256
sudo rpm --import RPM-GPG-KEY-Mellanox-SHA256
public_key_installed=`rpm -q gpg-pubkey --qf '%{NAME}-%{VERSION}-%{RELEASE}\t%{SUMMARY}\n' | grep Mellanox`
if [ -z "$public_key_installed" ]
    then
echo "Mellanox GPG key installed"
else
    exit

# install doca host tools
sudo dpkg -i doca-host-repo-ubuntu2204_2.5.0-0.0.1.2.5.0108.1.23.10.1.1.9.0_amd64.deb
sudo apt update
sudo apt install doca-tools -y
sudo apt install doca-sdk -y
sudo apt install doca-ofed -y
sudo apt-get remove --auto-remove openvswitch-common
sudo apt-get remove --auto-remove openvswitch-switch
sudo apt install openvswitch-switch=2.17.8-1 openvswitch-common=2.17.8-1 -y
sudo apt install doca-runtime -y

# get device ID
sudo mst start
sudo mst status -v

# parse device id into device_id
bf1_mst='/dev/mst/mt41686_pciconf0'
bf1_pci='ca:00.0'
bf1_rdma='mlx5_2'
bf2_mst='/dev/mst/mt41686_pciconf0.1'
bf2_pci='ca:00.1'
bf2_rdma='mlx5_3'

device_id=$bf1_mst
mlxconfig -d $device_id -y reset


sudo bfb-install --rshim rshim0 --bfb DOCA_2.5.0_BSP_4.5.0_Ubuntu_22.04-1.23-10.prod.bfb

# Enable internet access to internet on DPU, run this on HOST
sudo echo 1 | sudo tee /proc/sys/net/ipv4/ip_forward
sudo iptables -t nat -A POSTROUTING -o eno8303 -j MASQUERADE
# IF still no internet access:
sudo iptables -A FORWARD -o eno8303 -j ACCEPT
sudo iptables -A FORWARD -m state --state ESTABLISHED,RELATED -i eno8303 -j ACCEPT
sudo ifconfig tmfifo_net0 192.168.100.1 netmask 255.255.255.0 up

sudo ipmitool power cycle

sudo scp -r doca-dpu-repo-ubuntu2204-local_2.5.0107-1.23.10.1.2.0.0.bf.4.5.0.12993_arm64.deb ubuntu@192.168.100.2:/tmp/

# Give LAN IPs to I/B interfaces:
sudo ip addr add 10.10.1.1/24 dev enp202s0f0np0
sudo ip addr add 10.10.1.2/24 dev enp202s0f1np1


## test kTLS

#  The following example checks kTLS hardware offload on the tested setup by tracking Rx and Tx TLS on device counters:
ethtool -S $iface | grep -i 'tx_tls_encrypted\|rx_tls_decrypted' # ($iface is the interface that offloads)

# To check kTLS over kernel counters:
cat /proc/net/tls_stat


# For APP SHIELD, install the following packages:
sudo tee /etc/apt/sources.list.d/ddebs.list << EOF
deb http://ddebs.ubuntu.com/ $(lsb_release -cs) main restricted universe multiverse
deb http://ddebs.ubuntu.com/ $(lsb_release -cs)-updates main restricted universe multiverse
deb http://ddebs.ubuntu.com/ $(lsb_release -cs)-proposed main restricted universe multiverse
EOF

sudo apt install ubuntu-dbgsym-keyring
sudo apt-get update
sudo apt-get install linux-image-$(uname -r)-dbgsym

wget https://github.com/volatilityfoundation/dwarf2json/releases/download/v0.9.0/dwarf2json-darwin-amd64

pip install psutil
pip install pdbparse

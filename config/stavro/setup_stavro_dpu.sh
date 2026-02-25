#!/bin/sh

##### DPU SETUP


# FW Update
# Restart host afterwards
sudo /opt/mellanox/mlnx-fw-updater/mlnx_fw_updater.pl --force-fw-update

sudo dpkg -i /tmp/doca-dpu-repo-ubuntu2204-local_2.5.0107-1.23.10.1.2.0.0.bf.4.5.0.12993_arm64.deb
sudo apt update
sudo apt install doca-runtime doca-sdk -y
sudo mokutil --sb-state  # Checks if secure boot is enabled

sudo /etc/init.d/openibd restart

# Default net configuration:
# ubuntu@localhost:~$ cat /etc/netplan/50-cloud-init.yaml
# # This file is generated from information provided by the datasource.  Changes
# # to it will not persist across an instance reboot.  To disable cloud-init's
# # network configuration capabilities, write a file
# # /etc/cloud/cloud.cfg.d/99-disable-network-config.cfg with the following:
# # network: {config: disabled}
# network:
#     ethernets:
#         oob_net0:
#             dhcp4: true
#         tmfifo_net0:
#             addresses:
#             - 192.168.100.2/30
#             dhcp4: false
#             nameservers:
#                 addresses:
#                 - 192.168.100.1
#             routes:
#             -   metric: 1025
#                 to: 0.0.0.0/0
#                 via: 192.168.100.1
#     renderer: NetworkManager
#     version: 2

# Add this to /etc/netplan/01-netcfg.yaml to get internet access for DPU
# This sets static IP for oob_net0:
# sudo tee -a /etc/netplan/01-netcfg.yaml << EOF
# sudo tee -a ~/test.txt << EOF
# network:
#     ethernets:
#         oob_net0:
#             dhcp4: false
#             addresses:
#             - 132.65.104.20/22
#             gateway4: 132.65.104.1
#             nameservers:
#                 addresses:
#                 - 132.65.116.7
#         tmfifo_net0:
#             addresses:
#             - 192.168.100.2/30
#             dhcp4: false
#             nameservers:
#                 addresses:
#                 - 192.168.100.1
#             routes:
#             -   metric: 1025
#                 to: 0.0.0.0/0
#                 via: 192.168.100.1
#     renderer: NetworkManager
#     version: 2
# EOF
# sudo ethtool -r oob_net0
# sudo netplan apply

sudo echo "nameserver 132.65.116.7" | sudo tee /etc/resolv.conf


# SETUP DRIVERS
sudo apt update
sudo apt --fix-broken install
sudo apt install gdb -y

# Install mellanox GPG key
sudo apt install -y gnupg2
export DOCA_REPO="https://linux.mellanox.com/public/repo/doca/2.5.0/ubuntu22.04/dpu-arm64"
sudo curl $DOCA_REPO/GPG-KEY-Mellanox.pub | sudo gpg --dearmor | sudo tee  /etc/apt/trusted.gpg.d/GPG-KEY-Mellanox.pub
sudo curl https://linux.mellanox.com/public/repo/doca/2.5.0/ubuntu22.04/dpu-arm64/Release.gpg | sudo gpg --dearmor | sudo tee  /etc/apt/trusted.gpg.d/GPG-KEY-Mellanox.pub

sudo echo "deb [signed-by=/etc/apt/trusted.gpg.d/GPG-KEY-Mellanox.pub] $DOCA_REPO ./" | sudo tee /etc/apt/sources.list.d/doca.list

sudo ip addr add 192.168.200.7/24 dev p0
sudo ip addr add 192.168.200.8/24 dev pf0hpf
sudo ip addr add 192.168.200.10/24 dev en3f0pf0sf0

## Allocate huge pages on DPU (Required when using DPDK)
sudo mkdir -p /hugepages
sudo mount -t hugetlbfs hugetlbfs /hugepages
sudo tee /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages << EOF
4096
EOF



# MOUNT DPU SHARE FOLDER
mount 10.0.0.10:/vol/myshare/ myshare/

# Make sure we are running in Embedded Controller mode
# run this to check if INTERNAL_CPU_MODEL is set to 0 (SEPARATED_HOST), we need to set it to 1
sudo mlxconfig -d /dev/mst/mt41686_pciconf0 q | grep -i internal_cpu
sudo mlxconfig -d /dev/mst/mt41686_pciconf0 s INTERNAL_CPU_MODEL=1
# and make sure changes were done. Power cycle on the host is necessary after this
sudo mlxconfig -d /dev/mst/mt41686_pciconf0 q | grep -i internal_cpu

# Enable new Scalable Functions
# On DPU, then power cycle DPU:
sudo mlxconfig -d 0000:03:00.0 s PF_BAR2_ENABLE=0 PER_PF_NUM_SF=1 PF_TOTAL_SF=236 PF_SF_BAR_SIZE=10

# If switch is set to legacy mode, need to change to switchdev
echo switchdev > /sys/class/net/p0/compat/devlink/mode
#1. Create  SF 4 & 5
sudo /opt/mellanox/iproute2/sbin/mlxdevm port add pci/0000:03:00.0 flavour pcisf pfnum 0 sfnum 4
sudo /opt/mellanox/iproute2/sbin/mlxdevm port add pci/0000:03:00.0 flavour pcisf pfnum 0 sfnum 5

#TODO need to acquire `pci/0000:03:00.0/229376` dynamically, this suffix changes each time
#2. Configure SF 4 and 5
# SF index is pci/0000:03:00.0/229376, representor name is en3f0pf0sf4
# set HW address, and activate SF4
# trust state not supported on this particular card. Otherwise, also set `trust on`
# OR
sudo /opt/mellanox/iproute2/sbin/mlxdevm port function set pci/0000:03:00.0/229377 hw_addr 02:9e:0f:d4:58:dc state active trust on
sudo /opt/mellanox/iproute2/sbin/mlxdevm port function set pci/0000:03:00.0/229378 hw_addr 02:9e:0f:d4:58:dd state active trust on
#3. Deploy SF 4 and 5
# Unbind sf4 then bind sf4
sudo chmod -R a+wr /sys/bus/auxiliary/drivers/mlx5_core*
echo mlx5_core.sf.4 > sudo tee -a /sys/bus/auxiliary/drivers/mlx5_core.sf_cfg/unbind
echo mlx5_core.sf.4 > sudo tee -a /sys/bus/auxiliary/drivers/mlx5_core.sf/bind
echo mlx5_core.sf.5 > sudo tee -a /sys/bus/auxiliary/drivers/mlx5_core.sf_cfg/unbind
echo mlx5_core.sf.5 > sudo tee -a /sys/bus/auxiliary/drivers/mlx5_core.sf/bind

sudo devlink dev param set auxiliary/mlx5_core.sf.4 name enable_eth value true cmode driverinit
sudo devlink dev param set auxiliary/mlx5_core.sf.4 name enable_rdma value true cmode driverinit
sudo devlink dev param set auxiliary/mlx5_core.sf.3 name enable_eth value true cmode driverinit
sudo devlink dev param set auxiliary/mlx5_core.sf.3 name enable_rdma value true cmode driverinit

rdma link

# pci/0000:03:00.0/229376: type eth netdev eth0 flavour pcisf controller 0 pfnum 0 sfnum 4
#   function:
#     hw_addr 00:00:00:00:00:00 state inactive opstate detached roce true max_uc_macs 128
/opt/mellanox/iproute2/sbin/mlxdevm port show
#To see the available sub-functions, run:

sudo devlink dev show

# Or in parsable JSON format: sudo devlink port show -j/-jp

# For example, if you run the command before creating, configuring, and deploying the SF (using the steps detailed earlier), the output would appear as follows:

# pci/0000:03:00.0
# pci/0000:03:00.1
# auxiliary/mlx5_core.sf.2
# auxiliary/mlx5_core.sf.3 

# After creating, configuring, and deploying the SF, the output would be:

# pci/0000:03:00.0
# pci/0000:03:00.1
# auxiliary/mlx5_core.sf.2
# auxiliary/mlx5_core.sf.3
# auxiliary/mlx5_core.sf.4 

# ubuntu@localhost:~$ sudo mlnx-sf -a show

# SF Index: pci/0000:03:00.0/229376
#   Parent PCI dev: 0000:03:00.0
#   Representor netdev: en3f0pf0sf0
#   Function HWADDR: 02:db:3e:bb:b3:7c
#   Function trust: off
#   Auxiliary device: mlx5_core.sf.2
#     netdev: enp3s0f0s0
#     RDMA dev: mlx5_2

# SF Index: pci/0000:03:00.0/229409
#   Parent PCI dev: 0000:03:00.0
#   Representor netdev: en3f0pf0sf4
#   Function HWADDR: 00:00:00:00:04:00
#   Function trust: on
#   Auxiliary device: mlx5_core.sf.3
#     netdev: enp3s0f0s4
#     RDMA dev: mlx5_3

# SF Index: pci/0000:03:00.0/229410
#   Parent PCI dev: 0000:03:00.0
#   Representor netdev: en3f0pf0sf5
#   Function HWADDR: 00:00:00:00:05:00
#   Function trust: on
#   Auxiliary device: mlx5_core.sf.4
#     netdev: enp3s0f0s5
#     RDMA dev: mlx5_4

# To verify the newly added SFs, and to query what the next SF# would be
sudo apt install tree
tree -l -L 3 -P "mlx5_core.sf." /sys/bus/auxiliary/devices/

## Enable OVS DOCA mode
# Enable default configuration if necessary
for br in $(sudo ovs-vsctl list-br); do sudo ovs-vsctl del-br $br; done           # erasing existing bridges

sudo ovs-vsctl del-br ovsbr1
sudo ovs-vsctl del-br ovsbr2
sudo ovs-vsctl add-br ovsbr1
sudo ovs-vsctl add-port ovsbr1 pf0hpf
# sudo ovs-vsctl add-port ovsbr1 en3f0pf0sf0
sudo ovs-vsctl add-port ovsbr1 en3f0pf0sf4
sudo ovs-vsctl add-br ovsbr2
sudo ovs-vsctl add-port ovsbr2 p0
sudo ovs-vsctl add-port ovsbr2 en3f0pf0sf5


# Output of `sudo ovs-vsctl show` should look like
# Bridge sf_bridge1
#      Port p0
#          Interface p0
#      Port sf_bridge1
#          Interface sf_bridge1
#              type: internal
#      Port en3f0pf0sf4
#          Interface en3f0pf0sf4
# Bridge sf_bridge2
#      Port sf_bridge2
#          Interface sf_bridge2
#              type: internal
#      Port en3f0pf0sf5
#          Interface en3f0pf0sf5
#      Port pf0hpf
#          Interface pf0hpf
# ovs_version: "2.14.1" 


sudo ip addr add 192.168.200.11/24 dev enp3s0f0s4
sudo ip addr add 192.168.200.12/24 dev enp3s0f0s5

sudo ifconfig enp3s0f0s4 up
sudo ifconfig enp3s0f0s5 up

# sudo ethtool --set-priv-flags en3f0pf0sf0 sniffer on
sudo ethtool --set-priv-flags en3f0pf0sf4 sniffer on
sudo ethtool --set-priv-flags en3f0pf0sf5 sniffer on
sudo ethtool --set-priv-flags enp3s0f0s4 sniffer on
sudo ethtool --set-priv-flags enp3s0f0s5 sniffer on
# sudo ethtool enp3s0f0s0 sniffer on
sudo ethtool enp3s0f0s4 sniffer on
sudo ethtool enp3s0f0s5 sniffer on


sudo ovs-vsctl set Open_vSwitch . other_config:dpdk-extra="-a 0000:00:00.0"
# sudo ovs-vsctl set Open_vSwitch . other_config:dpdk-extra="-w 0000:03:00.0,representor=[0,65535],dv_flow_en=1,dv_xmeta_en=1,sys_mem_en=1"
sudo ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-init=true  # This enables OVS-DPDK mode
sudo ovs-vsctl --no-wait set Open_vSwitch . other_config:doca-init=true  # This enables OVS-DOCA mode
sudo ovs-vsctl set Open_vSwitch . other_config:hw-offload=true  # This enables OVS hardware offload
# OVS-DPDK supports parallel insertion and deletion of offloads (flow & CT). While multiple threads are supported, by default only one is used.
# To configure multiple threads: 
# sudo ovs-vsctl set Open_vSwitch . other_config:n-offload-threads=3

sudo systemctl restart openvswitch-switch.service


# Connection Tracking Offload 

# Connection tracking enables stateful packet processing by keeping a record of currently open connections.
# OVS flows using connection tracking can be accelerated using advanced Network Interface Cards (NICs) by offloading established connections.

# To view offloaded connections, run: 
sudo ovs-appctl dpctl/offload-stats-show



# start PCI config service
# Run this on the host, then repeat this on the DPU
sudo mst start
sudo mst status -v
# Start regex engine (on host)
sudo systemctl start mlx-regex
sudo systemctl status mlx-regex

## Docker setup
sudo systemctl daemon-reload
sudo systemctl start docker

# Start container with huge pages
# Dev container
sudo docker run -v /:/doca_devel -v /dev/hugepages:/dev/hugepages --privileged --net=host -it -e container=docker nvcr.io/nvidia/doca/doca:2.5.0-devel


# To test if DPDK works using our SF, use dpdk-testpmd
sudo /opt/mellanox/dpdk/bin/dpdk-devbind.py --status
sudo env LD_LIBRARY_PATH=/opt/mellanox/dpdk/lib/aarch64-linux-gnu /opt/mellanox/dpdk/bin/dpdk-testpmd -a 03:00.0,representor=[0,65535] --socket-mem=1024 -- --total-num-mbufs=131000 -i -a


## Run traffic-dump DPU application
sudo ./build/traffic-dump -l 1 -n 1 -a 03:00.0,representor=[0,65535] --socket-mem=1024





############## Other stuff ###########

# Compile suricata rules
#e.g. 
# drop tcp any any -> any any (msg:\"some msg\"; flow:to_server; pcre:\"/some_regex_pattern/I\"; sid:%d;)\n
# drop drop tcp any any -> any any (msg:\"some msg\"; flow:to_server; tls.sni; pcre:\"/some_regex_pattern/\"; sid:%d;)\n
# sudo doca_url_filter --json url_filter_config.json
# sudo /opt/mellanox/doca/applications/url_filter/bin/doca_url_filter -a auxiliary:mlx5_core.sf.4,sft_en=1 -a auxiliary:mlx5_core.sf.5,sft_en=1 -c3 -- -p -a 03:00.0

##### Setting up rxp (from DOCA 2.2.0 archives)
# Install rxp compiler
wget https://linux.mellanox.com/public/repo/doca/2.2.0/ubuntu22.04/aarch64/rxp-compiler_23.07.1_arm64.deb
wget https://linux.mellanox.com/public/repo/doca/2.2.0/ubuntu22.04/aarch64/librxpcompiler-dev_23.07.1_arm64.deb
sudo apt install ./rxp-compiler_23.07.1_arm64.deb
sudo apt install ./librxpcompiler-dev_23.07.1_arm64.deb

# compile rules (file=session.rules)
# RUN DNS FILTER
# rxpc -V bf2 -f /doca_devel/opt/mellanox/doca/applications/dns_filter/bin/regex_rules.txt -p 0.01 -o /tmp/regex_rules
# /doca_devel/opt/mellanox/doca/applications/dns_filter/bin/doca_dns_filter -a auxiliary:mlx5_core.sf.3,dv_flow_en=2 -a auxiliary:mlx5_core.sf.4,dv_flow_en=2 -- -l 60 -p 03:00.0 --rules /tmp/regex_rules.rof2.binary --type allow

# RUN SESSION ID FILTER
# compile rules (file=session.rules)
# rxpc -V bf2 -f /doca_devel/opt/mellanox/doca/applications/session_filter/bin/session.rules -p 0.01 -o /tmp/regex_rules
# /doca_devel/opt/mellanox/doca/applications/build/session_filter/src/doca_session_filter  -a auxiliary:mlx5_core.sf.3,dv_flow_en=2 -a auxiliary:mlx5_core.sf.4,dv_flow_en=2 -- -l 60 -p 03:00.0 --rules /tmp/regex_rules.rof2.binary --type allow


# #### DISABLE FIREWALL
# rm -f /etc/iptables/rules.v4
# iptables -F

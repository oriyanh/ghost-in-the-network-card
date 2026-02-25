#!/bin/bash

sudo apt update
sudo apt upgrade -y

sudo apt install -y python3 python3-dev python3-venv libaugeas-dev gcc

sudo python3 -m venv /opt/certbot/
sudo /opt/certbot/bin/pip install --upgrade pip


sudo /opt/certbot/bin/pip install certbot certbot-nginx
sudo ln -s /opt/certbot/bin/certbot /usr/bin/certbot

sudo certbot --nginx


echo "0 0,12 * * * root /opt/certbot/bin/python -c 'import random; import time; time.sleep(random.random() * 3600)' && sudo certbot renew -q" | sudo tee -a /etc/crontab > /dev/null
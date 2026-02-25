cd /tmp
# cd to openssl directory and create certificate & key
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes

# Add key and certificate from first stage to nginx config
sudo cp /tmp/key.pem /usr/local/nginx/conf/ && sudo cp /tmp/cert.pem /usr/local/nginx/conf/


######
## Setup nginx with kTLS
## https://davidvn.com/2022/10/26/how-to-build-and-install-nginx-from-source-on-ubuntu-22-04/
## https://www.f5.com/company/blog/nginx/improving-nginx-performance-with-kernel-tls
## https://blog.salrashid.dev/en/articles/2022/kernel_tls/

# Load kTLS module
sudo modprobe tls
# Install deps
sudo apt install -y unzip build-essential \
libpcre3 libpcre2-dev libpcre3-dev zlib1g \
zlib1g-dev libssl-dev libgd-dev libxml2 \
libxml2-dev uuid-dev libmaxminddb-dev \
libgeoip-dev libxslt1-dev

# Setup user for nginx
sudo addgroup nginx
sudo adduser nginx --system \
--home=/var/www --disabled-login \
--disabled-password --ingroup nginx

# Setup working dirs
sudo mkdir -p /var/lib/nginx/body
sudo mkdir /var/lib/nginx/proxy
sudo mkdir /var/lib/nginx/fastcgi
sudo mkdir /var/lib/nginx/uwsgi
sudo mkdir /var/lib/nginx/scgi
sudo mkdir -p /usr/lib/nginx/modules
sudo mkdir /etc/nginx/sites-available
sudo mkdir -p /etc/nginx/ssl  # SSL Certs dir

# install nginx
cd ~/
mkdir repos/ && cd repos/

wget https://github.com/openssl/openssl/releases/download/openssl-3.2.6/openssl-3.2.6.tar.gz
tar -xvzf openssl-3.2.6.tar.gz

git clone https://github.com/nginx/nginx.git && cd nginx

.auto//configure \
--with-debug \
--prefix=/usr/local \
--conf-path=/usr/local/etc/nginx/nginx.conf \
--error-log-path=/var/log/nginx/error.log \
--http-log-path=/var/log/nginx/access.log \
--pid-path=/var/run/nginx.pid \
--lock-path=/var/run/nginx.lock \
--http-client-body-temp-path=/var/cache/nginx/client_temp \
--http-proxy-temp-path=/var/cache/nginx/proxy_temp \
--http-fastcgi-temp-path=/var/cache/nginx/fastcgi_temp \
--http-uwsgi-temp-path=/var/cache/nginx/uwsgi_temp \
--http-scgi-temp-path=/var/cache/nginx/scgi_temp \
--user=nginx \
--group=nginx \
--with-compat \
--with-file-aio \
--with-threads \
--with-http_addition_module \
--with-http_auth_request_module \
--with-http_dav_module \
--with-http_flv_module \
--with-http_gunzip_module \
--with-http_gzip_static_module \
--with-http_mp4_module \
--with-http_random_index_module \
--with-http_realip_module \
--with-http_secure_link_module \
--with-http_slice_module \
--with-http_ssl_module \
--with-http_stub_status_module \
--with-http_sub_module \
--with-http_v2_module \
--with-mail \
--with-mail_ssl_module \
--with-stream \
--with-stream_realip_module \
--with-stream_ssl_module \
--with-stream_ssl_preread_module \
--with-openssl=/home/oriyanh/repos/openssl-3.2.6/ \
--with-openssl-opt=enable-ktls \
--with-cc-opt='-g -O2 -fstack-protector-strong -Wformat -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -fPIC' \
--with-ld-opt='-Wl,-Bsymbolic-functions -Wl,-z,relro -Wl,-z,now -Wl,--as-needed -pie'

sudo make -j 24
sudo make install -j 24


## Configure nginx
# configure test server with TLS
sudo tee -a /etc/nginx/nginx.conf << EOF
user nginx;
worker_processes auto;
pid /run/nginx.pid;

events {
        worker_connections 1024;
        # multi_accept on;
}

http {

        ##
        # Basic Settings
        ##

        sendfile on;
        tcp_nopush on;
        types_hash_max_size 2048;
        # server_tokens off;

        # server_names_hash_bucket_size 64;
        # server_name_in_redirect off;

        include /etc/nginx/mime.types;
        default_type application/octet-stream;

        ##
        # SSL Settings
        ##

        ssl_protocols TLSv1.2 TLSv1.3;
        ssl_prefer_server_ciphers on;

        ##
        # Logging Settings
        ##

        access_log /var/log/nginx/access.log;
        error_log /var/log/nginx/error.log;

        ##
        # Gzip Settings
        ##

        gzip on;

        # gzip_vary on;
        # gzip_proxied any;
        # gzip_comp_level 6;
        # gzip_buffers 16 8k;
        # gzip_http_version 1.1;
        # gzip_types text/plain text/css application/json application/javascript text/xml application/xml application/xml+rss text/javascript;

        ##
        # Virtual Host Configs
        ##

        include /etc/nginx/conf.d/*.conf;
        include /etc/nginx/sites-enabled/*;
}
EOF
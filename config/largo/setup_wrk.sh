sudo apt install -y unzip
git clone https://github.com/wg/wrk.git && cd wrk/ && make -j 16
taskset -c 0 ./wrk -t1 -c10 -d30s https://4.4.4.4:443/index.html
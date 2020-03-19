FROM ubuntu:18.04 as base

#SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get --yes update && apt-get install --yes --no-install-recommends openjdk-8-jre-headless python python3 gcc build-essential wget ranger libedit-dev libxml2-dev libpython2.7-dev

WORKDIR /root

RUN wget http://sdk-releases.upmem.com/2020.1.0/ubuntu_18.04/upmem-2020.1.0-Linux-x86_64.tar.gz
RUN tar -xvzf upmem-2020.1.0-Linux-x86_64.tar.gz
RUN mv upmem-2020.1.0-Linux-x86_64 upmem-2020.1.0

#COPY "upmem-2019.4.0" "upmem-2019.4.0"

RUN echo ". $HOME/upmem-2020.1.0/upmem_env.sh" >> $HOME/.bashrc


FROM ubuntu:20.04
# LABEL about the custom image
LABEL maintainer="emartinena@unex.es"
LABEL version="0.1"
LABEL description="This is custom Docker Image for Robocomp Cortex"
RUN echo "Building with branch $Branch"
# Disable Prompt During Packages Installation
ARG DEBIAN_FRONTEND=noninteractive

COPY . /cortex/
# Update Ubuntu Software repository
RUN apt update && apt install -y \
    cmake \
    curl \
    g++ \
    gcc \
    libeigen3-dev \
    libopenscenegraph-dev \
    make \
    qtbase5-dev \
    sudo \ 
    git 
#    && rm -rf /var/lib/apt/lists/*
WORKDIR /cortex/
RUN yes | bash -x dependencies.sh  
RUN (curl -O https://raw.githubusercontent.com/robocomp/robocomp/development/classes/threadpool/threadpool.h && mkdir -p /usr/include/threadpool && mv threadpool.h /usr/include/threadpool )
RUN ls
RUN mkdir build
WORKDIR /cortex/build/
RUN cmake .. && make && sudo make install


FROM centos:7
MAINTAINER malkazwinI@gmail.com

RUN yum update iy && yum install -y make gcc gcc-c++ g++ wget autoconf autogen

RUN mkdir -p /temp

RUN mkdir -p /installs
RUN cd /installs && wget https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs922/ghostscript-9.22-linux-x86_64.tgz
RUN cd /installs && tar -xvf ghostscript-9.22-linux-x86_64.tgz

ENV PATH /installs/ghostscript-9.22-linux-x86_64:$PATH
COPY ./ghostpdl-9.22 /installs/ghostpdl-9.22

RUN cd /installs/ghostpdl-9.22 && ./autogen.sh && ./configure && make -j 5 && make install

ENTRYPOINT ["tail", "-f", "/dev/null"]
FROM debian:11.11-slim

RUN apt update \
    && apt install -y git python3 libusb-1.0-0 python3-venv cmake \
    &&  rm -rf /var/lib/apt/lists/*


COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ARG USERNAME
ARG USER_UID
ARG USER_GID

RUN groupadd -g $USER_GID $USERNAME \
    && useradd -u $USER_UID -g $USER_GID -m -s /bin/bash $USERNAME \
    # default dailout GID on linux is 20. Add the user to it for correct read/write access
    && (getent group dialout || groupadd -g 20 dialout) \
    && usermod -aG dialout $USERNAME

USER $USERNAME


WORKDIR /deps

RUN git clone --branch v5.4.1 --depth 1 https://github.com/espressif/esp-idf.git
RUN cd esp-idf \
    && git submodule update --init

# this needs to happen after the user is created so the virtual env
# gets placed in the correct path

# create the python virtual env
RUN cd esp-idf \
    && ./install.sh

# add this to .bashrc to export vars when you connect
RUN echo ". /deps/esp-idf/export.sh" >> /home/$USERNAME/.bashrc

RUN git clone --branch v0.0.3 --depth 1 https://github.com/sepfy/libpeer.git
RUN cd libpeer \ 
    && git submodule update --init

RUN git clone https://github.com/sepfy/esp_ports.git srtp
RUN cd srtp \
    && git checkout f39a4a2c70a4b7d09f9f852a58b5ea39b9c1182c \
    && git submodule update --init

WORKDIR /app

ENTRYPOINT ["/entrypoint.sh"]
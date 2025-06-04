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

WORKDIR /app

ENTRYPOINT ["/entrypoint.sh"]
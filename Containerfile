FROM docker.io/espressif/idf:latest

# Install PlatformIO CLI into the ESP-IDF Python environment
RUN /bin/bash -c 'IDF_PY_BIN="$(echo /opt/esp/python_env/*/bin | awk "{print \$1}")" && \
    "${IDF_PY_BIN}/pip" install --no-cache-dir platformio pyserial click intelhex reedsolo rich-click && \
    ln -sf "${IDF_PY_BIN}/pio" /usr/local/bin/pio && \
    ln -sf "${IDF_PY_BIN}/platformio" /usr/local/bin/platformio'

ENV PLATFORMIO_CORE_DIR=/platformio_cache
ENV PATH="/usr/local/bin:${PATH}"

WORKDIR /workspace
ENTRYPOINT []
CMD ["pio", "run", "-e", "watchy-v1-v2"]

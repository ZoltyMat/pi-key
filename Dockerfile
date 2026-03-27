# PiKey — Docker image for Raspberry Pi deployment
#
# Build:
#   docker build --platform linux/amd64 -t pikey .
#
# Run:
#   docker run --privileged --net=host \
#     -v /var/run/dbus:/var/run/dbus \
#     -v $(pwd)/config.yaml:/app/config.yaml \
#     pikey --mode both --transport auto
#
# Notes:
#   --privileged: needed for Bluetooth and USB gadget access
#   --net=host: needed for BlueZ D-Bus communication
#   /var/run/dbus: share host D-Bus socket for bluetoothd

FROM python:3.14-slim

RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        bluez \
        python3-dbus \
        python3-gi \
        gobject-introspection \
        gir1.2-glib-2.0 \
        libglib2.0-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY src/ src/
COPY config.example.yaml .

# Don't copy config.yaml — mount it at runtime
# COPY config.yaml .

ENTRYPOINT ["python3", "-m", "src.main"]
CMD ["--mode", "both", "--transport", "auto"]

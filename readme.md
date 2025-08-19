
# ESP32-S3 WebRTC H.264 Video Example
This is a minimal example project that runs a WebRTC peer on an ESP32-S3 and streams H.264 encoded video. The Dev Env runs inside a docker container to help keep development consistent. Its not a requirement.

## Features
- Runs a [libpeer](https://github.com/sepfy/libpeer) WebRTC client on the ESP32-S3
- Software encodes video in H.264
- Streams video to pion peer, then forards to the browser
- Ideal for cheap home IOT projects

## Hardware Requirements
-  [ESP32-S3](https://www.amazon.com/FORIOT-ESP32-S3-CAM-Development-ESP32-S3-WROOM-Microcontroller/dp/B0F4DKTBR9)

## Software Requirements
- Docker
- Either Linux or Mac

## Getting Started
Lets build the eps32 first, the docker portion is optional.

### 1. Clone the repository
```bash
git  https://github.com/stephanrotolante/esp32-webrtc-example.git
```

### 2. Start Docker Development container (Optional)

> 💡 **Note:**  Update the docker compose to match your needs. I am changing the **User** that the docker container is running as, and passing the USB device to the container
>
```yaml
USERNAME: stephan
USER_UID: 1000
USER_GID: 1000
devices:
	- /dev/ttyACM0:/dev/ttyACM0
```
```bash
docker-compose up --build
```

### 3. Exec into container (Optional)
```bash
docker exec -it esp-dev /bin/bash
```

### 4. Compile
> 💡 **Note:** Be sure to populate the necessary variables in the CMakeList.txt to connect to Wifi, and pass the correct URL for the whip endpoint
>

```c
add_compile_definitions(WIFI_SSID="")
add_compile_definitions(WIFI_PASSWORD="")
add_compile_definitions(WHIP_URL="http://192.168.1.250:8080/whip")
```
Set target and build
```bash
idf.py set-target esp3232
  
idf.py build
```

### 5. Start Whip Server
Open up video player in the browser and connect

```bash
cd whip-server

go run main.go
```

> 💡 **Note:** You will probably have to adjust this in the main.go file to the correct network interface. Otherwise pion is going to send all possible candidates to the esp32... Lets reduce that, memory is limited on the esp32. I was using ethernet.... Run `ip addr` to see you your interfaces
>

```go
s.SetInterfaceFilter(func(s string) (keep bool) {
	return s == "enp2s0"
})
```

### 6. Flash and Monitor ESP32s3

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

### 7. See video stream to your browser
Open your browser and subscribe to the video

<video width="640" height="480" controls>
  <source src="assets/video.mp4" type="video/mp4">
  Your browser does not support the video tag.
</video>
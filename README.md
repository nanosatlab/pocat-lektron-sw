# <sup>po</sup>CAT Lektron

This Readme is under development.

## 1. Docker installation
If you do not have docker installed in your PC:
*   **Windows / macOS:**
    1. Download [Docker Desktop](https://www.docker.com/products/docker-desktop), AMD version.  
    2. Execute installer.exe and select **"Use WSL 2 instead of Hyper-V"** when asked. 
    3. Once installed just open the docker desktop app. 
*   **Linux:**
    1. Install it via `docker.io`.

## 2. Image creation

Currently the docker image must be build by the command in the project. 
docker build -t pocat-build-env .
It might take a while to build the image (a few minutes). When finished use the following command in order to compile the project:

docker run --rm -v "${PWD}:/app" pocat-build-env

The build needs the `POCAT_PSK` environment variable: 32 hex characters, the 16-byte pre-shared key. `build.sh` uses it to generate `include/subsystems/comms/psk.h`. Never commit that file. The Docker image sets a development key.

## 3. Host unit tests

The logic of the flight software can be tested on your PC, without the STM32 board, using a separate Docker image:

```
docker build -t pocat-tests -f Dockerfile.test .
docker run --rm -v "${PWD}:/app" pocat-tests            # all test suites
docker run --rm -v "${PWD}:/app" pocat-tests --list     # list the suites
```




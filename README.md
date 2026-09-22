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

docker run --rm -v ${PWD}:/app pocat-build-env




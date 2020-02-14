# UPMEM SDK in Docker

How to obtain the UPMEM SDK environment on top of an Ubuntu image

## Pull from Docker Hub
To use a pre-built docker image, use:
`docker pull penjiboy/upmem_sdk_base`

## Build from source
Alternatively, you can also build (and modify) the docker image from source using the Dockerfile in this repository. To build, use: 
`docker build . -t upmem_sdk_base`

## Usage
It is often useful to place code in a mounted volume on the docker container. To run the container and mount a directory into the container:
`docker run -it -v /full/path/to/local/directory:/root /bin/bash`

# This Dockerfile allows building a custom image if the pre-built one 
# from ghcr.io needs modification.
# Based on NCS documentation.

ARG VERSION=latest
FROM ghcr.io/nrfconnect/sdk-nrf-toolchain:${VERSION}

# Set env to accept license automatically in CI environments
ENV ACCEPT_JLINK_LICENSE=1

# Set default shell to bash
SHELL ["/bin/bash", "-c"]

# Create working directory
WORKDIR /workdir

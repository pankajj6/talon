#!/bin/bash

sudo apt update && sudo apt install -y --no-install-recommends build-essential cmake libboost-dev git && echo "Talon simulation requirements installed successfully" || echo "Error: Talon requirements installation failed"


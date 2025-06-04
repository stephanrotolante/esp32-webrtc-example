#!/bin/bash

# mount files on host machine after container starts up
rm -rf /app/deps
cp -r /deps/ /app/

tail -f /dev/null
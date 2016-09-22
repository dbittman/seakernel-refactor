#!/bin/sh
ip link set up dev tap0
ip addr add 10.0.0.1/8 dev tap0
ip addr add fe80::1 dev tap0


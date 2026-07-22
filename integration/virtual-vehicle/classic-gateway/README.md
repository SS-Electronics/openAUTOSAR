<!-- SPDX-License-Identifier: MIT -->

# Classic Gateway Configuration

This directory contains controlled software-in-the-loop configuration for the
Classic Platform and external-system integration slice. The MVP uses SocketCAN
`vcan` and the versioned ultrasonic signal database; it does not depend on an
MCU or commercial Classic stack.

The validated source model generates a runtime key/value configuration and a
route plan under `out/generated/classic-integration`.

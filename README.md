[![Build Status](https://travis-ci.com/BlumAmir/TimeSyncClientArduino.svg?branch=master)](https://travis-ci.com/BlumAmir/TimeSyncClientArduino)
# TimeSync
This library is a client implementation of the Time Sync Protocol, which is used for precise, efficient and simple clock synchronization for multiple esp controllers that communicate over unreliable, high latency network.
LED shows are a common example of distributed system which requires very precise accuresy time synchronization to run a single visual show from different controllers over Wifi or Etherent.

# Install
Recommended installation is via the [platformio](https://platformio.org/) library manager.
Add this to your `lib_deps` in the `platformio.ini` file:
```
lib_deps =
    https://github.com/BlumAmir/TimeSyncClientArduino.git
```
and you'r good to go.

# Usage
This is the client part of the protocol. You need to install and run the [Time Sync Server](https://github.com/BlumAmir/TimeSyncServer) and configure the host and port in the client.
Usage examples are availible in the `examples` folder of the project.

# Configuration
The client can be configured with 4 values which control the required precision of the clock vs the network overhead in aquiring it.

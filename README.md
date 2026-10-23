# muart-group/esphome-components

### An ESPHome component for controlling Mitsubishi heat pumps via the CN105 port

Check out the documentation to get started at [muart-group.github.io](https://muart-group.github.io/).

This component relies heavily on the [itp-packet](https://github.com/muart-group/itp-packet) library to handle the IT Protocol communications and state.

The `main` branch will (aspirationally) contain stable code, and new development will happen on the `v*` branches (`v2` at time of writing).

Issues with the MITP component should be reported in this repository.

[Previous attempts](https://github.com/esphome/esphome/pull/7289) to merge this code into ESPHome have been unsuccessful, and the focus of this project is now on user experience and stable behavior rather than attempting to merge with ESPHome. (As much of the ITP logic has been moved to `itp-packet`, it should be relatively easy to leverage the work here in a non-ESPHome ecosystem)

## Getting started with development

The included devcontainer should take care of all the prerequisites.  In your testing ESPHome config file, you can reference your local version of this repository like:

```yaml
external_components:
  - source:
      type: local
      path: /workspaces/muart-group/esphome-components/components
```

## Architecture Notes

To help keep things a little more organized and easy to understand, some quick notes on the architecture:

### ITPByteProvider
The ITP Packet library needs bytes to parse, and the implementation of this interface here provides those using ESPHome's UART component.

### ITPPacketReceiver 
The ITP Packet library sends parsed packets via this interface, and the implementation here (mostly in `mitsubishi_itp-packetreceiving.cpp`) handles reading those packets and passing that information on to ESPHome / Home Assistant.

### ITPSystemState 
This class helps manage the overall state of the ITP system primarily by caching the latest received packets and providing some convenience methods to track whether a heat pump is connected.

### Heatpump and Thermostat
These classes represent the ITP heat pump and thermostat and are used to change settings to communicate with equipment. These are implemented in the `itp-packet` library, so more details are available there.

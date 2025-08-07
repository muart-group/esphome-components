# muart-group/esphome-components

### An ESPHome component for controlling Mitsubishi heat pumps via the CN105 port

Check out the documentation to get started at [muart-group.github.io](https://muart-group.github.io/).

Aspirationally, the `main` branch will contain stable code ready to be merged to ESPHome or elsewhere.  The `dev` branch will contain more actively developed code, but should still be relatively stable.

Issues with the MITP component should be reported in this repository.

The separate [itp-packet](https://github.com/muart-group/itp-packet) repository has been created for the logic of decoding and handling IT Protocol packets.

[This PR](https://github.com/esphome/esphome/pull/7289) is out of date enough that it's unlikely to be merged in its current state. If there is any renewed interest from the ESPHome community for merging this component we can work on a fresh PR (or updating the existing one).

## Getting started with development

The included devcontainer should take care of all the prerequisites.  In your testing ESPHome config file, you can reference your local version of this repository like:

```yaml
external_components:
  - source:
      type: local
      path: /workspaces/muart-group/esphome-components/components
```
# spatial

`spatial` unifies gesture-driven overview states into one staged flow:

- **desktop -> apps spread** (similar to scale)
- **apps spread -> workspaces spread** (similar to expo)

## comparision

Compared with running separate Wayfire `scale` and `expo` plugins, `spatial` keeps both stages in a single gesture/state machine:

- one shared progress axis (`0 -> 1 -> 2`) for transitions
- consistent swipe/pinch/keybinding behavior across both overview stages
- shared renderer and drag handling so transitions remain continuous

## references

Implementation was inspired by the combined behavior of Wayfire `scale` and `expo` plugins, but `spatial` intentionally keeps a slightly different unified interaction model tailored for this plugin.
